#ifndef BERTRAND_ITER_RANGE_H
#define BERTRAND_ITER_RANGE_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/union.h"


namespace bertrand {


namespace impl {
    struct range_tag {};

    /* A trivial range with zero elements.  This is the default type for the `range`
    class template, allowing it to be default-constructed. */
    struct empty_range {
        [[nodiscard]] static constexpr size_t size() noexcept { return 0; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return 0; }
        [[nodiscard]] static constexpr bool empty() noexcept { return true; }
        [[nodiscard]] static constexpr auto begin() noexcept {
            return empty_iterator<const NoneType&>{};
        }
        [[nodiscard]] static constexpr auto end() noexcept {
            return empty_iterator<const NoneType&>{};
        }
        [[nodiscard]] static constexpr auto rbegin() noexcept {
            return std::make_reverse_iterator(end());
        }
        [[nodiscard]] static constexpr auto rend() noexcept {
            return std::make_reverse_iterator(begin());
        }
    };

    /* A range over just a single scalar element.  Indexing the range perfectly
    forwards that element, and iterating over it is akin to taking its address.  A
    CTAD guide chooses this type when a single element is passed to the `range()`
    constructor. */
    template <meta::not_rvalue T>
    struct single_range {
        [[no_unique_address]] impl::ref<T> __value;

        [[nodiscard]] constexpr single_range() = default;

        template <typename... A> requires (sizeof...(A) > 0)
        [[nodiscard]] constexpr single_range(A&&... args)
            noexcept (meta::nothrow::constructible_from<impl::ref<T>, A...>)
            requires (meta::constructible_from<impl::ref<T>, A...>)
        :
            __value{std::forward<A>(args)...}
        {}

        [[nodiscard]] static constexpr size_t size() noexcept { return 1; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return 1; }
        [[nodiscard]] static constexpr bool empty() noexcept { return false; }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{meta::to_arrow(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::to_arrow(*std::forward<Self>(self).__value)};})
        {
            return meta::to_arrow(*std::forward<Self>(self).__value);
        }

        template <size_t I, typename Self> requires (I == 0)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{
                {meta::unpack_tuple<I>(*std::forward<Self>(self).__value)} noexcept;
            })
            requires (requires{
                {meta::unpack_tuple<I>(*std::forward<Self>(self).__value)};
            })
        {
            return (meta::unpack_tuple<I>(*std::forward<Self>(self).__value));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, size_t i) noexcept {
            return (*std::forward<Self>(self).__value);
        }

        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{contiguous_iterator<meta::as_lvalue<T>>{
                std::addressof(*__value)
            }} noexcept;})
            requires (requires{{contiguous_iterator<meta::as_lvalue<T>>{
                std::addressof(*__value)
            }};})
        {
            return contiguous_iterator<meta::as_lvalue<T>>{std::addressof(*__value)};
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{
                {contiguous_iterator<meta::as_const_ref<T>>{std::addressof(*__value)}} noexcept;})
            requires (requires{
                {contiguous_iterator<meta::as_const_ref<T>>{std::addressof(*__value)}};
            })
        {
            return contiguous_iterator<meta::as_const_ref<T>>{std::addressof(*__value)};
        }

        [[nodiscard]] constexpr auto end()
            noexcept (requires{
                {contiguous_iterator<meta::as_lvalue<T>>{std::addressof(*__value) + 1}} noexcept;
            })
            requires (requires{
                {contiguous_iterator<meta::as_lvalue<T>>{std::addressof(*__value) + 1}};
            })
        {
            return contiguous_iterator<meta::as_lvalue<T>>{std::addressof(*__value) + 1};
        }

        [[nodiscard]] constexpr auto end() const
            noexcept (requires{
                {contiguous_iterator<meta::as_const_ref<T>>{std::addressof(*__value) + 1}} noexcept;
            })
            requires (requires{
                {contiguous_iterator<meta::as_const_ref<T>>{std::addressof(*__value) + 1}};
            })
        {
            return contiguous_iterator<meta::as_const_ref<T>>{std::addressof(*__value) + 1};
        }

        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{std::make_reverse_iterator(end())} noexcept;})
            requires (requires{{std::make_reverse_iterator(end())};})
        {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{std::make_reverse_iterator(end())} noexcept;})
            requires (requires{{std::make_reverse_iterator(end())};})
        {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{std::make_reverse_iterator(begin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin())};})
        {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{std::make_reverse_iterator(begin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin())};})
        {
            return std::make_reverse_iterator(end());
        }
    };
    template <typename T>
    single_range(T&&) -> single_range<meta::remove_rvalue<T>>;

    enum class tuple_kind : uint8_t {
        EMPTY,
        TRIVIAL,
        VTABLE,
    };

    /* A generic iterator over an arbitrary tuple type embedded in a `tuple_range<C>`
    wrapper.  The iterator works by traversing a separate array, which may either
    contain references to the tuple's elements if they all happen to be the same type,
    or a vtable of function pointers that yield each value dynamically. */
    template <meta::lvalue T>
    struct tuple_iterator {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = meta::remove_reference<decltype((std::declval<T>()[0]))>;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::address_type<reference>;

        meta::as_pointer<T> dispatch;
        difference_type index;

        [[nodiscard]] constexpr decltype(auto) operator*() const
            noexcept (requires{{(*dispatch)[size_t(index)]} noexcept;})
        {
            return ((*dispatch)[size_t(index)]);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow_proxy(**this)} noexcept;})
        {
            return impl::arrow_proxy(**this);
        }

        [[nodiscard]] constexpr decltype(auto) operator[](difference_type n) const
            noexcept (requires{{(*dispatch)[size_t(index + n)]} noexcept;})
        {
            return ((*dispatch)[size_t(index + n)]);
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
            return {self.dispatch, self.index + n};
        }

        [[nodiscard]] friend constexpr tuple_iterator operator+(
            difference_type n,
            const tuple_iterator& self
        ) noexcept {
            return {self.dispatch, self.index + n};
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
            return {dispatch, index - n};
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

    /* A unique vtable has to be emitted for each observed qualification of the tuple
    type, so as to perfectly forward the results. */
    template <meta::tuple_like C>
    struct tuple_vtable {
        using type = meta::tuple_types<C>::template eval<meta::union_type>;
        template <size_t I>
        struct fn {
            static constexpr type operator()(meta::forward<C> c)
                noexcept (requires{{
                    meta::unpack_tuple<I>(c)
                } noexcept -> meta::nothrow::convertible_to<type>;})
            {
                return meta::unpack_tuple<I>(c);
            }
        };
        using dispatch = impl::basic_vtable<fn, meta::tuple_size<C>>;
    };

    template <typename... Ts>
    struct _tuple_range {
        using type = meta::union_type<Ts...>;
        static constexpr tuple_kind kind =
            meta::trivial_union<Ts...> ? tuple_kind::TRIVIAL : tuple_kind::VTABLE;
    };
    template <>
    struct _tuple_range<> {
        using type = const NoneType&;
        static constexpr tuple_kind kind = tuple_kind::EMPTY;
    };

    /* A wrapper around a generic tuple type that allows it to be indexed and iterated
    over at runtime, by dispatching to a reference array or vtable.  If the tuple
    consists of multiple types, then the subscript and yield types will be promoted to
    unions of all the possible results. */
    template <meta::tuple_like C>
    struct tuple_range : meta::tuple_types<C>::template eval<_tuple_range> {
        [[no_unique_address]] impl::ref<C> __value;

        template <typename... A>
        [[nodiscard]] tuple_range(A&&... args)
            noexcept (meta::nothrow::constructible_from<impl::ref<C>, A...>)
            requires (meta::constructible_from<impl::ref<C>, A...>)
        :
            __value(std::forward<A>(args)...)
        {}

        constexpr void swap(tuple_range& other)
            noexcept (meta::nothrow::swappable<impl::ref<C>>)
            requires (meta::swappable<impl::ref<C>>)
        {
            std::ranges::swap(__value, other.__value);
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{meta::to_arrow(*__value)} noexcept;})
            requires (requires{{meta::to_arrow(*__value)};})
        {
            return meta::to_arrow(*__value);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{meta::to_arrow(*__value)} noexcept;})
            requires (requires{{meta::to_arrow(*__value)};})
        {
            return meta::to_arrow(*__value);
        }

        [[nodiscard]] static constexpr size_t size() noexcept { return 0; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return 0; }
        [[nodiscard]] static constexpr bool empty() noexcept { return true; }

        [[nodiscard]] static constexpr auto begin() noexcept {
            return empty_iterator<const NoneType&>{};
        }

        [[nodiscard]] static constexpr auto end() noexcept {
            return empty_iterator<const NoneType&>{};
        }

        [[nodiscard]] static constexpr auto rbegin() noexcept {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]] static constexpr auto rend() noexcept {
            return std::make_reverse_iterator(begin());
        }
    };
    template <meta::tuple_like C>
        requires (meta::tuple_types<C>::template eval<_tuple_range>::kind == tuple_kind::TRIVIAL)
    struct tuple_range<C> : meta::tuple_types<C>::template eval<_tuple_range> {
        [[no_unique_address]] impl::ref<C> __value;

        [[nodiscard]] static constexpr size_t size() noexcept { return meta::tuple_size<C>; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return ssize_t(size()); }
        [[nodiscard]] static constexpr bool empty() noexcept { return false; }

    private:
        using base = meta::tuple_types<C>::template eval<_tuple_range>;
        using ref = impl::ref<typename base::type>;
        using array = std::array<ref, size()>;

        array elements {};

        template <size_t... Is>
        constexpr array init(std::index_sequence<Is...>)
            noexcept ((requires{
                {meta::unpack_tuple<Is>(*__value)} noexcept -> meta::nothrow::convertible_to<ref>;
            } && ...))
        {
            return {meta::unpack_tuple<Is>(*__value)...};
        }

    public:
        [[nodiscard]] constexpr tuple_range() = default;

        template <typename... A> requires (sizeof...(A) > 0)
        [[nodiscard]] constexpr tuple_range(A&&... args)
            noexcept (meta::nothrow::constructible_from<impl::ref<C>, A...>)
            requires (meta::constructible_from<impl::ref<C>, A...>)
        :
            __value(std::forward<A>(args)...),
            elements(init(std::make_index_sequence<size()>{}))
        {}

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{meta::to_arrow(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::to_arrow(*std::forward<Self>(self).__value)};})
        {
            return meta::to_arrow(*std::forward<Self>(self).__value);
        }

        template <size_t I, typename Self>
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{
                {meta::unpack_tuple<I>(*std::forward<Self>(self).__value)} noexcept;
            })
            requires (requires{
                {meta::unpack_tuple<I>(*std::forward<Self>(self).__value)};
            })
        {
            return (meta::unpack_tuple<I>(*std::forward<Self>(self).__value));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, size_t i) noexcept {
            return (*std::forward<Self>(self).elements[i]);
        }

        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{tuple_iterator<tuple_range&>{this, 0}} noexcept;})
            requires (requires{{tuple_iterator<tuple_range&>{this, 0}};})
        {
            return tuple_iterator<tuple_range&>{this, 0};
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{tuple_iterator<const tuple_range&>{this, 0}} noexcept;})
            requires (requires{{tuple_iterator<const tuple_range&>{this, 0}};})
        {
            return tuple_iterator<const tuple_range&>{this, 0};
        }

        [[nodiscard]] constexpr auto end()
            noexcept (requires{{tuple_iterator<tuple_range&>{this, ssize()}} noexcept;})
            requires (requires{{tuple_iterator<tuple_range&>{this, ssize()}};})
        {
            return tuple_iterator<tuple_range&>{this, ssize()};
        }

        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{tuple_iterator<const tuple_range&>{this, ssize()}} noexcept;})
            requires (requires{{tuple_iterator<const tuple_range&>{this, ssize()}};})
        {
            return tuple_iterator<const tuple_range&>{this, ssize()};
        }

        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{std::make_reverse_iterator(end())} noexcept;})
            requires (requires{{std::make_reverse_iterator(end())};})
        {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{std::make_reverse_iterator(end())} noexcept;})
            requires (requires{{std::make_reverse_iterator(end())};})
        {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{std::make_reverse_iterator(begin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin())};})
        {
            return std::make_reverse_iterator(begin());
        }

        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{std::make_reverse_iterator(begin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin())};})
        {
            return std::make_reverse_iterator(begin());
        }
    };
    template <meta::tuple_like C>
        requires (meta::tuple_types<C>::template eval<_tuple_range>::kind == tuple_kind::VTABLE)
    struct tuple_range<C> : meta::tuple_types<C>::template eval<_tuple_range> {
        [[no_unique_address]] impl::ref<C> __value;

        [[nodiscard]] static constexpr size_t size() noexcept { return meta::tuple_size<C>; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return ssize_t(size()); }
        [[nodiscard]] static constexpr bool empty() noexcept { return false; }

    private:
        using base = meta::tuple_types<C>::template eval<_tuple_range>;

        template <typename Self>
        using dispatch = tuple_vtable<decltype((*std::declval<Self>().__value))>::dispatch;

    public:
        [[nodiscard]] constexpr tuple_range() = default;

        template <typename... A> requires (sizeof...(A) > 0)
        [[nodiscard]] constexpr tuple_range(A&&... args)
            noexcept (meta::nothrow::constructible_from<impl::ref<C>, A...>)
            requires (meta::constructible_from<impl::ref<C>, A...>)
        :
            __value(std::forward<A>(args)...)
        {}

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{meta::to_arrow(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::to_arrow(*std::forward<Self>(self).__value)};})
        {
            return meta::to_arrow(*std::forward<Self>(self).__value);
        }

        template <size_t I, typename Self>
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{
                {meta::unpack_tuple<I>(*std::forward<Self>(self).__value)} noexcept;
            })
            requires (requires{
                {meta::unpack_tuple<I>(*std::forward<Self>(self).__value)};
            })
        {
            return (meta::unpack_tuple<I>(*std::forward<Self>(self).__value));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, size_t i)
            noexcept (requires{{dispatch<Self>{i}(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{dispatch<Self>{i}(*std::forward<Self>(self).__value)};})
        {
            return (dispatch<Self>{i}(*std::forward<Self>(self).__value));
        }

        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{tuple_iterator<tuple_range&>{this, 0}} noexcept;})
            requires (requires{{tuple_iterator<tuple_range&>{this, 0}};})
        {
            return tuple_iterator<tuple_range&>{this, 0};
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{tuple_iterator<const tuple_range&>{this, 0}} noexcept;})
            requires (requires{{tuple_iterator<const tuple_range&>{this, 0}};})
        {
            return tuple_iterator<const tuple_range&>{this, 0};
        }

        [[nodiscard]] constexpr auto end()
            noexcept (requires{{tuple_iterator<tuple_range&>{this, ssize()}} noexcept;})
            requires (requires{{tuple_iterator<tuple_range&>{this, ssize()}};})
        {
            return tuple_iterator<tuple_range&>{this, ssize()};
        }

        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{tuple_iterator<const tuple_range&>{this, ssize()}} noexcept;})
            requires (requires{{tuple_iterator<const tuple_range&>{this, ssize()}};})
        {
            return tuple_iterator<const tuple_range&>{this, ssize()};
        }

        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{std::make_reverse_iterator(end())} noexcept;})
            requires (requires{{std::make_reverse_iterator(end())};})
        {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{std::make_reverse_iterator(end())} noexcept;})
            requires (requires{{std::make_reverse_iterator(end())};})
        {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{std::make_reverse_iterator(begin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin())};})
        {
            return std::make_reverse_iterator(begin());
        }

        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{std::make_reverse_iterator(begin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin())};})
        {
            return std::make_reverse_iterator(begin());
        }
    };
    template <typename T>
    tuple_range(T&&) -> tuple_range<meta::remove_rvalue<T>>;


    /// TODO: all the iota machinery goes up here and should get simplified as much as
    /// possible.  I then follow it with the counted case, and then finally the
    /// subrange case, all of which have their associated CTAD guides, and must be
    /// specialized to work with and without step sizes of several kinds.


    /// TODO: these concepts are going to need to be refactored to work with the
    /// iota/subrange/counted use cases, and apply `step` equally across all of them,
    /// which is a challenge.  I should probably address all the other issues first.

    template <typename T>
    concept strictly_positive =
        meta::unsigned_integer<T> ||
        !requires(meta::as_const_ref<T> t) {{t < 0} -> meta::explicitly_convertible_to<bool>;};

    template <typename Start, typename Stop, typename Step>
    concept iota_bounded = meta::lt_returns<bool, const Start&, const Stop&> && (
        meta::is_void<Step> ||
        strictly_positive<Step> ||
        meta::gt_returns<bool, const Start&, const Stop&>
    );

    template <typename Start, typename Stop, typename Step>
    concept iota_incrementable = (meta::is_void<Step> && meta::has_preincrement<Start&>) || (
        meta::not_void<Step> &&
        (meta::has_iadd<Start&, const Step&> || (
            meta::has_preincrement<Start&> &&
            (strictly_positive<Step> || meta::has_predecrement<Start&>)
        ))
    );

    template <typename Start, typename Stop, typename Step>
    concept iota_concept =
        meta::unqualified<Start> &&
        meta::unqualified<Stop> &&
        meta::copyable<Start> &&
        meta::copyable<Stop> &&
        (meta::is_void<Step> || (meta::unqualified<Step> && meta::copyable<Step>)) &&
        (iota_bounded<Start, Stop, Step> || meta::None<Stop>) &&
        iota_incrementable<Start, Stop, Step>;

    template <typename Start, typename Stop, typename Step> requires (iota_concept<Start, Stop, Step>)
    struct iota;

    /// TODO: counted, then subrange

}


namespace meta {

    // namespace detail {

    //     template <typename>
    //     constexpr bool unpack = false;
    //     template <typename C>
    //     constexpr bool unpack<impl::unpack<C>> = true;

    //     template <typename T, bool done, size_t I, typename... Rs>
    //     constexpr bool _unpack_convert = done;
    //     template <typename T, bool done, size_t I, typename R> requires (I < meta::tuple_size<T>)
    //     constexpr bool _unpack_convert<T, done, I, R> =
    //         meta::convertible_to<meta::get_type<T, I>, R> && _unpack_convert<T, true, I + 1, R>;
    //     template <typename T, bool done, size_t I, typename R, typename... Rs>
    //         requires (I < meta::tuple_size<T>)
    //     constexpr bool _unpack_convert<T, done, I, R, Rs...> =
    //         meta::convertible_to<meta::get_type<T, I>, R> && _unpack_convert<T, done, I + 1, Rs...>;
    //     template <typename T, typename... Rs>
    //     constexpr bool unpack_convert = _unpack_convert<T, false, 0, Rs...>;

    // }

    /// TODO: this idea is actually fantastic, and can be scaled to all other types
    /// as well.  The idea is that for every class, you would have a meta:: concept
    /// that takes the class as the first template argument, and then optionally
    /// takes any number of additional template arguments, which would mirror the
    /// exact signature of the class.

    template <typename T, typename R = void>
    concept range = inherits<T, impl::range_tag> && (is_void<R> || yields<T, R>);

    /// TODO: perhaps I should add `bidirectional_range`, `random_access_range`,
    /// `contiguous_range`, `output_range`, `common_range`, all possibly with an
    /// optional yield type.

    // template <typename T, typename R = void>
    // concept unpack = range<T, R> && detail::unpack<unqualify<T>>;

    // template <typename T, typename... Rs>
    // concept unpack_to = detail::unpack<unqualify<T>> && (
    //     (tuple_like<T> && tuple_size<T> == sizeof...(Rs) && detail::unpack_convert<T, Rs...>) ||
    //     (!tuple_like<T> && ... && yields<T, Rs>)
    // );

    namespace detail {

        template <meta::range T>
        constexpr bool prefer_constructor<T> = true;

    }

}


namespace iter {

    template <meta::not_rvalue C = impl::empty_range> requires (meta::iterable<C>)
    struct range;

    /// TODO: CTAD guides will need to be updated to account for all the iota/subrange/
    /// counted cases, with and without `step`, as well as the unary `range(value)`
    /// case, which is always devoted to converting `value` into an iterable range.
    /// This will have to be done in tandem with the concept refactors.

    template <typename C> requires (meta::iterable<C>)
    range(C&&) -> range<meta::remove_rvalue<C>>;

    template <typename C> requires (!meta::iterable<C> && meta::tuple_like<C>)
    range(C&&) -> range<impl::tuple_range<meta::remove_rvalue<C>>>;

    template <typename C> requires (!meta::iterable<C> && !meta::tuple_like<C>)
    range(C&&) -> range<impl::single_range<meta::remove_rvalue<C>>>;

    // template <typename Stop>
    //     requires (
    //         !meta::iterable<Stop> &&
    //         !meta::tuple_like<Stop> &&
    //         impl::iota_concept<Stop, Stop, void>
    //     )
    // range(Stop) -> range<impl::iota<Stop, Stop, void>>;

    template <meta::iterator Begin, meta::sentinel_for<Begin> End>
    range(Begin, End) -> range<std::ranges::subrange<Begin, End>>;

    template <typename Start, typename Stop>
        requires (
            (!meta::iterator<Start> || !meta::sentinel_for<Stop, Start>) &&
            impl::iota_concept<Start, Stop, void>
        )
    range(Start, Stop) -> range<impl::iota<Start, Stop, void>>;

    template <meta::iterator Begin, typename Count>
        requires (
            !meta::sentinel_for<Begin, Count> &&
            !impl::iota_concept<Begin, Count, void> &&
            meta::integer<Count>
        )
    range(Begin, Count) -> range<std::ranges::subrange<
        std::counted_iterator<Begin>,
        std::default_sentinel_t
    >>;

    template <typename Start, typename Stop, typename Step>
        requires (impl::iota_concept<Start, Stop, Step>)
    range(Start, Stop, Step) -> range<impl::iota<Start, Stop, Step>>;

}


namespace impl {

    /// TODO: unpack ranges should use a custom iterator type that behaves identically
    /// to a normal range iterator, but if the yield type is a range, and we're
    /// applying double unpacking, then the inner range should also be unpacked.  That
    /// may be slightly tricky to implement, but it's core to how unpacking should
    /// work in general, and ranges can't be solved until it's done.

    /* A trivial subclass of `range` that allows the range to be destructured when
    used as an argument to a Bertrand function. */
    template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
    struct unpack : iter::range<C> {
        [[nodiscard]] explicit constexpr unpack(meta::forward<C> c)
            noexcept (meta::nothrow::constructible_from<iter::range<C>, meta::forward<C>>)
            requires (meta::constructible_from<iter::range<C>, meta::forward<C>>)
        :
            iter::range<C>(std::forward<C>(c))
        {}
    };

    template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
    unpack(C&&) -> unpack<meta::remove_rvalue<C>>;






    /// TODO: review all the range_ and iota_ helpers to streamline them and make them
    /// more maintainable.

    template <typename>
    constexpr bool range_reverse_iterable = false;
    template <typename... A> requires ((!meta::range<A> || meta::reverse_iterable<A>) && ...)
    constexpr bool range_reverse_iterable<meta::pack<A...>> = true;

    /* An index sequence records the positions of the incoming ranges. */
    template <typename out, size_t, typename...>
    struct _range_indices { using type = out; };
    template <size_t... Is, size_t I, typename T, typename... Ts>
    struct _range_indices<std::index_sequence<Is...>, I, T, Ts...> :
        _range_indices<std::index_sequence<Is...>, I + 1, Ts...>
    {};
    template <size_t... Is, size_t I, meta::range T, typename... Ts>
    struct _range_indices<std::index_sequence<Is...>, I, T, Ts...> :
        _range_indices<std::index_sequence<Is..., I>, I + 1, Ts...>
    {};
    template <meta::not_rvalue... A>
    using range_indices = _range_indices<std::index_sequence<>, 0, A...>::type;

    /* Zip iterators preserve as much of the iterator interface as possible. */
    template <typename, meta::not_rvalue... Iters>
    struct range_category;
    template <size_t... Is, meta::not_rvalue... Iters>
    struct range_category<std::index_sequence<Is...>, Iters...> {
        using type = meta::common_type<
            meta::iterator_category<meta::unpack_type<Is, Iters...>>...
        >;
    };

    /* The difference type between zip iterators (if any) is the common type for all
    constituent ranges. */
    template <typename, meta::not_rvalue... Iters>
    struct range_difference;
    template <size_t... Is, meta::not_rvalue... Iters>
    struct range_difference<std::index_sequence<Is...>, Iters...> {
        using type = meta::common_type<
            meta::iterator_difference_type<meta::unpack_type<Is, Iters...>>...
        >;
    };

    /// TODO: enable_borrowed_range should always be enabled for iotas.

    /* Iota iterators will use the difference type between `stop` and `start` if
    available and integer-like.  Otherwise, if `stop` is `None`, and start has an
    integer difference type with respect to itself, then we use that type.  Lastly,
    we default to `std::ptrdiff_t`. */
    template <typename Start, typename Stop>
    struct iota_difference { using type = std::ptrdiff_t; };
    template <typename Start, typename Stop>
        requires (
            meta::has_sub<const Stop&, const Start&> &&
            meta::integer<meta::sub_type<const Stop&, const Start&>>
        )
    struct iota_difference<Start, Stop> {
        using type = meta::sub_type<const Stop&, const Start&>;
    };
    template <typename Start, meta::None Stop>
        requires (
            !meta::has_sub<const Stop&, const Start&> &&
            meta::has_sub<const Start&, const Start&> &&
            meta::integer<meta::sub_type<const Start&, const Start&>>
        )
    struct iota_difference<Start, Stop> {
        using type = meta::sub_type<const Start&, const Start&>;
    };

    /* Iota iterators default to modeling `std::input_iterator` only. */
    template <typename Start, typename Stop, typename Step>
    struct iota_category { using type = std::input_iterator_tag; };

    /* If `Start` is comparable with itself, then the iterator can be upgraded to model
    `std::forward_iterator`. */
    template <typename Start>
    concept iota_forward = requires(Start start) {
        { start == start } -> meta::convertible_to<bool>;
        { start != start } -> meta::convertible_to<bool>;
    };
    template <typename Start, typename Stop, typename Step>
        requires (iota_forward<Start>)
    struct iota_category<Start, Stop, Step> {
        using type = std::forward_iterator_tag;
    };

    /* If `Start` is also decrementable, then the iterator can be upgraded to model
    `std::bidirectional_iterator`. */
    template <typename Start>
    concept iota_bidirectional = iota_forward<Start> && requires(Start start) { --start; };
    template <typename Start, typename Stop, typename Step>
        requires (iota_bidirectional<Start>)
    struct iota_category<Start, Stop, Step> {
        using type = std::bidirectional_iterator_tag;
    };

    /* If `Start` also supports addition, subtraction, and ordered comparisons, then
    the iterator can be upgraded to model `std::random_access_iterator`. */
    template <typename Start, typename difference>
    concept iota_random_access = iota_bidirectional<Start> &&
        requires(Start start, Start& istart, difference n) {
            { start + n } -> meta::convertible_to<Start>;
            { start - n } -> meta::convertible_to<Start>;
            { istart += n } -> meta::convertible_to<Start&>;
            { istart -= n } -> meta::convertible_to<Start&>;
            { start < start } -> meta::convertible_to<bool>;
            { start <= start } -> meta::convertible_to<bool>;
            { start > start } -> meta::convertible_to<bool>;
            { start >= start } -> meta::convertible_to<bool>;
        };
    template <typename Start, typename Stop, typename Step>
        requires (iota_random_access<Start, typename iota_difference<Start, Stop>::type>)
    struct iota_category<Start, Stop, Step> {
        using type = std::random_access_iterator_tag;
    };

    /* The `->` operator for iota iterators will prefer to recursively call the same
    iterator on `start`. */
    template <typename T>
    struct iota_pointer { using type = meta::as_pointer<T>; };
    template <meta::has_arrow T>
    struct iota_pointer<T> { using type = meta::arrow_type<T>; };
    template <meta::has_address T> requires (!meta::has_arrow<T>)
    struct iota_pointer<T> { using type = meta::address_type<T>; }; 

    template <typename Start, typename Stop, typename Step>
    constexpr bool iota_empty(const Start& start, const Stop& stop, const Step& step)
        noexcept (requires{{!(start < stop)} noexcept -> meta::nothrow::convertible_to<bool>;} && (
            strictly_positive<Step> || (
            requires{
                {step < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {!(start > stop)} noexcept -> meta::nothrow::convertible_to<bool>;
            }
        )))
        requires (requires{{!(start < stop)} -> meta::convertible_to<bool>;} && (
            strictly_positive<Step> ||
            requires{{!(start > stop)} -> meta::convertible_to<bool>;}
        ))
    {
        if constexpr (!strictly_positive<Step>) {
            if (step < 0) {
                return !(start > stop);
            }
        }
        return !(start < stop);
    }

    /* Iota iterators attempt to model the most permissive iterator category possible,
    but only for the `begin()` iterator.  The `end()` iterator is usually just a
    trivial sentinel that triggers a `<`/`>` comparison between `start` and `stop` to
    sidestep perfect equality. */
    template <typename Start, typename Stop, typename Step>
        requires (iota_concept<Start, Stop, Step>)
    struct iota_iterator {
        using iterator_category = iota_category<Start, Stop, Step>::type;
        using difference_type = iota_difference<Start, Stop>::type;
        using value_type = Start;
        using reference = const Start&;
        using pointer = iota_pointer<reference>::type;

        [[no_unique_address]] Start start;
        [[no_unique_address]] Stop stop;
        [[no_unique_address]] Step step;

        [[nodiscard]] constexpr value_type operator[](difference_type n) const
            noexcept (requires{{start + step * n} noexcept -> meta::nothrow::convertible_to<Start>;})
            requires (requires{{start + step * n} -> meta::convertible_to<Start>;})
        {
            return start + step * n;
        }

        /// NOTE: we need 2 dereference operators in order to satisfy
        /// `std::random_access_iterator` in case `operator[]` is also defined, in
        /// which case the dereference operator must return a copy, not a reference.

        [[nodiscard]] constexpr value_type operator*() const
            noexcept (meta::nothrow::copyable<Start>)
            requires (requires(difference_type n) {{start + step * n} -> meta::convertible_to<Start>;})
        {
            return start;
        }

        [[nodiscard]] constexpr reference operator*() const noexcept
            requires (!requires(difference_type n) {{start + step * n} -> meta::convertible_to<Start>;})
        {
            return start;
        }

        [[nodiscard]] constexpr pointer operator->() const
            noexcept (requires{{meta::to_arrow(start)} noexcept;})
            requires (requires{{meta::to_arrow(start)};})
        {
            return meta::to_arrow(start);
        }

        constexpr iota_iterator& operator++()
            noexcept (meta::nothrow::has_iadd<Start&, const Step&> || (
                !meta::has_iadd<Start&, const Step&> &&
                meta::nothrow::default_constructible<meta::unqualify<Step>> &&
                requires(meta::unqualify<Step> s) {
                    {s < step} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                    {++s} noexcept;
                } && (
                    strictly_positive<Step> ||
                    requires(meta::unqualify<Step> s) {
                        {s > step} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                        {--s} noexcept;
                    }
                )
            ))
            requires (meta::has_iadd<Start&, const Step&> || (
                meta::default_constructible<meta::unqualify<Step>> &&
                requires(meta::unqualify<Step> s) {
                    {s < step} -> meta::explicitly_convertible_to<bool>;
                    {++s};
                } && (
                    strictly_positive<Step> ||
                    requires(meta::unqualify<Step> s) {
                        {s > step} -> meta::explicitly_convertible_to<bool>;
                        {--s};
                    }
                )
            ))
        {
            if constexpr (meta::has_iadd<Start&, const Step&>) {
                start += step;
            } else if constexpr (strictly_positive<Step>) {
                for (meta::unqualify<Step> s {}; s < step; ++s) {
                    ++start;
                }
            } else {
                if (step < 0) {
                    for (meta::unqualify<Step> s {}; s > step; --s) {
                        --start;
                    }
                } else {
                    for (meta::unqualify<Step> s {}; s < step; ++s) {
                        ++start;
                    }
                }
            }
            return *this;
        }

        [[nodiscard]] constexpr iota_iterator operator++(int)
            noexcept (
                meta::nothrow::copyable<iota_iterator> &&
                meta::nothrow::has_preincrement<iota_iterator>
            )
            requires (
                meta::copyable<iota_iterator> &&
                meta::has_preincrement<iota_iterator>
            )
        {
            iota_iterator temp = *this;
            ++*this;
            return temp;
        }

        [[nodiscard]] friend constexpr iota_iterator operator+(
            const iota_iterator& self,
            difference_type n
        )
            noexcept (requires{
                {iota_iterator{self.start + self.step * n, self.stop, self.step}} noexcept;
            })
            requires (requires{
                {iota_iterator{self.start + self.step * n, self.stop, self.step}};
            })
        {
            return {self.start + self.step * n, self.stop, self.step};
        }

        [[nodiscard]] friend constexpr iota_iterator operator+(
            difference_type n,
            const iota_iterator& self
        )
            noexcept (requires{
                {iota_iterator{self.start + self.step * n, self.stop, self.step}} noexcept;
            })
            requires (requires{
                {iota_iterator{self.start + self.step * n, self.stop, self.step}};
            })
        {
            return {self.start + self.step * n, self.stop, self.step};
        }

        constexpr iota_iterator& operator+=(difference_type n)
            noexcept (requires{{start += step * n} noexcept;})
            requires (requires{{start += step * n};})
        {
            start += step * n;
            return *this;
        }

        constexpr iota_iterator& operator--()
            noexcept (meta::nothrow::has_isub<Start&, const Step&> || (
                !meta::has_isub<Start&, const Step&> &&
                meta::nothrow::default_constructible<meta::unqualify<Step>> &&
                requires(meta::unqualify<Step> s) {
                    {s < step} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                    {--s} noexcept;
                } && (
                    strictly_positive<Step> ||
                    requires(meta::unqualify<Step> s) {
                        {s > step} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                        {++s} noexcept;
                    }
                )
            ))
            requires (meta::has_isub<Start&, const Step&> || (
                meta::default_constructible<meta::unqualify<Step>> &&
                requires(meta::unqualify<Step> s) {
                    {s < step} -> meta::explicitly_convertible_to<bool>;
                    {--s};
                } && (
                    strictly_positive<Step> ||
                    requires(meta::unqualify<Step> s) {
                        {s > step} -> meta::explicitly_convertible_to<bool>;
                        {++s};
                    }
                )
            ))
        {
            if constexpr (meta::has_isub<Start&, const Step&>) {
                start -= step;
            } else if constexpr (strictly_positive<Step>) {
                for (meta::unqualify<Step> s {}; s < step; ++s) {
                    --start;
                }
            } else {
                if (step < 0) {
                    for (meta::unqualify<Step> s {}; s > step; --s) {
                        ++start;
                    }
                } else {
                    for (meta::unqualify<Step> s {}; s < step; ++s) {
                        --start;
                    }
                }
            }
            return *this;
        }

        [[nodiscard]] constexpr iota_iterator operator--(int)
            noexcept (
                meta::nothrow::copyable<iota_iterator> &&
                meta::nothrow::has_predecrement<iota_iterator>
            )
            requires (
                meta::copyable<iota_iterator> &&
                meta::has_predecrement<iota_iterator>
            )
        {
            iota_iterator temp = *this;
            --*this;
            return temp;
        }

        [[nodiscard]] constexpr iota_iterator operator-(difference_type n) const
            noexcept (requires{{iota_iterator{start - step * n, stop, step}} noexcept;})
            requires (requires{{iota_iterator{start - step * n, stop, step}};})
        {
            return {start - step * n, stop, step};
        }

        [[nodiscard]] constexpr difference_type operator-(const iota_iterator& other) const
            noexcept (requires{{
                (start - other.start) / step
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (requires{{
                (start - other.start) / step
            } -> meta::convertible_to<difference_type>;})
        {
            return (start - other.start) / step;
        }

        constexpr iota_iterator& operator-=(difference_type n)
            noexcept (meta::nothrow::has_isub<Start&, difference_type>)
            requires (meta::has_isub<Start&, difference_type>)
        {
            start -= n;
            return *this;
        }

        [[nodiscard]] constexpr bool operator<(const iota_iterator& other) const
            noexcept (requires{
                {start < other.start} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{start < other.start} -> meta::convertible_to<bool>;})
        {
            return start < other.start;
        }

        [[nodiscard]] constexpr bool operator<=(const iota_iterator& other) const
            noexcept (requires{
                {start <= other.start} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{start <= other.start} -> meta::convertible_to<bool>;})
        {
            return start <= other.start;
        }

        [[nodiscard]] constexpr bool operator==(const iota_iterator& other) const
            noexcept (requires{
                {start == other.start} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{start == other.start} -> meta::convertible_to<bool>;})
        {
            return start == other.start;
        }

        [[nodiscard]] friend constexpr bool operator==(const iota_iterator& self, NoneType)
            noexcept (
                requires{{iota_empty(self.start, self.stop, self.step)} noexcept;} ||
                !requires{{iota_empty(self.start, self.stop, self.step)};}
            )
            requires (
                requires{{iota_empty(self.start, self.stop, self.step)};} ||
                meta::None<Step>
            )
        {
            if constexpr (requires{{iota_empty(self.start, self.stop, self.step)};}) {
                return iota_empty(self.start, self.stop, self.step);
            } else {
                return false;  // infinite range
            }
        }

        [[nodiscard]] friend constexpr bool operator==(NoneType, const iota_iterator& self)
            noexcept (
                requires{{iota_empty(self.start, self.stop, self.step)} noexcept;} ||
                !requires{{iota_empty(self.start, self.stop, self.step)};}
            )
            requires (
                requires{{iota_empty(self.start, self.stop, self.step)};} ||
                meta::None<Step>
            )
        {
            if constexpr (requires{{iota_empty(self.start, self.stop, self.step)};}) {
                return iota_empty(self.start, self.stop, self.step);
            } else {
                return false;  // infinite range
            }
        }

        [[nodiscard]] constexpr bool operator!=(const iota_iterator& other) const
            noexcept (requires{
                {start != other.start} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{start != other.start} -> meta::convertible_to<bool>;})
        {
            return start != other.start;
        }

        [[nodiscard]] friend constexpr bool operator!=(const iota_iterator& self, NoneType)
            noexcept (requires{{!(self == None)} noexcept;})
            requires (requires{{!(self == None)};})
        {
            return !(self == None);
        }

        [[nodiscard]] friend constexpr bool operator!=(NoneType, const iota_iterator& self)
            noexcept (requires{{!(None == self)} noexcept;})
            requires (requires{{!(None == self)};})
        {
            return !(None == self);
        }

        [[nodiscard]] constexpr bool operator>=(const iota_iterator& other) const
            noexcept (requires{
                {start >= other.start} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{start >= other.start} -> meta::convertible_to<bool>;})
        {
            return start >= other.start;
        }

        [[nodiscard]] constexpr bool operator>(const iota_iterator& other) const
            noexcept (requires{
                {start > other.start} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{start > other.start} -> meta::convertible_to<bool>;})
        {
            return start > other.start;
        }

        [[nodiscard]] constexpr auto operator<=>(const iota_iterator& other) const
            noexcept (requires{{start <=> other.start} noexcept;})
            requires (requires{{start <=> other.start};})
        {
            return start <=> other.start;
        }
    };

    /* Specialization for an iota iterator without a step size, which slightly
    optimizes the inner loop. */
    template <typename Start, typename Stop> requires (iota_concept<Start, Stop, void>)
    struct iota_iterator<Start, Stop, void> {
        using iterator_category = iota_category<Start, Stop, void>::type;
        using difference_type = iota_difference<Start, Stop>::type;
        using value_type = Start;
        using reference = const Start&;
        using pointer = iota_pointer<reference>::type;

        [[no_unique_address]] Start start;
        [[no_unique_address]] Stop stop;

        [[nodiscard]] constexpr value_type operator[](difference_type n) const
            noexcept (requires{{start + n} noexcept -> meta::nothrow::convertible_to<Start>;})
            requires (requires{{start + n} -> meta::convertible_to<Start>;})
        {
            return start + n;
        }

        /// NOTE: we need 2 dereference operators in order to satisfy
        /// `std::random_access_iterator` in case `operator[]` is also defined, in
        /// which case the dereference operator must return a copy, not a reference.

        [[nodiscard]] constexpr value_type operator*() const
            noexcept (meta::nothrow::copyable<Start>)
            requires (requires(difference_type n) {{start + n} -> meta::convertible_to<Start>;})
        {
            return start;
        }

        [[nodiscard]] constexpr reference operator*() const noexcept
            requires (!requires(difference_type n) {{start + n} -> meta::convertible_to<Start>;})
        {
            return start;
        }

        [[nodiscard]] constexpr pointer operator->() const
            noexcept (requires{{meta::to_arrow(start)} noexcept;})
            requires (requires{{meta::to_arrow(start)};})
        {
            return meta::to_arrow(start);
        }

        constexpr iota_iterator& operator++()
            noexcept (meta::nothrow::has_preincrement<Start&>)
            requires (meta::has_preincrement<Start&>)
        {
            ++start;
            return *this;
        }

        [[nodiscard]] constexpr iota_iterator operator++(int)
            noexcept (
                meta::nothrow::copyable<iota_iterator> &&
                meta::nothrow::has_preincrement<Start&>
            )
            requires (meta::has_preincrement<Start&>)
        {
            iota_iterator temp = *this;
            ++*this;
            return temp;
        }

        [[nodiscard]] friend constexpr iota_iterator operator+(
            const iota_iterator& self,
            difference_type n
        )
            noexcept (requires{{iota_iterator{self.start + n, self.stop}} noexcept;})
            requires (requires{{iota_iterator{self.start + n, self.stop}};})
        {
            return {self.start + n, self.stop};
        }

        [[nodiscard]] friend constexpr iota_iterator operator+(
            difference_type n,
            const iota_iterator& self
        )
            noexcept (requires{{iota_iterator{self.start + n, self.stop}} noexcept;})
            requires (requires{{iota_iterator{self.start + n, self.stop}};})
        {
            return {self.start + n, self.stop};
        }

        constexpr iota_iterator& operator+=(difference_type n)
            noexcept (requires{{start += n} noexcept;})
            requires (requires{{start += n};})
        {
            start += n;
            return *this;
        }

        constexpr iota_iterator& operator--()
            noexcept (meta::nothrow::has_predecrement<Start&>)
            requires (meta::has_predecrement<Start&>)
        {
            --start;
            return *this;
        }

        [[nodiscard]] constexpr iota_iterator operator--(int)
            noexcept (
                meta::nothrow::copyable<iota_iterator> &&
                meta::nothrow::has_predecrement<Start&>
            )
            requires (meta::has_predecrement<Start&>)
        {
            iota_iterator temp = *this;
            --*this;
            return temp;
        }

        [[nodiscard]] constexpr iota_iterator operator-(difference_type n) const
            noexcept (requires{{iota_iterator{start - n, stop}} noexcept;})
            requires (requires{{iota_iterator{start - n, stop}};})
        {
            return {start - n, stop};
        }

        [[nodiscard]] constexpr difference_type operator-(const iota_iterator& other) const
            noexcept (meta::nothrow::sub_returns<difference_type, const Start&, const Start&>)
            requires (meta::sub_returns<difference_type, const Start&, const Start&>)
        {
            return start - other.start;
        }

        constexpr iota_iterator& operator-=(difference_type n)
            noexcept (meta::nothrow::has_isub<Start&, difference_type>)
            requires (meta::has_isub<Start&, difference_type>)
        {
            start -= n;
            return *this;
        }

        [[nodiscard]] constexpr bool operator<(const iota_iterator& other) const
            noexcept (requires{
                {start < other.start} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{start < other.start} -> meta::convertible_to<bool>;})
        {
            return start < other.start;
        }

        [[nodiscard]] constexpr bool operator<=(const iota_iterator& other) const
            noexcept (requires{
                {start <= other.start} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{start <= other.start} -> meta::convertible_to<bool>;})
        {
            return start <= other.start;
        }

        [[nodiscard]] constexpr bool operator==(const iota_iterator& other) const
            noexcept (requires{
                {start == other.start} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{start == other.start} -> meta::convertible_to<bool>;})
        {
            return start == other.start;
        }

        [[nodiscard]] friend constexpr bool operator==(const iota_iterator& self, NoneType)
            noexcept (
                requires{{!(self.start < self.stop)} noexcept -> meta::nothrow::convertible_to<bool>;} ||
                !requires{{!(self.start < self.stop)} -> meta::convertible_to<bool>;}
            )
            requires (
                requires{{!(self.start < self.stop)} -> meta::convertible_to<bool>;} ||
                meta::None<Stop>
            )
        {
            if constexpr (requires{{!(self.start < self.stop)} -> meta::convertible_to<bool>;}) {
                return !(self.start < self.stop);
            } else {
                return false;  // infinite range
            }
        }

        [[nodiscard]] friend constexpr bool operator==(NoneType, const iota_iterator& self)
            noexcept (
                requires{{!(self.start < self.stop)} noexcept -> meta::nothrow::convertible_to<bool>;} ||
                !requires{{!(self.start < self.stop)} -> meta::convertible_to<bool>;}
            )
            requires (
                requires{{!(self.start < self.stop)} -> meta::convertible_to<bool>;} ||
                meta::None<Stop>
            )
        {
            if constexpr (requires{{!(self.start < self.stop)} -> meta::convertible_to<bool>;}) {
                return !(self.start < self.stop);
            } else {
                return false;  // infinite range
            }
        }

        [[nodiscard]] constexpr bool operator!=(const iota_iterator& other) const
            noexcept (requires{
                {start != other.start} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{start != other.start} -> meta::convertible_to<bool>;})
        {
            return start != other.start;
        }

        [[nodiscard]] friend constexpr bool operator!=(const iota_iterator& self, NoneType)
            noexcept (requires{{!(self == None)} noexcept;})
            requires (requires{{!(self == None)};})
        {
            return !(self == None);
        }

        [[nodiscard]] friend constexpr bool operator!=(NoneType, const iota_iterator& self)
            noexcept (requires{{!(None == self)} noexcept;})
            requires (requires{{!(None == self)};})
        {
            return !(None == self);
        }

        [[nodiscard]] constexpr bool operator>=(const iota_iterator& other) const
            noexcept (requires{
                {start >= other.start} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{start >= other.start} -> meta::convertible_to<bool>;})
        {
            return start >= other.start;
        }

        [[nodiscard]] constexpr bool operator>(const iota_iterator& other) const
            noexcept (requires{
                {start > other.start} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{start > other.start} -> meta::convertible_to<bool>;})
        {
            return start > other.start;
        }

        [[nodiscard]] constexpr auto operator<=>(const iota_iterator& other) const
            noexcept (requires{{start <=> other.start} noexcept;})
            requires (requires{{start <=> other.start};})
        {
            return start <=> other.start;
        }
    };

    /* A replacement for `std::ranges::iota_view` that allows for an arbitrary step
    size.  Can be used with any type, as long as the following are satisfied:

        1.  `start < stop` is a valid expression returning a contextual boolean, which
            determines the end of the range.  If a step size is given, then
            `start > stop` must also be valid.
        2.  Either `++start` or `start += step` are valid expressions, depending on
            whether a step size is given.
        3.  If `start` is omitted from the constructor, then it must be
            default-constructible.

    The resulting iota exposes `size()` and `ssize()` if `stop - start` or
    `(stop - start) / step` yields a value that can be casted to `size_t` and/or
    `ssize_t`, respectively.  `empty()` is always supported.

    If the `--start` is also valid, then the iterators over the iota will model
    `std::bidirectional_iterator`.  If `start` is totally ordered with respect to
    itself, and `start + step * i`, `start - step * i`, and their in-place equivalents
    are valid expressions, then the iterators will also model
    `std::random_access_iterator`.  Otherwise, they will only model
    `std::input_iterator` or `std::forward_iterator` if `start` is comparable with
    itself. */
    template <typename Start, typename Stop, typename Step>
        requires (iota_concept<Start, Stop, Step>)
    struct iota {
        using start_type = Start;
        using stop_type = Stop;
        using step_type = Step;
        using size_type = size_t;
        using index_type = ssize_t;
        using iterator = iota_iterator<Start, Stop, Step>;

        static constexpr bool bounded = impl::iota_bounded<Start, Stop, Step>;

        [[no_unique_address]] Start start;
        [[no_unique_address]] Stop stop;
        [[no_unique_address]] Step step;

        [[nodiscard]] constexpr iota()
            noexcept (
                meta::nothrow::default_constructible<Start> &&
                meta::nothrow::default_constructible<Stop> &&
                meta::nothrow::constructible_from<Step, int>
            )
            requires (
                meta::default_constructible<Start> &&
                meta::default_constructible<Stop> &&
                meta::constructible_from<Step, int>
            )
        :
            start(),
            stop(),
            step(1)
        {};

        [[nodiscard]] constexpr iota(Start start, Stop stop, Step step)
            noexcept (
                !DEBUG &&
                meta::nothrow::movable<Start> &&
                meta::nothrow::movable<Stop> &&
                meta::nothrow::movable<Step>
            )
        :
            start(std::move(start)),
            stop(std::move(stop)),
            step(std::move(step))
        {
            if constexpr (DEBUG && meta::eq_returns<bool, Step&, int>) {
                if (step == 0) {
                    throw ValueError("step size cannot be zero");
                }
            }
        }

        /* Swap the contents of two iotas. */
        constexpr void swap(iota& other)
            noexcept (
                meta::nothrow::swappable<Start> &&
                meta::nothrow::swappable<Stop> &&
                meta::nothrow::swappable<Step>
            )
            requires (
                meta::swappable<Start> &&
                meta::swappable<Stop> &&
                meta::swappable<Step>
            )
        {
            std::ranges::swap(start, other.start);
            std::ranges::swap(stop, other.stop);
            std::ranges::swap(step, other.step);
        }

        /* Return `true` if the iota contains no elements. */
        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{iota_empty(start, stop, step)} noexcept;})
            requires (requires{{iota_empty(start, stop, step)};})
        {
            return iota_empty(start, stop, step);
        }

        /* Attempt to get the size of the iota as an unsigned integer, assuming
        `(stop - start) / step` is a valid expression whose result can be explicitly
        converted to `size_type`. */
        [[nodiscard]] constexpr size_type size() const
            noexcept (requires{{static_cast<size_type>((stop - start) / step) * !empty()} noexcept;})
            requires (requires{{static_cast<size_type>((stop - start) / step) * !empty()};})
        {
            return static_cast<size_type>((stop - start) / step) * !empty();
        }

        /* Attempt to get the size of the iota as a signed integer, assuming
        `(stop - start) / step` is a valid expression whose result can be explicitly
        converted to `index_type`. */
        [[nodiscard]] constexpr index_type ssize() const
            noexcept (requires{{static_cast<index_type>((stop - start) / step) * !empty()} noexcept;})
            requires (requires{{static_cast<index_type>((stop - start) / step) * !empty()};})
        {
            return static_cast<index_type>((stop - start) / step) * !empty();
        }

        /* Get the value at index `i`, assuming both `ssize()` and `start + step * i`
        are valid expressions.  Applies Python-style wraparound for negative
        indices. */
        [[nodiscard]] constexpr decltype(auto) operator[](index_type i) const
            noexcept (requires{{start + step * impl::normalize_index(ssize(), i)} noexcept;})
            requires (requires{{start + step * impl::normalize_index(ssize(), i)};})
        {
            return (start + step * impl::normalize_index(ssize(), i));
        }

        /* Get the value at index `i` for an unsized iota, assuming `start + step * i`
        is a valid expression. */
        [[nodiscard]] constexpr decltype(auto) operator[](index_type i) const
            noexcept (requires{{start + step * i} noexcept;})
            requires (
                !requires{{start + step * impl::normalize_index(ssize(), i)};} &&
                requires{{start + step * i};}
            )
        {
            return (start + step * i);
        }

        /* Get an iterator to the start of the iota. */
        [[nodiscard]] constexpr iterator begin() const
            noexcept (meta::nothrow::constructible_from<
                iterator,
                const Start&,
                const Stop&,
                const Step&
            >)
        {
            return iterator{start, stop, step};
        }

        /* Get a sentinel for the end of the iota. */
        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }
    };

    /* A specialization of `iota` that lacks a step size.  This causes the iteration
    algorithm to use prefix `++` rather than `+= step` to get the next value, which
    allows us to ignore negative step sizes and increase performance. */
    template <typename Start, typename Stop> requires (iota_concept<Start, Stop, void>)
    struct iota<Start, Stop, void> {
        using start_type = Start;
        using stop_type = Stop;
        using step_type = void;
        using size_type = size_t;
        using index_type = ssize_t;
        using iterator = iota_iterator<Start, Stop, void>;

        static constexpr bool bounded = impl::iota_bounded<Start, Stop, void>;

        [[no_unique_address]] Start start;
        [[no_unique_address]] Stop stop;

        [[nodiscard]] constexpr iota() = default;

        /* Single-argument constructor, which default-constructs the start value.  If
        compiled in debug mode and `stop < start` is true, then an `AssertionError`
        will be thrown. */
        [[nodiscard]] constexpr iota(Stop stop)
            noexcept (
                meta::nothrow::default_constructible<Start> &&
                meta::nothrow::movable<Stop>
            )
            requires (meta::default_constructible<Start>)
        :
            start(),
            stop(std::move(stop))
        {}

        /* Two-argument constructor.  If compiled in debug mode and `stop < start` is
        true, then an `AssertionError` will be thrown. */
        [[nodiscard]] constexpr iota(Start start, Stop stop)
            noexcept (meta::nothrow::movable<Start> && meta::nothrow::movable<Stop>)
        :
            start(std::forward<Start>(start)),
            stop(std::forward<Stop>(stop))
        {}

        /* Swap the contents of two iotas. */
        constexpr void swap(iota& other)
            noexcept (meta::nothrow::swappable<Start> && meta::nothrow::swappable<Stop>)
            requires (meta::swappable<Start> && meta::swappable<Stop>)
        {
            std::ranges::swap(start, other.start);
            std::ranges::swap(stop, other.stop);
        }

        /* Return `true` if the iota contains no elements. */
        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{!(start < stop)} noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{!(start < stop)} -> meta::convertible_to<bool>;})
        {
            return !(start < stop);
        }

        /* Attempt to get the size of the iota as an unsigned integer, assuming
        `stop - start` is a valid expression whose result can be explicitly converted
        to `size_type`. */
        [[nodiscard]] constexpr size_type size() const
            noexcept (requires{{static_cast<size_type>(stop - start) * !empty()} noexcept;})
            requires (requires{{static_cast<size_type>(stop - start) * !empty()};})
        {
            return static_cast<size_type>(stop - start) * !empty();
        }

        /* Attempt to get the size of the iota as a signed integer, assuming
        `stop - start` is a valid expression whose result can be explicitly converted
        to `index_type`. */
        [[nodiscard]] constexpr index_type ssize() const
            noexcept (requires{{static_cast<index_type>(stop - start) * !empty()} noexcept;})
            requires (requires{{static_cast<index_type>(stop - start) * !empty()};})
        {
            return static_cast<index_type>(stop - start) * !empty();
        }

        /* Get the value at index `i`, assuming both `ssize()` and `start + i` are
        valid expressions.  Applies Python-style wraparound for negative indices. */
        [[nodiscard]] constexpr decltype(auto) operator[](index_type i) const
            noexcept (requires{{start + impl::normalize_index(ssize(), i)} noexcept;})
            requires (requires{{start + impl::normalize_index(ssize(), i)};})
        {
            return (start + impl::normalize_index(ssize(), i));
        }

        /* Get the value at index `i` for an unsized iota, assuming `start + i` is a
        valid expression. */
        [[nodiscard]] constexpr decltype(auto) operator[](index_type i) const
            noexcept (requires{{start + i} noexcept;})
            requires (
                !requires{{start + impl::normalize_index(ssize(), i)};} &&
                requires{{start + i};}
            )
        {
            return (start + i);
        }

        /* Get an iterator to the start of the iota. */
        [[nodiscard]] constexpr iterator begin() const
            noexcept (meta::nothrow::constructible_from<iterator, const Start&, const Stop&>)
        {
            return iterator{start, stop};
        }

        /* Get a sentinel for the end of the iota. */
        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }
    };

    template <typename Stop> requires (iota_concept<Stop, Stop, void>)
    iota(Stop) -> iota<Stop, Stop, void>;

    template <typename Start, typename Stop> requires (iota_concept<Start, Stop, void>)
    iota(Start, Stop) -> iota<Start, Stop, void>;

    template <typename Start, typename Stop, typename Step>
        requires (iota_concept<Start, Stop, Step>)
    iota(Start, Stop, Step) -> iota<Start, Stop, Step>;

    /// TODO: range_get() and range_subscript() may need to account for single-element
    /// ranges?

    template <ssize_t I, typename C>
    constexpr decltype(auto) range_get(C&& c)
        noexcept (requires{{meta::unpack_tuple<I>(std::forward<C>(c))} noexcept;})
        requires (requires{{meta::unpack_tuple<I>(std::forward<C>(c))};})
    {
        return (meta::unpack_tuple<I>(std::forward<C>(c)));
    }

    template <ssize_t I, ssize_t... Is, typename C> requires (sizeof...(Is) > 0)
    constexpr decltype(auto) range_get(C&& c)
        noexcept (requires{{range_get<Is...>(range_get<I>(std::forward<C>(c)))} noexcept;})
        requires (requires{{range_get<Is...>(range_get<I>(std::forward<C>(c)))};})
    {
        return (range_get<Is...>(range_get<I>(std::forward<C>(c))));
    }

    template <typename C>
    constexpr decltype(auto) range_subscript(C&& c, ssize_t i)
        noexcept (requires{
            {std::forward<C>(c)[size_t(impl::normalize_index(std::ranges::ssize(c), i))]} noexcept;
        } || meta::tuple_like<C>)
        requires (requires{
            {std::forward<C>(c)[size_t(impl::normalize_index(std::ranges::ssize(c), i))]};
        } || meta::tuple_like<C>)
    {
        if constexpr (requires{{std::forward<C>(c)[i]};}) {
            return (std::forward<C>(c)[size_t(impl::normalize_index(std::ranges::ssize(c), i))]);
        } else {
            return (impl::tuple_range{std::forward<C>(c)}[
                size_t(impl::normalize_index(meta::tuple_size<C>, i))
            ]);
        }
    }

    template <typename C, meta::convertible_to<ssize_t>... Is> requires (sizeof...(Is) > 0)
    constexpr decltype(auto) range_subscript(C&& container, ssize_t i, Is... is)
        noexcept (requires{
            {range_subscript(range_subscript(std::forward<C>(container), i), is...)} noexcept;
        })
        requires (requires{
            {range_subscript(range_subscript(std::forward<C>(container), i), is...)};
        })
    {
        return (range_subscript(range_subscript(std::forward<C>(container), i), is...));
    }

    template <typename to, meta::tuple_like C, size_t... Is>
    constexpr to range_tuple_conversion(C&& container, std::index_sequence<Is...>)
        noexcept (requires{{to{meta::unpack_tuple<Is>(std::forward<C>(container))...}} noexcept;})
        requires (requires{{to{meta::unpack_tuple<Is>(std::forward<C>(container))...}};})
    {
        return to{meta::unpack_tuple<Is>(std::forward<C>(container))...};
    }

    template <typename C, typename T, size_t... Is>
    constexpr void range_tuple_assignment(C& container, T&& r, std::index_sequence<Is...>)
        noexcept (requires{{
            ((meta::unpack_tuple<Is>(container) = meta::unpack_tuple<Is>(std::forward<T>(r))), ...)
        } noexcept;})
        requires (requires{{
            ((meta::unpack_tuple<Is>(container) = meta::unpack_tuple<Is>(std::forward<T>(r))), ...)
        };})
    {
        ((meta::unpack_tuple<Is>(container) = meta::unpack_tuple<Is>(std::forward<T>(r))), ...);
    }

    template <typename L, typename R>
    consteval ValueError range_size_mismatch() noexcept {
        static constexpr static_str msg =
            "Size mismatch during range assignment: " + demangle<L>() + " = " + demangle<R>();
        return ValueError(msg);
    }

}


namespace iter {

    /* A wrapper for an arbitrary container type that can be used to form iterable
    expressions.

    Ranges can be constructed in a variety of ways, effectively replacing each of the
    following with a unified CTAD constructor (in order of preference):

        1.  `std::views::all(container)` -> `range(container)`, where `container` is any
            iterable or tuple-like type.
        2.  `std::ranges::subrange(begin, end)` -> `range(begin, end)`, where `begin` is
            an iterator and `end` is a matching sentinel.
        3.  `std::views::iota(start, stop)` -> `range(start, stop)`, where `start` and
            `stop` are arbitrary types for which `++start` and `start < stop` are
            well-formed.  Also permits an optional third `step` argument, which represents
            a step size that will be used between increments.  If the expression
            `start += step` is well-formed, then it will be used to obtain each value in
            constant time.  Otherwise, the step size must be default-constructible and
            support `step < step`, causing an inner loop to call either `++start` or
            `--start` to obtain each value in linear time, depending on the sign of `step`.
        4.  `std::views::iota(Stop{}, stop)` -> `range(stop)`, which corresponds to a
            Python-style `range` expression, where the start index is default-constructed
            with the same type as `stop`.  All other rules from (3) still apply, except
            that no step size is permitted (use the 2-argument form if a step size is
            needed).
        5.  `std::views::iota(start)` -> `range(start, None)`, representing an infinite
            range beginning at `start` and applying `++start` at every iteration.  A step
            size can be provided as an optional third argument, according to the rules laid
            out in (3).
        6.  `std::views::counted(begin, size)` -> `range(begin, size)`, where `begin` is
            an iterator and `size` is an unsigned integer.

    If the underlying container is tuple-like, then the range will be as well, and will
    forward to the container's `get<I>()` method when accessed or destructured.  If the
    tuple is not directly iterable, then an iterator will be generated for it, which may
    yield `Union<Ts...>`, where `Ts...` are the unique return types for each index.  All
    tuples, regardless of implementation, should therefore produce iterable ranges just
    like any other container.

    `range` has a number of subclasses, all of which extend the basic iteration interface
    in some way:

        1.  `unpack`: a trivial extension of `range` that behaves identically, and is
            returned by the prefix `*` operator for iterable and tuple-like containers.
            This is essentially equivalent to the standard `range` constructor; the only
            difference is that when an `unpack` range is provided to a Bertrand function
            (e.g. a `def` statement), it will be destructured into individual arguments,
            emulating Python-style container unpacking.  Applying a second prefix `*`
            promotes the `unpack` range into a keyword range, which destructures to
            keyword arguments if the function supports them.  Otherwise, when used in any
            context other than function calls, the prefix `*` operator simply provides a
            convenient entry point for range-based expressions over supported container
            types, separate from the `range` constructor.
        2.  `slice`: an extension of `range` that includes only a subset of the elements in
            a range, according to Python-style slicing semantics.  This can fully replace
            `std::views::take`, `std::views::drop`, and `std::views::stride`, as well as
            some uses of `std::views::reverse`, which can be implemented as a `slice` with
            a negative step size.  Note that `slice` cannot be tuple-like, since the
            included indices are only known at run time.
        3.  `mask`: an extension of `range` that includes only the elements that correspond
            to the `true` indices in a boolean mask.  This provides a finer level of
            control than `slice`, and can replace many uses of `std::views::filter` when a
            boolean mask is already available or can be easily generated.
        4.  `comprehension`: an extension of `range` that stores a function that will be
            applied elementwise over each value in the range.  This is similar to
            `std::views::transform`, but allows for more complex transformations, including
            destructuring and visitation for union and tuple elements consistent with the
            rest of Bertrand's pattern matching interface.  `comprehension`s also serve as
            the basic building blocks for arbitrary expression trees, and can replace
            `std::views::repeat` and any uses of `std::views::filter` that do not fall
            under the `mask` criteria simply by returning a nested `range`, which will be
            flattened into the result.
        5.  `zip`: an extension of `range` that fuses multiple ranges, and yields tuples of
            the corresponding elements, terminating when the shortest range has been
            exhausted.  This effectively replaces `std::views::zip` and
            `std::views::enumerate`.

        /// TODO: update these with the rest of the range interface when implemented

    Each subclass of `range` is also exposed to Bertrand's monadic operator interface,
    which returns lazily-evaluated `comprehension`s that encode each operation into an
    expression tree.  The tree will only be evaluated when the range is indexed, iterated
    over, or converted to a compatible type, which reduces it to a single loop that can be
    aggressively optimized by the compiler. */
    template <meta::not_rvalue C> requires (meta::iterable<C>)
    struct range : impl::range_tag {
        /// TODO: no need for `__type`: just infer from `decltype(__value)` like I do
        /// for unions, in order to prevent name conflicts.
        using __type = meta::remove_rvalue<C>;

        [[no_unique_address]] impl::ref<__type> __value;

        [[nodiscard]] constexpr range() = default;

        template <typename... A> requires (sizeof...(A) > 0)
        [[nodiscard]] constexpr range(A&&... args)
            noexcept (meta::nothrow::constructible_from<impl::ref<__type>, A...>)
            requires (meta::constructible_from<impl::ref<__type>, A...>)
        :
            __value(std::forward<A>(args)...)
        {}

        /// TODO: all of the constructors need to be reviewed, and changes would be
        /// coupled with the impl:: concept refactor and CTAD guides.

        // /* Forwarding constructor for the underlying container. */
        // [[nodiscard]] explicit constexpr range(meta::forward<C> c)
        //     noexcept (meta::nothrow::constructible_from<__type, meta::forward<C>>)
        //     requires (meta::constructible_from<__type, meta::forward<C>>)
        // :
        //     __value(std::forward<C>(c))
        // {}

        // /* CTAD constructor for 1-argument iota ranges. */
        // template <typename Stop>
        //     requires (
        //         !meta::constructible_from<__type, meta::forward<C>> &&
        //         impl::iota_concept<Stop, Stop, void>
        //     )
        // [[nodiscard]] explicit constexpr range(Stop stop)
        //     noexcept (meta::nothrow::constructible_from<__type, Stop, Stop>)
        //     requires (meta::constructible_from<__type, Stop, Stop>)
        // :
        //     __value(Stop(0), stop)
        // {}

        // /* CTAD constructor for iterator pair subranges. */
        // template <meta::iterator Begin, meta::sentinel_for<Begin> End>
        // [[nodiscard]] explicit constexpr range(Begin&& begin, End&& end)
        //     noexcept (meta::nothrow::constructible_from<__type, Begin, End>)
        //     requires (meta::constructible_from<__type, Begin, End>)
        // :
        //     __value(std::forward<Begin>(begin), std::forward<End>(end))
        // {}

        // /* CTAD constructor for 2-argument iota ranges. */
        // template <typename Start, typename Stop>
        //     requires (
        //         (!meta::iterator<Start> || !meta::sentinel_for<Stop, Start>) &&
        //         impl::iota_concept<Start, Stop, void>
        //     )
        // [[nodiscard]] explicit constexpr range(Start start, Stop stop)
        //     noexcept (meta::nothrow::constructible_from<__type, Start, Stop>)
        //     requires (meta::constructible_from<__type, Start, Stop>)
        // :
        //     __value(start, stop)
        // {}

        // /* CTAD constructor for counted ranges, which consist of a begin iterator and an
        // integer size. */
        // template <meta::iterator Begin, typename Count>
        //     requires (
        //         !meta::sentinel_for<Begin, Count> &&
        //         !impl::iota_concept<Begin, Count, void> &&
        //         meta::unsigned_integer<Count>
        //     )
        // [[nodiscard]] constexpr range(Begin begin, Count size)
        //     noexcept (meta::nothrow::constructible_from<
        //         __type,
        //         std::counted_iterator<Begin>,
        //         const std::default_sentinel_t&
        //     >)
        //     requires (meta::constructible_from<
        //         __type,
        //         std::counted_iterator<Begin>,
        //         const std::default_sentinel_t&
        //     >)
        // :
        //     __value(
        //         std::counted_iterator(std::move(begin), std::forward<Count>(size)),
        //         std::default_sentinel
        //     )
        // {}

        // /* CTAD constructor for 3-argument iota ranges. */
        // template <typename Start, typename Stop, typename Step>
        //     requires (impl::iota_concept<Start, Stop, Step>)
        // [[nodiscard]] explicit constexpr range(Start start, Stop stop, Step step)
        //     noexcept (meta::nothrow::constructible_from<__type, Start, Stop, Step>)
        //     requires (meta::constructible_from<__type, Start, Stop, Step>)
        // :
        //     __value(start, stop, step)
        // {}

        [[nodiscard]] constexpr range(const range&) = default;
        [[nodiscard]] constexpr range(range&&) = default;
        constexpr range& operator=(const range&) = default;
        constexpr range& operator=(range&&) = default;

        /* `swap()` operator between ranges. */
        constexpr void swap(range& other)
            noexcept (meta::nothrow::swappable<impl::ref<__type>>)
            requires (meta::swappable<impl::ref<__type>>)
        {
            std::ranges::swap(__value, other.__value);
        }

        /// TODO: the dereference operator is bound up in the `unpack` refactor, so
        /// that needs to be finished before this can be finalized.

        /* Dereferencing a range promotes it into a trivial `unpack` subclass, which allows
        it to be destructured when used as an argument to a Bertrand function. */
        template <typename Self>
        [[nodiscard]] constexpr auto operator*(this Self&& self)
            noexcept (requires{{impl::unpack{std::forward<Self>(self)}} noexcept;})
            requires (requires{{impl::unpack{std::forward<Self>(self)}};})
        {
            return impl::unpack{std::forward<Self>(self)};
        }

        /* Indirectly access a member of the wrapped container. */
        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{meta::to_arrow(*__value)} noexcept;})
            requires (requires{{meta::to_arrow(*__value)};})
        {
            return meta::to_arrow(*__value);
        }

        /* Indirectly access a member of the wrapped container. */
        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{meta::to_arrow(*__value)} noexcept;})
            requires (requires{{meta::to_arrow(*__value)};})
        {
            return meta::to_arrow(*__value);
        }

        /// TODO: .size(), .data(), indexing, etc. must account for single-element ranges.
        /// -> This may need to wait until the refactor for single-element ranges in
        /// general, since they may require different handling, or use a centralized
        /// flag to control their state.

        /* Get a pointer to the underlying data array, if one exists.  This is
        identical to a `std::ranges::data()` call on the underlying value, assuming
        that expression is well-formed. */
        [[nodiscard]] constexpr auto data()
            noexcept (requires{{std::ranges::data(*__value)} noexcept;})
            requires (requires{{std::ranges::data(*__value)};})
        {
            return std::ranges::data(*__value);
        }

        /* Get a pointer to the underlying data array, if one exists.  This is
        identical to a `std::ranges::data()` call on the underlying value, assuming
        that expression is well-formed. */
        [[nodiscard]] constexpr auto data() const
            noexcept (requires{{std::ranges::data(*__value)} noexcept;})
            requires (requires{{std::ranges::data(*__value)};})
        {
            return std::ranges::data(*__value);
        }

        /* Get a pointer to the underlying data array, if one exists.  This is
        identical to a `std::ranges::cdata()` call on the underlying value, assuming
        that expression is well-formed. */
        [[nodiscard]] constexpr auto cdata() const
            noexcept (requires{{std::ranges::cdata(*__value)} noexcept;})
            requires (requires{{std::ranges::cdata(*__value)};})
        {
            return std::ranges::cdata(*__value);
        }

        /* Check whether the underlying container has a definite size, which can be
        determined ahead of time.  This is provided to account for `sequence<T>` types,
        which may or may not be sized, but cannot determine that status at compile time
        due to type erasure.  For most other container types, this is identical to a
        `meta::has_size<T>` SFINAE check. */
        [[nodiscard]] static constexpr bool has_size()
            noexcept (requires{
                {meta::unqualify<C>::has_size()} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {meta::unqualify<C>::has_size()} -> meta::convertible_to<bool>;
            })
        {
            return meta::unqualify<C>::has_size();
        }

        /* Check whether the underlying container has a definite size, which can be
        determined ahead of time.  This is provided to account for `sequence<T>` types,
        which may or may not be sized, but cannot determine that status at compile time
        due to type erasure.  For most other container types, this is identical to a
        `meta::has_size<T>` SFINAE check. */
        [[nodiscard]] constexpr bool has_size() const
            noexcept (requires{
                {__value->has_size()} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (
                !requires{{meta::unqualify<C>::has_size()} -> meta::convertible_to<bool>;} &&
                requires{{__value->has_size()} -> meta::convertible_to<bool>;}
            )
        {
            return __value->has_size();
        }

        /* Forwarding `size()` operator for the underlying container, provided the
        container supports it. */
        [[nodiscard]] constexpr auto size() const
            noexcept (meta::nothrow::has_size<C> || meta::tuple_like<C>)
            requires (meta::has_size<C> || meta::tuple_like<C>)
        {
            if constexpr (meta::has_size<C>) {
                return std::ranges::size(*__value);
            } else {
                return meta::tuple_size<C>;
            }
        }

        /* Forwarding `ssize()` operator for the underlying container, provided the
        container supports it. */
        [[nodiscard]] constexpr auto ssize() const
            noexcept (meta::nothrow::has_ssize<C> || meta::tuple_like<C>)
            requires (meta::has_ssize<C> || meta::tuple_like<C>)
        {
            if constexpr (meta::has_ssize<C>) {
                return std::ranges::ssize(*__value);
            } else {
                return meta::to_signed(meta::tuple_size<C>);
            }
        }

        /* Forwarding `empty()` operator for the underlying container, provided the
        container supports it. */
        [[nodiscard]] constexpr bool empty() const
            noexcept (meta::nothrow::has_empty<C> || meta::tuple_like<C>)
            requires (meta::has_empty<C> || meta::tuple_like<C>)
        {
            if constexpr (meta::has_empty<C>) {
                return std::ranges::empty(*__value);
            } else {
                return meta::tuple_size<C> == 0;
            }
        }

        /* Forwarding `get<I>()` accessor, provided the underlying container is
        tuple-like.  Automatically applies Python-style wraparound for negative
        indices, and allows multidimensional indexing if the container is a tuple of
        tuples. */
        template <ssize_t... Is, typename Self> requires (sizeof...(Is) > 0)
        constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{
                {impl::range_get<Is...>(*std::forward<Self>(self).__value)} noexcept;
            })
            requires (requires{
                {impl::range_get<Is...>(*std::forward<Self>(self).__value)};
            })
        {
            return (impl::range_get<Is...>(*std::forward<Self>(self).__value));
        }

        /* Integer indexing operator.  Accepts one or more signed integers and
        retrieves the corresponding element from the underlying container after
        applying Python-style wraparound for negative indices, which converts the index
        to an unsigned integer.  If multiple indices are given, then each successive
        index after the first will be used to subscript the previous result.  If the
        container does not support indexing, but is otherwise tuple-like, then a vtable
        will be synthesized to back this operator. */
        template <typename Self, meta::convertible_to<ssize_t>... Is> requires (sizeof...(Is) > 0)
        constexpr decltype(auto) operator[](this Self&& self, Is&&... is)
            noexcept (requires{{impl::range_subscript(
                *std::forward<Self>(self).__value,
                std::forward<Is>(is)...
            )} noexcept;})
            requires (requires{{impl::range_subscript(
                *std::forward<Self>(self).__value,
                std::forward<Is>(is)...
            )};})
        {
            return (impl::range_subscript(
                *std::forward<Self>(self).__value,
                std::forward<Is>(is)...
            ));
        }

        /* Get a forward iterator to the start of the range. */
        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{std::ranges::begin(*__value)} noexcept;})
            requires (requires{{std::ranges::begin(*__value)};})
        {
            return std::ranges::begin(*__value);
        }

        /* Get a forward iterator to the start of the range. */
        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{std::ranges::begin(*__value)} noexcept;})
            requires (requires{{std::ranges::begin(*__value)};})
        {
            return std::ranges::begin(*__value);
        }

        /* Get a forward iterator to the start of the range. */
        [[nodiscard]] constexpr auto cbegin() const
            noexcept (requires{{begin()} noexcept;})
            requires (requires{{begin()};})
        {
            return begin();
        }

        /* Get a forward iterator to one past the last element of the range. */
        [[nodiscard]] constexpr auto end()
            noexcept (requires{{std::ranges::end(*__value)} noexcept;})
            requires (requires{{std::ranges::end(*__value)};})
        {
            return std::ranges::end(*__value);
        }

        /* Get a forward iterator to one past the last element of the range. */
        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{std::ranges::end(*__value)} noexcept;})
            requires (requires{{std::ranges::end(*__value)};})
        {
            return std::ranges::end(*__value);
        }

        /* Get a forward iterator to one past the last element of the range. */
        [[nodiscard]] constexpr auto cend() const
            noexcept (requires{{end()} noexcept;})
            requires (requires{{end()};})
        {
            return end();
        }

        /* Get a reverse iterator to the last element of the range. */
        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{std::ranges::rbegin(*__value)} noexcept;})
            requires (requires{{std::ranges::rbegin(*__value)};})
        {
            return std::ranges::rbegin(*__value);
        }

        /* Get a reverse iterator to the last element of the range. */
        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{std::ranges::rbegin(*__value)} noexcept;})
            requires (requires{{std::ranges::rbegin(*__value)};})
        {
            return std::ranges::rbegin(*__value);
        }

        /* Get a reverse iterator to the last element of the range. */
        [[nodiscard]] constexpr auto crbegin() const
            noexcept (requires{{rbegin()} noexcept;}
            )
            requires (requires{{rbegin()};})
        {
            return rbegin();
        }

        /* Get a reverse iterator to one before the first element of the range. */
        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{std::ranges::rend(*__value)} noexcept;})
            requires (requires{{std::ranges::rend(*__value)};})
        {
            return std::ranges::rend(*__value);
        }

        /* Get a reverse iterator to one before the first element of the range. */
        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{std::ranges::rend(*__value)} noexcept;})
            requires (requires{{std::ranges::rend(*__value)};})
        {
            return std::ranges::rend(*__value);
        }

        /* Get a reverse iterator to one before the first element of the range. */
        [[nodiscard]] constexpr auto crend() const
            noexcept (requires{{rend()} noexcept;})
            requires (requires{{rend()};})
        {
            return rend();
        }

        /// TODO: I'm not entirely sure how the conversion operators should behave,
        /// or the basis on which to judge them, so this needs to be thought through
        /// around the same time as the constructors, concepts, and CTAD guides.

        /* If the range is tuple-like, then conversions are allowed to any other type that
        can be directly constructed (via a braced initializer) from the perfectly-forwarded
        contents.  Otherwise, if the destination type has a matching `std::from_range`
        constructor, or constructor from a pair of iterators, then that constructor will be
        used instead. */
        template <typename Self, typename to> requires (!meta::prefer_constructor<to>)
        [[nodiscard]] constexpr operator to(this Self&& self)
            noexcept (
                requires{{impl::range_tuple_conversion<to>(
                    std::forward<Self>(self),
                    std::make_index_sequence<meta::tuple_size<C>>{}
                )} noexcept;} ||
                (
                    !requires{{impl::range_tuple_conversion<to>(
                        std::forward<Self>(self),
                        std::make_index_sequence<meta::tuple_size<C>>{}
                    )};} &&
                    requires{{to(std::from_range, std::forward<Self>(self))} noexcept;}
                ) || (
                    !requires{{impl::range_tuple_conversion<to>(
                        std::forward<Self>(self),
                        std::make_index_sequence<meta::tuple_size<C>>{}
                    )};} &&
                    !requires{{to(std::from_range, std::forward<Self>(self))};} &&
                    requires{{to(self.begin(), self.end())} noexcept;}
                )
            )
            requires (
                requires{{impl::range_tuple_conversion<to>(
                    std::forward<Self>(self),
                    std::make_index_sequence<meta::tuple_size<C>>{}
                )};} ||
                requires{{to(std::from_range, std::forward<Self>(self))};} ||
                requires{{to(self.begin(), self.end())};}
            )
        {
            if constexpr (requires{{impl::range_tuple_conversion<to>(
                std::forward<Self>(self),
                std::make_index_sequence<meta::tuple_size<C>>{}
            )};}) {
                return impl::range_tuple_conversion<to>(
                    std::forward<Self>(self),
                    std::make_index_sequence<meta::tuple_size<C>>{}
                );

            } else if constexpr (requires{{to(std::from_range, std::forward<Self>(self))};}) {
                return to(std::from_range, std::forward<Self>(self));

            } else {
                return to(self.begin(), self.end());
            }
        }

        /// TODO: range assignment is also complicated, and is probably tied to the
        /// constructor and conversion operator refactors.

        /* Assigning a range to another range triggers elementwise assignment between their
        contents.  If both ranges are tuple-like, then they must have the same size, such
        that the assignment can be done via a single fold expression. */
        template <typename L, meta::range R>
        constexpr L operator=(this L&& lhs, R&& rhs)
            noexcept (requires{{impl::range_tuple_assignment(
                lhs,
                std::forward<R>(rhs),
                std::make_index_sequence<meta::tuple_size<L>>{}
            )} noexcept;})
            requires (
                meta::tuple_like<L> && meta::tuple_like<R> &&
                meta::tuple_size<L> == meta::tuple_size<R> &&
                requires{{impl::range_tuple_assignment(
                    lhs,
                    std::forward<R>(rhs),
                    std::make_index_sequence<meta::tuple_size<L>>{}
                )};}
            )
        {
            impl::range_tuple_assignment(
                lhs,
                std::forward<R>(rhs),
                std::make_index_sequence<meta::tuple_size<L>>{}
            );
            return std::forward<L>(lhs);
        }

        /* Assigning a range to another range triggers elementwise assignment between their
        contents.  If either range is not tuple-like, then the assignment must be done with
        an elementwise loop. */
        template <typename L, meta::range R>
        constexpr L operator=(this L&& lhs, R&& rhs)
            requires (
                (!meta::tuple_like<L> || !meta::tuple_like<R>) &&
                requires(
                    decltype(std::ranges::begin(lhs)) lhs_it,
                    decltype(std::ranges::end(lhs)) lhs_end,
                    decltype(std::ranges::begin(rhs)) rhs_it,
                    decltype(std::ranges::end(rhs)) rhs_end
                ){
                    {lhs_it != lhs_end};
                    {rhs_it != rhs_end};
                    {*lhs_it = *rhs_it};
                    {++lhs_it};
                    {++rhs_it};
                }
            )
        {
            constexpr bool sized_lhs = meta::has_size<L>;
            constexpr bool sized_rhs = meta::has_size<R>;

            // ensure the sizes match if they can be determined in constant time
            size_t lhs_size = 0;
            size_t rhs_size = 0;
            if constexpr (sized_lhs && sized_rhs) {
                lhs_size = std::ranges::size(lhs);
                rhs_size = std::ranges::size(rhs);
                if (lhs_size != rhs_size) {
                    if consteval {
                        throw impl::range_size_mismatch<L, R>();
                    } else {
                        throw ValueError(
                            "cannot assign a range of size " + std::to_string(rhs_size) +
                            " to a slice of size " + std::to_string(lhs_size)
                        );
                    }
                }
            } else if constexpr (sized_lhs) {
                lhs_size = std::ranges::size(lhs);
            } else if constexpr (sized_rhs) {
                rhs_size = std::ranges::size(rhs);
            }

            // iterate over the range and assign each element
            auto lhs_it = std::ranges::begin(lhs);
            auto lhs_end = std::ranges::end(lhs);
            auto rhs_it = std::ranges::begin(rhs);
            auto rhs_end = std::ranges::end(rhs);
            while (lhs_it != lhs_end && rhs_it != rhs_end) {
                *lhs_it = *rhs_it;
                ++lhs_it;
                ++rhs_it;
                if constexpr (!sized_lhs) { ++lhs_size; }
                if constexpr (!sized_rhs) { ++rhs_size; }
            }

            // if the observed sizes do not match, then throw an error
            if constexpr (!sized_lhs || !sized_rhs) {
                if (lhs_it != lhs_end || rhs_it != rhs_end) {
                    if consteval {
                        throw impl::range_size_mismatch<L, R>();
                    } else {
                        if constexpr (!sized_lhs) {
                            while (lhs_it != lhs_end) {
                                ++lhs_it;
                                ++lhs_size;
                            }
                        }
                        if constexpr (!sized_rhs) {
                            while (rhs_it != rhs_end) {
                                ++rhs_it;
                                ++rhs_size;
                            }
                        }
                        throw ValueError(
                            "cannot assign a range of size " + std::to_string(rhs_size) +
                            " to a slice of size " + std::to_string(lhs_size)
                        );
                    }
                }
            }

            return std::forward<L>(lhs);
        }

        /// TODO: monadic call operator, which may be forward-declared and filled in
        /// later in op.h, once `zip{}` has been defined.

    };

    /* ADL `swap()` operator for ranges. */
    template <typename C>
    constexpr void swap(range<C>& lhs, range<C>& rhs)
        noexcept (requires{{lhs.swap(rhs)} noexcept;})
        requires (requires{{lhs.swap(rhs)};})
    {
        lhs.swap(rhs);
    }

}


static constexpr std::tuple tup {1, 2, 3.5};
static_assert([] {
    for (auto&& i : iter::range(tup)) {
        if (i != 1 && i != 2 && i != 3.5) {
            return false;
        }
    }
    for (auto&& i : iter::range(5)) {
        if (i != 5) {
            return false;
        }
    }
    return true;
}());


}


namespace std {

    /// TODO: enable_borrowed_range, tuple definitions

}


#endif  // BERTRAND_ITER_RANGE_H