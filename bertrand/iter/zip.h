#ifndef BERTRAND_ITER_ZIP_H
#define BERTRAND_ITER_ZIP_H

#include "bertrand/iter/range.h"
#include "bertrand/iter/join.h"


namespace bertrand {


/// TODO: if the range is itself a tuple, and doesn't just yield tuples, then unpacking
/// it in a `zip{}` call should expand it and then broadcast the values as scalars.
/// Once that's implemented, `zip{}` should be done.

/// TODO: maybe unpacking a non-range argument should not be a problem, and always
/// attempts to unpack the individual elements rather than the whole argument.  This
/// will require some thought.

/// TODO: zip{}(args...) should be callable with zero range arguments?  That would just
/// produce a range with exactly 1 element.

/// TODO: Should zip preserve the structure of nested ranges that are provided as
/// inputs?  If you provide a range of ranges as inputs, should you get a range of
/// ranges as output?  If so, how would this be done?


namespace impl {

    /* Arguments to a zip function will be either broadcasted as lvalues if they are
    non-range arguments or iterated elementwise if they are ranges.  If a range is
    given as an `unpack` type and yields tuples, then the tuples will destructured
    when they are passed into the zip function. */
    template <typename out, typename F, typename...>
    struct _zip_call { static constexpr bool enable = false; };
    template <typename... out, typename F> requires (meta::callable<F, out...>)
    struct _zip_call<meta::pack<out...>, F> {
        static constexpr bool enable = true;
        using type = meta::call_type<F, out...>;
    };
    template <typename... out, typename F, typename A, typename... As> requires (!meta::range<A>)
    struct _zip_call<meta::pack<out...>, F, A, As...> :
        _zip_call<meta::pack<out..., A>, F, As...>
    {};
    template <typename... out, typename F, meta::range A, typename... As>
        requires (!meta::unpack<A> || !meta::tuple_like<meta::yield_type<A>>)
    struct _zip_call<meta::pack<out...>, F, A, As...> :
        _zip_call<meta::pack<out..., meta::yield_type<A>>, F, As...>
    {};
    template <typename out, typename F, meta::unpack A, typename... As>
        requires (meta::tuple_like<meta::yield_type<A>>)
    struct _zip_call<out, F, A, As...> :
        _zip_call<meta::concat<out, meta::tuple_types<meta::yield_type<A>>>, F, As...>
    {};
    template <typename F, typename... A>
    using zip_call = _zip_call<meta::pack<>, meta::as_lvalue<F>, meta::as_lvalue<A>...>;

    template <typename F, typename... A>
    concept zip_concept =
        sizeof...(A) > 0 &&
        (meta::not_void<F> && ... && meta::not_void<A>) &&
        (meta::not_rvalue<F> && ... && meta::not_rvalue<A>) &&
        (meta::range<A> || ...) &&
        zip_call<F, A...>::enable;

    template <typename C, size_t I>
    concept zip_broadcast =
        !meta::range<typename meta::unqualify<C>::argument_types::template at<I>>;

    template <typename C, size_t I>
    concept zip_unpack =
        meta::unpack<typename meta::unqualify<C>::argument_types::template at<I>> &&
        meta::tuple_like<meta::yield_type<
            typename meta::unqualify<C>::argument_types::template at<I>
        >>;

    template <typename C, size_t I> requires (zip_unpack<C, I>)
    using zip_unpack_types = meta::tuple_types<meta::yield_type<
        typename meta::unqualify<C>::argument_types::template at<I>
    >>;

    /* Zip iterators take the cvref qualifications of the zipped range into account
    when deducing the return type for the dereference operator, applying the same
    broadcasting and unpacking rules as `zip_call`. */
    template <typename out, typename C, size_t I, typename...>
    struct _zip_yield { static constexpr bool enable = false; };
    template <typename... out, typename C, size_t I>
        requires (requires(C container, out... args) {
            {container.func()(std::forward<out>(args)...)};
        })
    struct _zip_yield<meta::pack<out...>, C, I> {
        static constexpr bool enable = true;
        using type = decltype((std::declval<C>().func()(std::declval<out>()...)));
    };
    template <typename... out, typename C, size_t I, typename curr, typename... next>
        requires (zip_broadcast<C, I>)
    struct _zip_yield<meta::pack<out...>, C, I, curr, next...> :
        _zip_yield<meta::pack<out..., curr>, C, I + 1, next...>
    {};
    template <typename... out, typename C, size_t I, typename curr, typename... next>
        requires (!zip_broadcast<C, I> && !zip_unpack<C, I>)
    struct _zip_yield<meta::pack<out...>, C, I, curr, next...> :
        _zip_yield<meta::pack<out..., meta::dereference_type<curr>>, C, I + 1, next...>
    {};
    template <typename out, typename C, size_t I, typename curr, typename... next>
        requires (zip_unpack<C, I>)
    struct _zip_yield<out, C, I, curr, next...> :
        _zip_yield<meta::concat<out, zip_unpack_types<C, I>>, C, I + 1, next...>
    {};
    template <typename C, typename... Iters>
    using zip_yield = _zip_yield<meta::pack<>, C, 0, Iters...>;

    /* Zip iterators work by storing a pointer to the zipped range and a tuple of
    backing iterators, which may be interspersed with references to scalar arguments
    that will be broadcasted across each iteration.  The transformation function and
    scalar values will be accessed indirectly via the pointer, and the iterator
    interface is forwarded to the backing iterators via fold expressions.
    Dereferencing the iterator equates to calling the transformation function with the
    scalar values or dereference types of the iterator tuple.  If an iterator
    originates from an `unpack` range, then it will be destructured into its respective
    components, if it has any. */
    template <meta::lvalue C, meta::not_rvalue... Iters> requires (zip_yield<C, Iters...>::enable)
    struct zip_iterator {
    private:
        using indices = meta::unqualify<C>::indices;
        using ranges = meta::unqualify<C>::ranges;

    public:
        using iterator_category = range_category<ranges, Iters...>::type;
        using difference_type = range_difference<ranges, Iters...>::type;
        using reference = zip_yield<C, Iters...>::type;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::address_type<reference>;

        meta::as_pointer<C> container = nullptr;
        impl::basic_tuple<Iters...> iters {};

    private:
        template <size_t I, typename Self, typename... A>
            requires (I < sizeof...(Iters) && zip_broadcast<C, I>)
        constexpr decltype(auto) deref(this Self&& self, A&&... args)
            noexcept (requires{{std::forward<Self>(self).template deref<I + 1>(
                std::forward<A>(args)...,
                std::forward<Self>(self).iters.template get<I>()
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template deref<I + 1>(
                std::forward<A>(args)...,
                std::forward<Self>(self).iters.template get<I>()
            )};})
        {
            return (std::forward<Self>(self).template deref<I + 1>(
                std::forward<A>(args)...,
                std::forward<Self>(self).iters.template get<I>()
            ));
        }

        template <size_t I, typename Self, meta::tuple_like T, size_t... Is, typename... A>
        constexpr decltype(auto) _deref(
            this Self&& self,
            T&& value,
            std::index_sequence<Is...>,
            A&&... args
        )
            noexcept (requires{{std::forward<Self>(self).template deref<I + 1>(
                std::forward<A>(args)...,
                meta::get<Is>(std::forward<T>(value))...
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template deref<I + 1>(
                std::forward<A>(args)...,
                meta::get<Is>(std::forward<T>(value))...
            )};})
        {
            return (std::forward<Self>(self).template deref<I + 1>(
                std::forward<A>(args)...,
                meta::get<Is>(std::forward<T>(value))...
            ));
        }

        template <size_t I, typename Self, typename... A>
            requires (I < sizeof...(Iters) && zip_unpack<C, I>)
        constexpr decltype(auto) deref(this Self&& self, A&&... args)
            noexcept (requires{{std::forward<Self>(self).template _deref<I>(
                *std::forward<Self>(self).iters.template get<I>(),
                std::make_index_sequence<zip_unpack_types<C, I>::size()>{},
                std::forward<A>(args)...
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template _deref<I>(
                *std::forward<Self>(self).iters.template get<I>(),
                std::make_index_sequence<zip_unpack_types<C, I>::size()>{},
                std::forward<A>(args)...
            )};})
        {
            return (std::forward<Self>(self).template _deref<I>(
                *std::forward<Self>(self).iters.template get<I>(),
                std::make_index_sequence<zip_unpack_types<C, I>::size()>{},
                std::forward<A>(args)...
            ));
        }

        template <size_t I, typename Self, typename... A>
            requires (I < sizeof...(Iters) && !zip_broadcast<C, I> && !zip_unpack<C, I>)
        constexpr decltype(auto) deref(this Self&& self, A&&... args)
            noexcept (requires{{std::forward<Self>(self).template deref<I + 1>(
                std::forward<A>(args)...,
                *std::forward<Self>(self).iters.template get<I>()
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template deref<I + 1>(
                std::forward<A>(args)...,
                *std::forward<Self>(self).iters.template get<I>()
            )};})
        {
            return (std::forward<Self>(self).template deref<I + 1>(
                std::forward<A>(args)...,
                *std::forward<Self>(self).iters.template get<I>()
            ));
        }

        template <size_t I, typename Self, typename... A> requires (I == sizeof...(Iters))
        constexpr decltype(auto) deref(this Self&& self, A&&... args)
            noexcept (requires{{self.container->func()(std::forward<A>(args)...)} noexcept;})
            requires (requires{{self.container->func()(std::forward<A>(args)...)};})
        {
            return (self.container->func()(std::forward<A>(args)...));
        }

        template <size_t I, typename Self, typename... A>
            requires (I < sizeof...(Iters) && zip_broadcast<C, I>)
        constexpr decltype(auto) subscript(this Self&& self, difference_type i, A&&... args)
            noexcept (requires{{std::forward<Self>(self).template subscript<I + 1>(
                std::forward<A>(args)...,
                std::forward<Self>(self).iters.template get<I>()
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template subscript<I + 1>(
                std::forward<A>(args)...,
                std::forward<Self>(self).iters.template get<I>()
            )};})
        {
            return (std::forward<Self>(self).template subscript<I + 1>(
                std::forward<A>(args)...,
                std::forward<Self>(self).iters.template get<I>()
            ));
        }

        template <size_t I, typename Self, meta::tuple_like T, size_t... Is, typename... A>
        constexpr decltype(auto) _subscript(
            this Self&& self,
            difference_type i,
            T&& value,
            std::index_sequence<Is...>,
            A&&... args
        )
            noexcept (requires{{std::forward<Self>(self).template subscript<I + 1>(
                i,
                std::forward<A>(args)...,
                meta::get<Is>(std::forward<T>(value))...
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template subscript<I + 1>(
                i,
                std::forward<A>(args)...,
                meta::get<Is>(std::forward<T>(value))...
            )};})
        {
            return (std::forward<Self>(self).template subscript<I + 1>(
                i,
                std::forward<A>(args)...,
                meta::get<Is>(std::forward<T>(value))...
            ));
        }

        template <size_t I, typename Self, typename... A>
            requires (I < sizeof...(Iters) && zip_unpack<C, I>)
        constexpr decltype(auto) subscript(this Self&& self, difference_type i, A&&... args)
            noexcept (requires{{std::forward<Self>(self).template _subscript<I>(
                i,
                std::forward<Self>(self).iters.template get<I>()[i],
                std::make_index_sequence<zip_unpack_types<C, I>::size()>{},
                std::forward<A>(args)...
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template _subscript<I>(
                i,
                std::forward<Self>(self).iters.template get<I>()[i],
                std::make_index_sequence<zip_unpack_types<C, I>::size()>{},
                std::forward<A>(args)...
            )};})
        {
            return (std::forward<Self>(self).template _subscript<I>(
                i,
                std::forward<Self>(self).iters.template get<I>()[i],
                std::make_index_sequence<zip_unpack_types<C, I>::size()>{},
                std::forward<A>(args)...
            ));
        }

        template <size_t I, typename Self, typename... A>
            requires (I < sizeof...(Iters) && !zip_broadcast<C, I> && !zip_unpack<C, I>)
        constexpr decltype(auto) subscript(this Self&& self, difference_type i, A&&... args)
            noexcept (requires{{std::forward<Self>(self).template subscript<I + 1>(
                std::forward<A>(args)...,
                std::forward<Self>(self).iters.template get<I>()[i]
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template subscript<I + 1>(
                std::forward<A>(args)...,
                std::forward<Self>(self).iters.template get<I>()[i]
            )};})
        {
            return (std::forward<Self>(self).template subscript<I + 1>(
                std::forward<A>(args)...,
                std::forward<Self>(self).iters.template get<I>()[i]
            ));
        }

        template <size_t I, typename Self, typename... A> requires (I == sizeof...(Iters))
        constexpr decltype(auto) subscript(this Self&& self, difference_type i, A&&... args)
            noexcept (requires{{self.container->func()(std::forward<A>(args)...)} noexcept;})
            requires (requires{{self.container->func()(std::forward<A>(args)...)};})
        {
            return (self.container->func()(std::forward<A>(args)...));
        }

        template <size_t... Is> requires (sizeof...(Is) == ranges::size())
        constexpr void increment(std::index_sequence<Is...>)
            noexcept (requires{{((++iters.template get<Is>()), ...)} noexcept;})
            requires (requires{{((++iters.template get<Is>()), ...)};})
        {
            ((++iters.template get<Is>()), ...);
        }

        template <size_t I, typename T>
        static constexpr decltype(auto) _add(const T& value, difference_type i)
            noexcept (zip_broadcast<C, I> || requires{{value + i} noexcept;})
            requires (zip_broadcast<C, I> || requires{{value + i};})
        {
            if constexpr (zip_broadcast<C, I>) {
                return (value);
            } else {
                return (value + i);
            }
        }

        template <size_t... Is> requires (sizeof...(Is) == indices::size())
        constexpr zip_iterator add(difference_type i, std::index_sequence<Is...>) const
            noexcept (requires{{zip_iterator{
                .container = container,
                .iters = impl::basic_tuple<Iters...>{_add<Is>(iters.template get<Is>(), i)...}
            }} noexcept;})
            requires (requires{{zip_iterator{
                .container = container,
                .iters = impl::basic_tuple<Iters...>{_add<Is>(iters.template get<Is>(), i)...}
            }};})
        {
            return {
                .container = container,
                .iters = impl::basic_tuple<Iters...>{_add<Is>(iters.template get<Is>(), i)...}
            };
        }

        template <size_t... Is> requires (sizeof...(Is) == ranges::size())
        constexpr void iadd(difference_type i, std::index_sequence<Is...>)
            noexcept (requires{{((iters.template get<Is>() += i), ...)} noexcept;})
            requires (requires{{((iters.template get<Is>() += i), ...)};})
        {
            ((iters.template get<Is>() += i), ...);
        }

        template <size_t... Is> requires (sizeof...(Is) == ranges::size())
        constexpr void decrement(std::index_sequence<Is...>)
            noexcept (requires{{((--iters.template get<Is>()), ...)} noexcept;})
            requires (requires{{((--iters.template get<Is>()), ...)};})
        {
            ((--iters.template get<Is>()), ...);
        }

        template <size_t I, typename T>
        static constexpr decltype(auto) _sub(const T& value, difference_type i)
            noexcept (zip_broadcast<C, I> || requires{{value - i} noexcept;})
            requires (zip_broadcast<C, I> || requires{{value - i};})
        {
            if constexpr (zip_broadcast<C, I>) {
                return (value);
            } else {
                return (value - i);
            }
        }

        template <size_t... Is> requires (sizeof...(Is) == indices::size())
        constexpr zip_iterator sub(difference_type i, std::index_sequence<Is...>) const
            noexcept (requires{{zip_iterator{
                .container = container,
                .iters = impl::basic_tuple<Iters...>{_sub<Is>(iters.template get<Is>(), i)...}
            }} noexcept;})
            requires (requires{{zip_iterator{
                .container = container,
                .iters = impl::basic_tuple<Iters...>{_sub<Is>(iters.template get<Is>(), i)...}
            }};})
        {
            return {
                .container = container,
                .iters = impl::basic_tuple<Iters...>{_sub<Is>(iters.template get<Is>(), i)...}
            };
        }

        template <typename L, typename R>
        static constexpr void _distance(L&& lhs, R&& rhs, difference_type& min)
            noexcept (requires{
                {lhs - rhs} noexcept -> meta::nothrow::convertible_to<difference_type>;
            })
            requires (requires{{lhs - rhs} -> meta::convertible_to<difference_type>;})
        {
            difference_type d = lhs - rhs;
            if (d < 0) {
                if (d > min) {
                    min = d;
                }
            } else {
                if (d < min) {
                    min = d;
                }
            }
        }

        template <typename... Ts, size_t I, size_t... Is>
        constexpr difference_type distance(
            const zip_iterator<C, Ts...>& other,
            std::index_sequence<I, Is...>
        ) const
            noexcept (requires{
                {
                    iters.template get<I>() - other.iters.template get<I>()
                } noexcept -> meta::nothrow::convertible_to<difference_type>;
                {(_distance(
                    iters.template get<Is>(),
                    other.iters.template get<Is>(),
                    iters.template get<I>() - other.iters.template get<I>()
                ), ...)} noexcept;
            })
            requires (requires{
                {
                    iters.template get<I>() - other.iters.template get<I>()
                } -> meta::convertible_to<difference_type>;
                {(_distance(
                    iters.template get<Is>(),
                    other.iters.template get<Is>(),
                    iters.template get<I>() - other.iters.template get<I>()
                ), ...)};
            })
        {
            difference_type min = iters.template get<I>() - other.iters.template get<I>();
            (_distance(
                iters.template get<Is>(),
                other.iters.template get<Is>(),
                min
            ), ...);
            return min;
        }

        template <size_t... Is> requires (sizeof...(Is) == ranges::size())
        constexpr void isub(difference_type i, std::index_sequence<Is...>)
            noexcept (requires{{((iters.template get<Is>() -= i), ...)} noexcept;})
            requires (requires{{((iters.template get<Is>() -= i), ...)};})
        {
            ((iters.template get<Is>() -= i), ...);
        }

        template <typename... Ts, size_t... Is> requires (sizeof...(Is) == ranges::size())
        constexpr bool lt(const zip_iterator<C, Ts...>& other, std::index_sequence<Is...>) const
            noexcept (requires{{
                ((iters.template get<Is>() < other.iters.template get<Is>()) && ...)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                ((iters.template get<Is>() < other.iters.template get<Is>()) && ...)
            } -> meta::convertible_to<bool>;})
        {
            return ((iters.template get<Is>() < other.iters.template get<Is>()) && ...);
        }

        template <typename... Ts, size_t... Is> requires (sizeof...(Is) == ranges::size())
        constexpr bool le(const zip_iterator<C, Ts...>& other, std::index_sequence<Is...>) const
            noexcept (requires{{
                ((iters.template get<Is>() <= other.iters.template get<Is>()) && ...)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                ((iters.template get<Is>() <= other.iters.template get<Is>()) && ...)
            } -> meta::convertible_to<bool>;})
        {
            return ((iters.template get<Is>() <= other.iters.template get<Is>()) && ...);
        }

        template <typename... Ts, size_t... Is> requires (sizeof...(Is) == ranges::size())
        constexpr bool eq(const zip_iterator<C, Ts...>& other, std::index_sequence<Is...>) const
            noexcept (requires{{
                ((iters.template get<Is>() == other.iters.template get<Is>()) && ...)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                ((iters.template get<Is>() == other.iters.template get<Is>()) && ...)
            } -> meta::convertible_to<bool>;})
        {
            return ((iters.template get<Is>() == other.iters.template get<Is>()) && ...);
        }

        template <typename... Ts, size_t... Is> requires (sizeof...(Is) == ranges::size())
        constexpr bool ne(const zip_iterator<C, Ts...>& other, std::index_sequence<Is...>) const
            noexcept (requires{{
                ((iters.template get<Is>() != other.iters.template get<Is>()) && ...)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                ((iters.template get<Is>() != other.iters.template get<Is>()) && ...)
            } -> meta::convertible_to<bool>;})
        {
            return ((iters.template get<Is>() != other.iters.template get<Is>()) && ...);
        }

        template <typename... Ts, size_t... Is> requires (sizeof...(Is) == ranges::size())
        constexpr bool ge(const zip_iterator<C, Ts...>& other, std::index_sequence<Is...>) const
            noexcept (requires{{
                ((iters.template get<Is>() >= other.iters.template get<Is>()) && ...)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                ((iters.template get<Is>() >= other.iters.template get<Is>()) && ...)
            } -> meta::convertible_to<bool>;})
        {
            return ((iters.template get<Is>() >= other.iters.template get<Is>()) && ...);
        }

        template <typename... Ts, size_t... Is> requires (sizeof...(Is) == ranges::size())
        constexpr bool gt(const zip_iterator<C, Ts...>& other, std::index_sequence<Is...>) const
            noexcept (requires{{
                ((iters.template get<Is>() > other.iters.template get<Is>()) && ...)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                ((iters.template get<Is>() > other.iters.template get<Is>()) && ...)
            } -> meta::convertible_to<bool>;})
        {
            return ((iters.template get<Is>() > other.iters.template get<Is>()) && ...);
        }

    public:
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).template deref<0>()} noexcept;})
            requires (requires{{std::forward<Self>(self).template deref<0>()};})
        {
            return (std::forward<Self>(self).template deref<0>());
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{impl::arrow_proxy{*std::forward<Self>(self)}} noexcept;})
            requires (requires{{impl::arrow_proxy{*std::forward<Self>(self)}};})
        {
            return impl::arrow_proxy{*std::forward<Self>(self)};
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, difference_type i)
            noexcept (requires{{std::forward<Self>(self).template subscript<0>(i)} noexcept;})
            requires (requires{{std::forward<Self>(self).template subscript<0>(i)};})
        {
            return (std::forward<Self>(self).template subscript<0>(i));
        }

        constexpr zip_iterator& operator++()
            noexcept (requires{{increment(ranges{})} noexcept;})
            requires (requires{{increment(ranges{})};})
        {
            increment(ranges{});
            return *this;
        }

        [[nodiscard]] constexpr zip_iterator operator++(int)
            noexcept (
                meta::nothrow::copyable<zip_iterator> &&
                meta::nothrow::has_preincrement<zip_iterator&>
            )
            requires (meta::copyable<zip_iterator> && meta::has_preincrement<zip_iterator&>)
        {
            zip_iterator copy = *this;
            ++*this;
            return copy;
        }

        [[nodiscard]] friend constexpr zip_iterator operator+(
            const zip_iterator& self,
            difference_type i
        )
            noexcept (requires{{self.add(i, indices{})} noexcept;})
            requires (requires{{self.add(i, indices{})};})
        {
            return self.add(i, indices{});
        }

        [[nodiscard]] friend constexpr zip_iterator operator+(
            difference_type i,
            const zip_iterator& self
        )
            noexcept (requires{{self.add(i, indices{})} noexcept;})
            requires (requires{{self.add(i, indices{})};})
        {
            return self.add(i, indices{});
        }

        constexpr zip_iterator& operator+=(difference_type i)
            noexcept (requires{{iadd(i, ranges{})} noexcept;})
            requires (requires{{iadd(i, ranges{})};})
        {
            iadd(i, ranges{});
            return *this;
        }

        constexpr zip_iterator& operator--()
            noexcept (requires{{decrement(ranges{})} noexcept;})
            requires (requires{{decrement(ranges{})};})
        {
            decrement(ranges{});
            return *this;
        }

        [[nodiscard]] constexpr zip_iterator operator--(int)
            noexcept (
                meta::nothrow::copyable<zip_iterator> &&
                meta::nothrow::has_predecrement<zip_iterator&>
            )
            requires (meta::copyable<zip_iterator> && meta::has_predecrement<zip_iterator&>)
        {
            zip_iterator copy = *this;
            --*this;
            return copy;
        }

        [[nodiscard]] constexpr zip_iterator operator-(difference_type i) const
            noexcept (requires{{sub(i, indices{})} noexcept;})
            requires (requires{{sub(i, indices{})};})
        {
            return sub(i, indices{});
        }

        [[nodiscard]] constexpr difference_type operator-(
            const zip_iterator<C, Iters...>& other
        ) const
            noexcept (requires{{distance(other, ranges{})} noexcept;})
            requires (requires{{distance(other, ranges{})};})
        {
            return distance(other, ranges{});
        }

        constexpr zip_iterator& operator-=(difference_type i)
            noexcept (requires{{isub(i, ranges{})} noexcept;})
            requires (requires{{isub(i, ranges{})};})
        {
            isub(i, ranges{});
            return *this;
        }

        template <typename... Ts>
        [[nodiscard]] constexpr bool operator<(const zip_iterator<C, Ts...>& other) const
            noexcept (requires{{lt(other, ranges{})} noexcept;})
            requires (requires{{lt(other, ranges{})};})
        {
            return lt(other, ranges{});
        }

        template <typename... Ts>
        [[nodiscard]] constexpr bool operator<=(const zip_iterator<C, Ts...>& other) const
            noexcept (requires{{le(other, ranges{})} noexcept;})
            requires (requires{{le(other, ranges{})};})
        {
            return le(other, ranges{});
        }

        template <typename... Ts>
        [[nodiscard]] constexpr bool operator==(const zip_iterator<C, Ts...>& other) const
            noexcept (requires{{eq(other, ranges{})} noexcept;})
            requires (requires{{eq(other, ranges{})};})
        {
            return eq(other, ranges{});
        }

        template <typename... Ts>
        [[nodiscard]] constexpr bool operator!=(const zip_iterator<C, Ts...>& other) const
            noexcept (requires{{ne(other, ranges{})} noexcept;})
            requires (requires{{ne(other, ranges{})};})
        {
            return ne(other, ranges{});
        }

        template <typename... Ts>
        [[nodiscard]] constexpr bool operator>=(const zip_iterator<C, Ts...>& other) const
            noexcept (requires{{ge(other, ranges{})} noexcept;})
            requires (requires{{ge(other, ranges{})};})
        {
            return ge(other, ranges{});
        }

        template <typename... Ts>
        [[nodiscard]] constexpr bool operator>(const zip_iterator<C, Ts...>& other) const
            noexcept (requires{{gt(other, ranges{})} noexcept;})
            requires (requires{{gt(other, ranges{})};})
        {
            return gt(other, ranges{});
        }
    };

    template <meta::lvalue T>
    struct make_zip_begin {
        using type = T;
        T arg;
        constexpr type operator()() noexcept { return arg; }
    };
    template <meta::lvalue T> requires (meta::range<T>)
    struct make_zip_begin<T> {
        using type = meta::begin_type<T>;
        T arg;
        constexpr type operator()()
            noexcept (requires{{arg.begin()} noexcept;})
            requires (requires{{arg.begin()};})
        {
            return arg.begin();
        }
    };
    template <typename T>
    make_zip_begin(T&) -> make_zip_begin<T&>;

    template <meta::lvalue T>
    struct make_zip_end {
        using type = T;
        T arg;
        constexpr type operator()() noexcept { return arg; }
    };
    template <meta::lvalue T> requires (meta::range<T>)
    struct make_zip_end<T> {
        using type = meta::end_type<T>;
        T arg;
        constexpr type operator()()
            noexcept (requires{{arg.end()} noexcept;})
            requires (requires{{arg.end()};})
        {
            return arg.end();
        }
    };
    template <typename T>
    make_zip_end(T&) -> make_zip_end<T&>;

    /* Forward iterators over `zip` ranges consist of an inner tuple holding either a
    reference to a non-range argument or an appropriate range iterator at each index.
    If all ranges use the same begin and end types, then the overall zip iterators will
    also match. */
    template <meta::lvalue T, typename>
    struct _make_zip_iterator;
    template <meta::lvalue T, size_t... Is>
    struct _make_zip_iterator<T, std::index_sequence<Is...>> {
        using begin = zip_iterator<
            T,
            typename make_zip_begin<decltype((std::declval<T>().template arg<Is>()))>::type...
        >;
        using end = zip_iterator<
            T,
            typename make_zip_end<decltype((std::declval<T>().template arg<Is>()))>::type...
        >;
    };
    template <meta::lvalue T>
    struct make_zip_iterator {
        T container;

    private:
        using indices = meta::unqualify<T>::indices;
        using type = _make_zip_iterator<T, indices>;

        template <size_t... Is>
        constexpr type::begin _begin(std::index_sequence<Is...>)
            noexcept (requires{{typename type::begin{
                .container = std::addressof(container),
                .iters = {make_zip_begin{container.template arg<Is>()}()...}
            }} noexcept;})
            requires (requires{{typename type::begin{
                .container = std::addressof(container),
                .iters = {make_zip_begin{container.template arg<Is>()}()...}
            }};})
        {
            return {
                .container = std::addressof(container),
                .iters = {make_zip_begin{container.template arg<Is>()}()...}
            };
        }

        template <size_t... Is>
        constexpr type::end _end(std::index_sequence<Is...>)
            noexcept (requires{{typename type::end{
                .container = std::addressof(container),
                .iters = {make_zip_end{container.template arg<Is>()}()...}
            }} noexcept;})
            requires (requires{{typename type::end{
                .container = std::addressof(container),
                .iters = {make_zip_end{container.template arg<Is>()}()...}
            }};})
        {
            return {
                .container = std::addressof(container),
                .iters = {make_zip_end{container.template arg<Is>()}()...}
            };
        }

    public:
        using begin_type = type::begin;
        using end_type = type::end;

        [[nodiscard]] constexpr begin_type begin()
            noexcept (requires{{_begin(indices{})} noexcept;})
            requires (requires{{_begin(indices{})};})
        {
            return _begin(indices{});
        }

        [[nodiscard]] constexpr end_type end()
            noexcept (requires{{_end(indices{})} noexcept;})
            requires (requires{{_end(indices{})};})
        {
            return _end(indices{});
        }
    };
    template <typename T>
    make_zip_iterator(T&) -> make_zip_iterator<T&>;

    template <meta::lvalue T>
    struct make_zip_rbegin {
        using type = meta::as_lvalue<T>;
        T arg;
        constexpr type operator()() noexcept { return arg; }
    };
    template <meta::lvalue T> requires (meta::range<T>)
    struct make_zip_rbegin<T> {
        using type = meta::rbegin_type<T>;
        T arg;
        constexpr type operator()()
            noexcept (requires{{arg.rbegin()} noexcept;})
            requires (requires{{arg.rbegin()};})
        {
            return arg.rbegin();
        }
    };
    template <meta::lvalue T>
    make_zip_rbegin(T&) -> make_zip_rbegin<T&>;

    template <meta::lvalue T>
    struct make_zip_rend {
        using type = meta::as_lvalue<T>;
        T arg;
        constexpr type operator()() noexcept { return arg; }
    };
    template <meta::lvalue T> requires (meta::range<T>)
    struct make_zip_rend<T> {
        using type = meta::rend_type<T>;
        T arg;
        constexpr type operator()()
            noexcept (requires{{arg.rend()} noexcept;})
            requires (requires{{arg.rend()};})
        {
            return arg.rend();
        }
    };
    template <meta::lvalue T>
    make_zip_rend(T&) -> make_zip_rend<T&>;

    /* If all of the input ranges happen to be reverse iterable, then the zipped range
    will also be reverse iterable, and the rbegin and rend iterators will match if
    all of the input ranges use the same rbegin and rend types. */
    template <meta::lvalue T, typename>
    struct _make_zip_reversed;
    template <meta::lvalue T, size_t... Is>
    struct _make_zip_reversed<T, std::index_sequence<Is...>> {
        using begin = zip_iterator<
            T,
            typename make_zip_rbegin<decltype((std::declval<T>().template arg<Is>()))>::type...
        >;
        using end = zip_iterator<
            T,
            typename make_zip_rend<decltype((std::declval<T>().template arg<Is>()))>::type...
        >;
    };
    template <meta::lvalue T>
        requires (range_reverse_iterable<typename meta::unqualify<T>::argument_types>)
    struct make_zip_reversed {
        T container;

    private:
        using indices = meta::unqualify<T>::indices;
        using type = _make_zip_reversed<T, indices>;

        template <size_t... Is>
        constexpr type::begin _begin(std::index_sequence<Is...>)
            noexcept (requires{{typename type::begin{
                .container = std::addressof(container),
                .iters = {make_zip_rbegin{container.template arg<Is>()}()...}
            }} noexcept;})
            requires (requires{{typename type::begin{
                .container = std::addressof(container),
                .iters = {make_zip_rbegin{container.template arg<Is>()}()...}
            }};})
        {
            return {
                .container = std::addressof(container),
                .iters = {make_zip_rbegin{container.template arg<Is>()}()...}
            };
        }

        template <size_t... Is>
        constexpr type::end _end(std::index_sequence<Is...>)
            noexcept (requires{{typename type::end{
                .container = std::addressof(container),
                .iters = {make_zip_rend{container.template arg<Is>()}()...}
            }} noexcept;})
            requires (requires{{typename type::end{
                .container = std::addressof(container),
                .iters = {make_zip_rend{container.template arg<Is>()}()...}
            }};})
        {
            return {
                .container = std::addressof(container),
                .iters = {make_zip_rend{container.template arg<Is>()}()...}
            };
        }

    public:
        using begin_type = type::begin;
        using end_type = type::end;

        [[nodiscard]] constexpr begin_type begin()
            noexcept (requires{{_begin(indices{})} noexcept;})
            requires (requires{{_begin(indices{})};})
        {
            return _begin(indices{});
        }

        [[nodiscard]] constexpr end_type end()
            noexcept (requires{{_end(indices{})} noexcept;})
            requires (requires{{_end(indices{})};})
        {
            return _end(indices{});
        }
    };
    template <typename T>
    make_zip_reversed(T&) -> make_zip_reversed<T&>;

    template <size_t min, typename...>
    constexpr size_t _zip_tuple_size = min;
    template <size_t min, typename T, typename... Ts>
    constexpr size_t _zip_tuple_size<min, T, Ts...> = _zip_tuple_size<min, Ts...>;
    template <size_t min, meta::range T, typename... Ts>
        requires (meta::tuple_like<T> && meta::tuple_size<T> < min)
    constexpr size_t _zip_tuple_size<min, T, Ts...> = _zip_tuple_size<meta::tuple_size<T>, Ts...>;
    template <typename... Ts>
    constexpr size_t zip_tuple_size = _zip_tuple_size<std::numeric_limits<size_t>::max(), Ts...>;

    /* Zipped ranges store an arbitrary set of argument types as well as a function to
    apply over them.  The arguments are not required to be ranges, and will be
    broadcasted as lvalues over the length of the range.  If any of the arguments are
    ranges, then they will be iterated over like normal. */
    template <typename F, typename... A> requires (zip_concept<F, A...>)
    struct zip {
        using function_type = F;
        using argument_types = meta::pack<A...>;
        using return_type = zip_call<F, A...>::type;
        using size_type = size_t;
        using index_type = ssize_t;
        using indices = std::make_index_sequence<sizeof...(A)>;
        using ranges = range_indices<A...>;

        [[no_unique_address]] impl::ref<F> m_func;
        [[no_unique_address]] impl::basic_tuple<A...> m_args;

        /* Perfectly forward the underlying transformation function. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) func(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_func);
        }

        /* Perfectly forward the I-th zipped argument. */
        template <size_t I, typename Self> requires (I < sizeof...(A))
        [[nodiscard]] constexpr decltype(auto) arg(this Self&& self) noexcept {
            return (std::forward<Self>(self).m_args.template get<I>());
        }

    private:
        static constexpr bool sized = ((!meta::range<A> || meta::has_size<A>) && ...);
        static constexpr bool tuple_like = ((!meta::range<A> || meta::tuple_like<A>) && ...);
        static constexpr bool sequence_like = (meta::sequence<A> || ...);

        template <typename T>
        static constexpr bool has_size_impl(const T& value) noexcept {
            if constexpr (meta::sequence<T>) {
                return value.has_size();
            } else {
                return true;
            }
        }

        template <size_t... Is> requires (sequence_like && sizeof...(Is) == ranges::size())
        constexpr bool _has_size(std::index_sequence<Is...>) const noexcept {
            return (has_size_impl(arg<Is>()) && ...);
        }

        template <size_t... Is> requires (sizeof...(Is) == ranges::size())
        constexpr size_type _size(std::index_sequence<Is...>) const
            noexcept (requires{{std::min({size_type(arg<Is>().size())...})} noexcept;})
            requires (requires{{std::min({size_type(arg<Is>().size())...})};})
        {
            return std::min({size_type(arg<Is>().size())...});
        }

    public:
        /* If any of the input ranges are `sequence` types, then it's possible that
        `.size()` could throw a runtime error due to type erasure, which acts as a
        SFINAE barrier with respect to the underlying container.  In order to handle
        this, a zipped range consisting of one or more sequences will expose the same
        `has_size()` accessor as the sequences themselves, and will return their
        logical conjunction. */
        [[nodiscard]] static constexpr bool has_size() noexcept requires (sized && !sequence_like) {
            return true;
        }

        /* If any of the input ranges are `sequence` types, then it's possible that
        `.size()` could throw a runtime error due to type erasure, which acts as a
        SFINAE barrier with respect to the underlying container.  In order to handle
        this, a zipped range consisting of one or more sequences will expose the same
        `has_size()` accessor as the sequences themselves, and will return their
        logical conjunction. */
        [[nodiscard]] constexpr bool has_size() const noexcept requires (sized && sequence_like) {
            return _has_size(ranges{});
        }

        /* The overall size of the zipped range as an unsigned integer.  This is only
        enabled if all of the arguments are either sized ranges or non-range inputs, in
        which case it will return the minimum size of the constituent ranges.  If all
        of the ranges are tuple-like, then the size will be computed statically at
        compile time.  Otherwise, it will be computed using a fold over the input
        ranges.
        
        Note that if all of the input ranges are `sequence` types (which may or may not
        be sized), then this method may throw an `IndexError` if and only if
        `sequence.has_size()` evaluates to `false` for any of the input ranges.  This
        is a consequence of type erasure on the underlying container, and may be worked
        around via the `has_size()` method.  If that method returns `true`, then this
        method will never throw. */
        [[nodiscard]] static constexpr size_type size() noexcept requires (tuple_like) {
            return zip_tuple_size<A...>;
        }

        /* The overall size of the zipped range as an unsigned integer.  This is only
        enabled if all of the arguments are either sized ranges or non-range inputs, in
        which case it will return the minimum size of the constituent ranges.  If all
        of the ranges are tuple-like, then the size will be computed statically at
        compile time.  Otherwise, it will be computed using a fold over the input
        ranges.

        Note that if any of the input ranges are `sequence` types (which may or may not
        be sized), then this method may throw an `IndexError` if `sequence.has_size()`
        evaluates to `false` for any of the sequences.  This is a consequence of type
        erasure on the underlying container, which acts as a SFINAE barrier for the
        compiler, and may be worked around via the `has_size()` method.  If that method
        returns `true`, then this method will never throw. */
        [[nodiscard]] constexpr size_type size() const
            noexcept (requires{{_size(ranges{})} noexcept;})
            requires (!tuple_like && sized)
        {
            return _size(ranges{});
        }

        /* The overall size of the zipped range as a signed integer.  This is only
        enabled if all of the arguments are either sized ranges or non-range inputs. */
        [[nodiscard]] constexpr index_type ssize() const
            noexcept (requires{{index_type(size())} noexcept;})
            requires (sized)
        {
            return index_type(size());
        }

        /* True if the zipped range contains no elements.  False otherwise. */
        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{begin() == end()} noexcept;})
        {
            return begin() == end();
        }

    private:
        template <size_t I, size_t J, typename Self, typename... Ts>
            requires (J < sizeof...(A) && zip_broadcast<Self, J>)
        constexpr decltype(auto) _get(this Self&& self, Ts&&... args)
            noexcept (requires{{std::forward<Self>(self).template _get<I, J + 1>(
                std::forward<Ts>(args)...,
                std::forward<Self>(self).template arg<J>()
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template _get<I, J + 1>(
                std::forward<Ts>(args)...,
                std::forward<Self>(self).template arg<J>()
            )};})
        {
            return (std::forward<Self>(self).template _get<I, J + 1>(
                std::forward<Ts>(args)...,
                std::forward<Self>(self).template arg<J>()
            ));
        }

        template <
            size_t I,
            size_t J,
            typename Self,
            meta::tuple_like T,
            size_t... Is,
            typename... Ts
        >
        constexpr decltype(auto) _get_impl(
            this Self&& self,
            T&& value,
            std::index_sequence<Is...>,
            Ts&&... args
        )
            noexcept (requires{{std::forward<Self>(self).template _get<I, J + 1>(
                std::forward<Ts>(args)...,
                meta::get<Is>(std::forward<T>(value))...
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template _get<I, J + 1>(
                std::forward<Ts>(args)...,
                meta::get<Is>(std::forward<T>(value))...
            )};})
        {
            return (std::forward<Self>(self).template _get<I, J + 1>(
                std::forward<Ts>(args)...,
                meta::get<Is>(std::forward<T>(value))...
            ));
        }

        template <size_t I, size_t J, typename Self, typename... Ts>
            requires (J < sizeof...(A) && zip_unpack<Self, J>)
        constexpr decltype(auto) _get(this Self&& self, Ts&&... args)
            noexcept (requires{{std::forward<Self>(self).template _get_impl<I, J>(
                std::forward<Self>(self).template arg<J>().template get<I>(),
                std::make_index_sequence<zip_unpack_types<Self, J>::size()>{},
                std::forward<Ts>(args)...
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template _get_impl<I, J>(
                std::forward<Self>(self).template arg<J>().template get<I>(),
                std::make_index_sequence<zip_unpack_types<Self, J>::size()>{},
                std::forward<Ts>(args)...
            )};})
        {
            return (std::forward<Self>(self).template _get_impl<I, J>(
                std::forward<Self>(self).template arg<J>().template get<I>(),
                std::make_index_sequence<zip_unpack_types<Self, J>::size()>{},
                std::forward<Ts>(args)...
            ));
        }

        template <size_t I, size_t J, typename Self, typename... Ts>
            requires (J < sizeof...(A) && !zip_broadcast<Self, J> && !zip_unpack<Self, J>)
        [[nodiscard]] constexpr decltype(auto) _get(this Self&& self, Ts&&... args)
            noexcept (requires{{std::forward<Self>(self).template _get<I, J + 1>(
                std::forward<Ts>(args)...,
                std::forward<Self>(self).template arg<J>().template get<I>()
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template _get<I, J + 1>(
                std::forward<Ts>(args)...,
                std::forward<Self>(self).template arg<J>().template get<I>()
            )};})
        {
            return (std::forward<Self>(self).template _get<I, J + 1>(
                std::forward<Ts>(args)...,
                std::forward<Self>(self).template arg<J>().template get<I>()
            ));
        }

        template <size_t I, size_t J, typename Self, typename... Ts> requires (J == sizeof...(A))
        constexpr decltype(auto) _get(this Self&& self, Ts&&... args)
            noexcept (requires{
                {std::forward<Self>(self).func()(std::forward<Ts>(args)...)} noexcept;
            })
            requires (requires{
                {std::forward<Self>(self).func()(std::forward<Ts>(args)...)};
            })
        {
            return (std::forward<Self>(self).func()(std::forward<Ts>(args)...));
        }

        template <size_t J, typename Self, typename... Ts>
            requires (J < sizeof...(A) && zip_broadcast<Self, J>)
        constexpr decltype(auto) subscript(this Self&& self, index_type i, Ts&&... args)
            noexcept (requires{{std::forward<Self>(self).template subscript<J + 1>(
                i,
                std::forward<Ts>(args)...,
                std::forward<Self>(self).template arg<J>()
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template subscript<J + 1>(
                i,
                std::forward<Ts>(args)...,
                std::forward<Self>(self).template arg<J>()
            )};})
        {
            return (std::forward<Self>(self).template subscript<J + 1>(
                i,
                std::forward<Ts>(args)...,
                std::forward<Self>(self).template arg<J>()
            ));
        }

        template <size_t J, typename Self, meta::tuple_like T, size_t... Is, typename... Ts>
        constexpr decltype(auto) _subscript(
            this Self&& self,
            index_type i,
            T&& value,
            std::index_sequence<Is...>,
            Ts&&... args
        )
            noexcept (requires{{std::forward<Self>(self).template subscript<J + 1>(
                i,
                std::forward<Ts>(args)...,
                meta::get<Is>(std::forward<T>(value))...
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template subscript<J + 1>(
                i,
                std::forward<Ts>(args)...,
                meta::get<Is>(std::forward<T>(value))...
            )};})
        {
            return (std::forward<Self>(self).template subscript<J + 1>(
                i,
                std::forward<Ts>(args)...,
                meta::get<Is>(std::forward<T>(value))...
            ));
        }

        template <size_t J, typename Self, typename... Ts>
            requires (J < sizeof...(A) && zip_unpack<Self, J>)
        constexpr decltype(auto) subscript(this Self&& self, index_type i, Ts&&... args)
            noexcept (requires{{std::forward<Self>(self).template _subscript<J>(
                i,
                std::forward<Self>(self).template arg<J>()[i],
                std::make_index_sequence<zip_unpack_types<Self, J>::size()>{},
                std::forward<Ts>(args)...
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template _subscript<J>(
                i,
                std::forward<Self>(self).template arg<J>()[i],
                std::make_index_sequence<zip_unpack_types<Self, J>::size()>{},
                std::forward<Ts>(args)...
            )};})
        {
            return (std::forward<Self>(self).template _subscript<J>(
                i,
                std::forward<Self>(self).template arg<J>()[i],
                std::make_index_sequence<zip_unpack_types<Self, J>::size()>{},
                std::forward<Ts>(args)...
            ));
        }

        template <size_t J, typename Self, typename... Ts>
            requires (J < sizeof...(A) && !zip_broadcast<Self, J> && !zip_unpack<Self, J>)
        constexpr decltype(auto) subscript(this Self&& self, index_type i, Ts&&... args)
            noexcept (requires{{std::forward<Self>(self).template subscript<J + 1>(
                i,
                std::forward<Ts>(args)...,
                std::forward<Self>(self).template arg<J>()[i]
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template subscript<J + 1>(
                i,
                std::forward<Ts>(args)...,
                std::forward<Self>(self).template arg<J>()[i]
            )};})
        {
            return (std::forward<Self>(self).template subscript<J + 1>(
                i,
                std::forward<Ts>(args)...,
                std::forward<Self>(self).template arg<J>()[i]
            ));
        }

        template <size_t J, typename Self, typename... Ts> requires (J == sizeof...(A))
        constexpr decltype(auto) subscript(this Self&& self, index_type i, Ts&&... args)
            noexcept (requires{
                {std::forward<Self>(self).func()(std::forward<Ts>(args)...)} noexcept;
            })
            requires (requires{
                {std::forward<Self>(self).func()(std::forward<Ts>(args)...)};
            })
        {
            return (std::forward<Self>(self).func()(std::forward<Ts>(args)...));
        }

    public:
        /* Access the `I`-th element of a tuple-like, zipped range, passing the
        unpacked arguments into the transformation function.  Non-range arguments will
        be forwarded according to the current cvref qualifications of the `zip` range,
        while range arguments will be accessed using the provided index before
        forwarding.  If the index is invalid for one or more of the input ranges, or
        the forwarded arguments are not valid inputs to the visitor function, then this
        method will fail to compile. */
        template <size_t I, typename Self> requires (tuple_like && I < zip_tuple_size<A...>)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).template _get<I, 0>()} noexcept;})
            requires (requires{{std::forward<Self>(self).template _get<I, 0>()};})
        {
            return (std::forward<Self>(self).template _get<I, 0>());
        }

        /* Index into the zipped range, passing the indexed arguments into the
        transformation function.  Non-range arguments will be forwarded according to
        the current cvref qualifications of the `zip` range, while range arguments will
        be accessed using the provided index before forwarding.  If the index is not
        supported for one or more of the input ranges, or the forwarded arguments are
        not valid inputs to the visitor function, then this method will fail to
        compile. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, size_type i)
            noexcept (requires{{std::forward<Self>(self).template subscript<0>(i)} noexcept;})
            requires (requires{{std::forward<Self>(self).template subscript<0>(i)};})
        {
            return (std::forward<Self>(self).template subscript<0>(i));
        }

        /* Get a forward iterator over the zipped range. */
        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{make_zip_iterator{*this}.begin()} noexcept;})
            requires (requires{{make_zip_iterator{*this}.begin()};})
        {
            return make_zip_iterator{*this}.begin();
        }

        /* Get a forward iterator over the zipped range. */
        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{make_zip_iterator{*this}.begin()} noexcept;})
            requires (requires{{make_zip_iterator{*this}.begin()};})
        {
            return make_zip_iterator{*this}.begin();
        }

        /* Get a forward sentinel one past the end of the zipped range. */
        [[nodiscard]] constexpr auto end()
            noexcept (requires{{make_zip_iterator{*this}.end()} noexcept;})
            requires (requires{{make_zip_iterator{*this}.end()};})
        {
            return make_zip_iterator{*this}.end();
        }

        /* Get a forward sentinel one past the end of the zipped range. */
        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{make_zip_iterator{*this}.end()} noexcept;})
            requires (requires{{make_zip_iterator{*this}.end()};})
        {
            return make_zip_iterator{*this}.end();
        }

        /* Get a reverse iterator over the zipped range. */
        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{make_zip_reversed{*this}.begin()} noexcept;})
            requires (requires{{make_zip_reversed{*this}.begin()};})
        {
            return make_zip_reversed{*this}.begin();
        }

        /* Get a reverse iterator over the zipped range. */
        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{make_zip_reversed{*this}.begin()} noexcept;})
            requires (requires{{make_zip_reversed{*this}.begin()};})
        {
            return make_zip_reversed{*this}.begin();
        }

        /* Get a reverse sentinel one before the beginning of the zipped range. */
        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{make_zip_reversed{*this}.end()} noexcept;})
            requires (requires{{make_zip_reversed{*this}.end()};})
        {
            return make_zip_reversed{*this}.end();
        }

        /* Get a reverse sentinel one before the beginning of the zipped range. */
        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{make_zip_reversed{*this}.end()} noexcept;})
            requires (requires{{make_zip_reversed{*this}.end()};})
        {
            return make_zip_reversed{*this}.end();
        }
    };

    /// TODO: fill in zip_tuple once `Tuple` is in scope

    /* If no transformation function is provided, then `zip{}` will default to
    returning each value as a `Tuple`, similar to `std::views::zip()` or `zip()` in
    Python. */
    struct zip_tuple {
        template <typename... A> requires (!meta::range<A> && ...)
        [[nodiscard]] constexpr auto operator()(A&&... args);
        //     noexcept (requires{{Tuple{std::forward<A>(args)...}} noexcept;})
        //     requires (requires{{Tuple{std::forward<A>(args)...}};})
        // {
        //     return Tuple{std::forward<A>(args)...};
        // }
    };

}


namespace iter {

    /* A function object that merges multiple ranges and/or scalar values into a single
    range, passing each element to a given transformation function.  If no transformation
    function is given, then the range defaults to returning a `Tuple` of the individual
    elements.

    This class unifies and replaces the following standard library views:

        1.  `std::views::zip` -> `zip{}(a...)`
        2.  `std::views::zip_transform` -> `zip{f}(a...)`
        3.  `std::views::transform` -> `zip{f}(r)`
        4.  `std::views::enumerate` -> `zip{f}(range(0, r.size()), r)`

    This class also serves as the basis for monadic operations on ranges, which differ only
    in the transformation function used to compute each element.  An expression such as
    `range(x) + 2` is therefore equivalent to:

        ```cpp
        auto add = []<typename L, typename R>(L&& l, R&& r) -> decltype(auto) {
            return (std::forward<L>(l) + std::forward<R>(r));
        };
        zip{add}(range(x), 2);
        ```

    Similar definitions exist for all overloadable operators, which act as simple
    elementwise transformations on the zipped range(s).

    Note that providing an unpacking operator (e.g. `*range(x)`) as an argument will
    trigger tuple decomposition for each element of the unpacked range, causing them to be
    passed as individual arguments to the transformation function.  For example:

        ```cpp
        auto f = [](int a, int b, int c) { return a + b + c; };

        Array x {Tuple{1, 2}, Tuple{3, 4}, Tuple{5, 6}};
        zip{f}(*range(x), 7);  // [10, 14, 18]
        ```

    If the unpacked range does not yield tuple-like elements, then the unpacking operator
    will be ignored.  Additionally, unpacking a non-range argument (i.e. `*x` instead of
    `*range(x)`) will decompose the argument before iterating, which may cause the contents
    to be broadcasted as scalars:

        ```cpp
        auto f = [](int a, int b, int c, int d) { return a + b + c + d; };

        Tuple x {1, 2, 3};
        zip{f}(*x, 4);  // [10]
        ```

    The additional unpacking behavior allows this class to also replace the following
    standard library views:

        1.  `std::views::elements`
        2.  `std::views::keys`
        3.  `std::views::values`

    ... Which all devolve to `zip{f}(*r)`, where `r` is a range yielding tuple-like
    elements, and `f` is a function that extracts the desired value(s). */
    template <meta::not_rvalue F = void>
    struct zip {
    private:
        template <typename... A>
        using container = impl::zip<F, meta::remove_rvalue<A>...>;

        template <typename... A>
        using range = iter::range<container<A...>>;

    public:
        [[no_unique_address]] F f;

        template <typename Self, typename... A> requires (impl::zip_concept<F, A...>)
        [[nodiscard]] constexpr auto operator()(this Self&& self, A&&... a)
            noexcept (requires{{range<A...>{container<A...>{
                .m_func = std::forward<Self>(self).f,
                .m_args = {std::forward<A>(a)...}
            }}} noexcept;})
            requires (requires{{range<A...>{container<A...>{
                .m_func = std::forward<Self>(self).f,
                .m_args = {std::forward<A>(a)...}
            }}};})
        {
            return range<A...>{container<A...>{
                .m_func = std::forward<Self>(self).f,
                .m_args = {std::forward<A>(a)...}
            }};
        }
    };

    /* A function object that merges multiple ranges and/or scalar values into a single
    range, passing each element to a given transformation function.  If no transformation
    function is given, then the range defaults to returning a `Tuple` of the individual
    elements.

    This class unifies and replaces the following standard library views:

        1.  `std::views::zip` -> `zip{}(a...)`
        2.  `std::views::zip_transform` -> `zip{f}(a...)`
        3.  `std::views::transform` -> `zip{f}(r)`
        4.  `std::views::enumerate` -> `zip{f}(range(0, r.size()), r)`

    This class also serves as the basis for monadic operations on ranges, which differ only
    in the transformation function used to compute each element.  An expression such as
    `range(x) + 2` is therefore equivalent to:

        ```cpp
        auto add = []<typename L, typename R>(L&& l, R&& r) -> decltype(auto) {
            return (std::forward<L>(l) + std::forward<R>(r));
        };
        zip{add}(range(x), 2);
        ```

    Similar definitions exist for all overloadable operators, which act as simple
    elementwise transformations on the zipped range(s).

    Note that providing an unpacking operator (e.g. `*range(x)`) as an argument will
    trigger tuple decomposition for each element of the unpacked range, causing them to be
    passed as individual arguments to the transformation function.  For example:

        ```cpp
        auto f = [](int a, int b, int c) { return a + b + c; };

        Array x {Tuple{1, 2}, Tuple{3, 4}, Tuple{5, 6}};
        zip{f}(*range(x), 7);  // [10, 14, 18]
        ```

    If the unpacked range does not yield tuple-like elements, then the unpacking operator
    will be ignored.  Additionally, unpacking a non-range argument (i.e. `*x` instead of
    `*range(x)`) will decompose the argument before iterating, which may cause the contents
    to be broadcasted as scalars:

        ```cpp
        auto f = [](int a, int b, int c, int d) { return a + b + c + d; };

        Tuple x {1, 2, 3};
        zip{f}(*x, 4);  // [10]
        ```

    The additional unpacking behavior allows this class to also replace the following
    standard library views:

        1.  `std::views::elements`
        2.  `std::views::keys`
        3.  `std::views::values`

    ... Which all devolve to `zip{f}(*r)`, where `r` is a range yielding tuple-like
    elements, and `f` is a function that extracts the desired value(s). */
    template <meta::is_void V> requires (meta::not_rvalue<V>)
    struct zip<V> {
    private:
        using F = impl::zip_tuple;

        template <typename... A>
        using container = impl::zip<meta::as_const_ref<F>, meta::remove_rvalue<A>...>;

        template <typename... A>
        using range = iter::range<container<A...>>;

    public:
        static constexpr F f;

        template <typename... A> requires (impl::zip_concept<const F&, A...>)
        [[nodiscard]] static constexpr auto operator()(A&&... a)
            noexcept (requires{{range<A...>{container<A...>{
                .m_func = f,
                .m_args = {std::forward<A>(a)...}
            }}} noexcept;})
            requires (requires{{range<A...>{container<A...>{
                .m_func = f,
                .m_args = {std::forward<A>(a)...}
            }}};})
        {
            return range<A...>{container<A...>{
                .m_func = f,
                .m_args = {std::forward<A>(a)...}
            }};
        }
    };

    template <typename F>
    zip(F&&) -> zip<meta::remove_rvalue<F>>;

}



// static constexpr std::array arr1 {1, 2, 3};
// static constexpr std::array arr2 {1, 2, 3, 4, 5};
// static constexpr auto z = iter::zip{
//     [](int x, int y) { return x + y;}
// }(iter::range(arr1), iter::range(arr2));
// static_assert([] {
//     auto r = iter::zip{[](int x, int y) {
//         return x + y;
//     }}(iter::range(arr1), iter::range(arr2));

//     if (r.size() != 3) return false;
//     if ((*r.__value)[1] != 4) return false;

//     for (auto&& x : z) {
//         if (x != 2 && x != 4 && x != 6) {
//             return false;
//         }
//     }
//     return true;
// }());


// static constexpr std::array arr3 {std::pair{1, 2}, std::pair{3, 4}};
// static constexpr auto r = iter::zip{[](int x, int y, int z, int w) {
//     return x + y;
// }}(*iter::range(arr3), iter::range(arr1), 2);
// static_assert([] {
//     if (r.size() != 2) return false;
//     if (r[0] != 3) return false;
//     if (r[1] != 7) return false;

//     for (auto&& x : r) {
//         if (x != 3 && x != 7) {
//             return false;
//         }
//     }

//     return true;
// }());

    
}


#endif  // BERTRAND_ITER_ZIP_H