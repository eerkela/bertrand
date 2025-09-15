#ifndef BERTRAND_ITER_RANGE_H
#define BERTRAND_ITER_RANGE_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/math.h"
#include "bertrand/union.h"


namespace bertrand {


namespace impl {
    struct range_tag {};
    struct sequence_tag {};
    struct iota_tag {};

    template <meta::not_rvalue C> requires (meta::iterable<C>)
    struct unpack;

}


namespace meta {

    namespace detail {

        template <typename>
        constexpr bool unpack = false;
        template <typename C>
        constexpr bool unpack<impl::unpack<C>> = true;

        template <typename T, bool done, size_t I, typename... Rs>
        constexpr bool _unpack_convert = done;
        template <typename T, bool done, size_t I, typename R> requires (I < meta::tuple_size<T>)
        constexpr bool _unpack_convert<T, done, I, R> =
            meta::convertible_to<meta::get_type<T, I>, R> && _unpack_convert<T, true, I + 1, R>;
        template <typename T, bool done, size_t I, typename R, typename... Rs>
            requires (I < meta::tuple_size<T>)
        constexpr bool _unpack_convert<T, done, I, R, Rs...> =
            meta::convertible_to<meta::get_type<T, I>, R> && _unpack_convert<T, done, I + 1, Rs...>;
        template <typename T, typename... Rs>
        constexpr bool unpack_convert = _unpack_convert<T, false, 0, Rs...>;

    }

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

    template <typename T, typename R = void>
    concept sequence = range<T, R> && inherits<T, impl::sequence_tag>;

    template <typename T, typename R = void>
    concept unpack = range<T, R> && detail::unpack<unqualify<T>>;

    // template <typename T, typename... Rs>
    // concept unpack_to = detail::unpack<unqualify<T>> && (
    //     (tuple_like<T> && tuple_size<T> == sizeof...(Rs) && detail::unpack_convert<T, Rs...>) ||
    //     (!tuple_like<T> && ... && yields<T, Rs>)
    // );

    namespace detail {

        template <meta::range T>
        constexpr bool prefer_constructor<T> = true;

        /// TODO: wraparound<T>

    }

}


namespace impl {

    /* A trivial range with zero elements.  This is the default type for the `range`
    class template, allowing it to be default-constructed. */
    struct empty_range {
        static constexpr void swap(empty_range&) noexcept {}
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
        [[no_unique_address]] Optional<T> __value;

        [[nodiscard]] constexpr single_range() = default;

        template <typename... A> requires (sizeof...(A) > 0)
        [[nodiscard]] constexpr single_range(A&&... args)
            noexcept (meta::nothrow::constructible_from<T, A...>)
            requires (meta::constructible_from<T, A...>)
        :
            __value{std::forward<A>(args)...}
        {}

        constexpr void swap(single_range& other)
            noexcept (meta::nothrow::swappable<impl::ref<T>>)
            requires (meta::swappable<impl::ref<T>>)
        {
            std::ranges::swap(__value, other.__value);
        }

        [[nodiscard]] constexpr size_t size() const noexcept { return __value != None; }
        [[nodiscard]] constexpr ssize_t ssize() const noexcept { return __value != None; }
        [[nodiscard]] constexpr bool empty() const noexcept { return __value == None; }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{meta::to_arrow(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::to_arrow(*std::forward<Self>(self).__value)};})
        {
            return meta::to_arrow(*std::forward<Self>(self).__value);
        }

        [[nodiscard]] constexpr auto data()
            noexcept (requires{{__value == None ? nullptr : std::addressof(*__value)} noexcept;})
            requires (requires{{__value == None ? nullptr : std::addressof(*__value)};})
        {
            return __value == None ? nullptr : std::addressof(*__value);
        }

        [[nodiscard]] constexpr auto data() const
            noexcept (requires{{__value == None ? nullptr : std::addressof(*__value)} noexcept;})
            requires (requires{{__value == None ? nullptr : std::addressof(*__value)};})
        {
            return __value == None ? nullptr : std::addressof(*__value);
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
            noexcept (requires{{__value.begin()} noexcept;})
            requires (requires{{__value.begin()};})
        {
            return __value.begin();
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{__value.begin()} noexcept;})
            requires (requires{{__value.begin()};})
        {
            return __value.begin();
        }

        [[nodiscard]] constexpr auto end()
            noexcept (requires{{__value.end()} noexcept;})
            requires (requires{{__value.end()};})
        {
            return __value.end();
        }

        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{__value.end()} noexcept;})
            requires (requires{{__value.end()};})
        {
            return __value.end();
        }

        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{__value.rbegin()} noexcept;})
            requires (requires{{__value.rbegin()};})
        {
            return __value.rbegin();
        }

        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{__value.rbegin()} noexcept;})
            requires (requires{{__value.rbegin()};})
        {
            return __value.rbegin();
        }

        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{__value.rend()} noexcept;})
            requires (requires{{__value.rend()};})
        {
            return __value.rend();
        }

        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{__value.rend()} noexcept;})
            requires (requires{{__value.rend()};})
        {
            return __value.rend();
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
            noexcept (requires{{impl::arrow(**this)} noexcept;})
        {
            return impl::arrow(**this);
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
    type, in order to perfectly forward the results. */
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

    template <typename>
    struct _tuple_range {
        using type = const NoneType&;
        static constexpr tuple_kind kind = tuple_kind::EMPTY;
    };
    template <typename T, typename... Ts>
    struct _tuple_range<meta::pack<T, Ts...>> {
        using type = meta::union_type<T, Ts...>;
        static constexpr tuple_kind kind =
            meta::trivial_union<T, Ts...> ? tuple_kind::TRIVIAL : tuple_kind::VTABLE;
    };

    /* A wrapper around a generic tuple type that allows it to be indexed and iterated
    over at runtime, by dispatching to a reference array or vtable.  If the tuple
    consists of multiple types, then the subscript and yield types will be promoted to
    unions of all the possible results. */
    template <meta::tuple_like C>
    struct tuple_range : _tuple_range<meta::tuple_types<C>> {
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
        requires (_tuple_range<meta::tuple_types<C>>::kind == tuple_kind::TRIVIAL)
    struct tuple_range<C> : _tuple_range<meta::tuple_types<C>> {
        [[no_unique_address]] impl::ref<C> __value;

        [[nodiscard]] static constexpr size_t size() noexcept { return meta::tuple_size<C>; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return ssize_t(size()); }
        [[nodiscard]] static constexpr bool empty() noexcept { return false; }

    private:
        using base = _tuple_range<meta::tuple_types<C>>;
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

        constexpr void swap(tuple_range& other)
            noexcept (meta::nothrow::swappable<impl::ref<C>> && meta::nothrow::swappable<array>)
            requires (meta::swappable<impl::ref<C>> && meta::swappable<array>)
        {
            std::ranges::swap(__value, other.__value);
            std::ranges::swap(elements, other.elements);
        }

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
        requires (_tuple_range<meta::tuple_types<C>>::kind == tuple_kind::VTABLE)
    struct tuple_range<C> : _tuple_range<meta::tuple_types<C>> {
        [[no_unique_address]] impl::ref<C> __value;

        [[nodiscard]] static constexpr size_t size() noexcept { return meta::tuple_size<C>; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return ssize_t(size()); }
        [[nodiscard]] static constexpr bool empty() noexcept { return false; }

    private:
        using base = _tuple_range<meta::tuple_types<C>>;

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

        constexpr void swap(tuple_range& other)
            noexcept (meta::nothrow::swappable<impl::ref<C>>)
            requires (meta::swappable<impl::ref<C>>)
        {
            std::ranges::swap(__value, other.__value);
        }

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

    template <typename T>
    concept strictly_positive = meta::unsigned_integer<T> || !requires(meta::as_const_ref<T> t) {
        {t < 0} -> meta::explicitly_convertible_to<bool>;
    };

    /* Iota iterators will use the difference type between `stop` and `start` if
    available and integer-like.  Otherwise, if `stop` is `iota_tag`, and start has
    an integer difference with respect to itself, then we use that type.  Lastly, we
    default to `std::ptrdiff_t` as a fallback. */
    template <typename Start, typename Stop>
    struct iota_difference { using type = std::ptrdiff_t; };
    template <typename Start, typename Stop>
        requires (requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {stop - start} -> meta::signed_integer;
        })
    struct iota_difference<Start, Stop> {
        using type = decltype(
            std::declval<meta::as_const_ref<Stop>>() -
            std::declval<meta::as_const_ref<Start>>()
        );
    };
    template <typename Start, meta::None Stop>
        requires (!requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {stop - start};
        } && requires(meta::as_const_ref<Start> start) {
            {start - start} -> meta::signed_integer;
        })
    struct iota_difference<Start, Stop> {
        using type = decltype(
            std::declval<meta::as_const_ref<Start>>() -
            std::declval<meta::as_const_ref<Start>>()
        );
    };

    /// TODO: these categories should probably be deduced based on the specific
    /// concepts that are satisfied.  The difference type is fine, but the tags need
    /// to consistently match the behavior of the iota class itself.  Perhaps it's
    /// best to deduce the category from within the iota class itself, after the
    /// full interface has been defined, and I can put it all in a giant requires
    /// clause.

    template <typename Start>
    concept iota_forward = requires(Start start) {
        {start == start} -> meta::convertible_to<bool>;
        {start != start} -> meta::convertible_to<bool>;
    };
    template <typename Start>
    concept iota_bidirectional = iota_forward<Start> && requires(Start start) {
        {--start};
    };
    template <typename Start, typename diff>
    concept iota_random_access = iota_bidirectional<Start> &&
        requires(Start start, meta::as_lvalue<Start> istart, diff n) {
            {start + n} -> meta::convertible_to<Start>;
            {start - n} -> meta::convertible_to<Start>;
            {istart += n} -> meta::convertible_to<Start&>;
            {istart -= n} -> meta::convertible_to<Start&>;
            {start < start} -> meta::convertible_to<bool>;
            {start <= start} -> meta::convertible_to<bool>;
            {start > start} -> meta::convertible_to<bool>;
            {start >= start} -> meta::convertible_to<bool>;
        };
    template <typename Start, typename diff, typename Step>
    concept iota_contiguous =
        meta::contiguous_iterator<Start> &&
        meta::is<Step, iota_tag> &&
        iota_random_access<Start, diff>;

    /* Iota iterators default to modeling `std::input_iterator` only.  If `Start` is
    comparable with itself, then the iterator can be upgraded to model
    `std::forward_iterator`.  If `Start` is also decrementable, then the iterator can
    be further upgraded to model `std::bidirectional_iterator`.  Lastly, if `Start`
    also supports addition, subtraction, and ordered comparisons, then the iterator can
    be upgraded to model `std::random_access_iterator`.  Contiguous iterators will only
    be generated if `Start` is itself a contiguous iterator and no step size is
    given. */
    template <typename Start, typename Stop, typename Step>
    struct iota_category { using type = std::input_iterator_tag; };
    template <typename Start, typename Stop, typename Step>
        requires (iota_forward<Start>)
    struct iota_category<Start, Stop, Step> { using type = std::forward_iterator_tag; };
    template <typename Start, typename Stop, typename Step>
        requires (iota_bidirectional<Start>)
    struct iota_category<Start, Stop, Step> { using type = std::bidirectional_iterator_tag; };
    template <typename Start, typename Stop, typename Step>
        requires (iota_random_access<Start, typename iota_difference<Start, Stop>::type>)
    struct iota_category<Start, Stop, Step> { using type = std::random_access_iterator_tag; };
    template <typename Start, typename Stop, typename Step>
        requires (iota_contiguous<Start, typename iota_difference<Start, Stop>::type, Step>)
    struct iota_category<Start, Stop, Step> { using type = std::contiguous_iterator_tag; };

    /// TODO: similarly, value_type, reference, and pointer can be deduced from
    /// `decltype(curr())`, which reduces the template depth.

    template <typename T>
    struct iota_value_type { using type = meta::remove_reference<T>; };
    template <meta::iterator T>
    struct iota_value_type<T> { using type = meta::iterator_value_type<T>; };

    template <typename T>
    struct iota_reference { using type = meta::as_lvalue<T>; };
    template <meta::iterator T>
    struct iota_reference<T> { using type = meta::iterator_reference_type<T>; };

    template <typename T>
    struct iota_pointer { using type = meta::as_pointer<T>; };
    template <meta::iterator T>
    struct iota_pointer<T> { using type = meta::iterator_pointer_type<T>; };

    template <typename Start>
    concept iota_subrange = meta::iterator<Start>;

    template <typename Start, typename Step>
    concept iota_unit = meta::is<Step, iota_tag> && requires(meta::as_lvalue<Start> start) {
        {++start};
    };

    template <typename Start, typename Stop, typename Step>
    concept iota_jump = (meta::is<Step, iota_tag> && requires(
        meta::as_lvalue<Start> start,
        typename iota_difference<Start, Stop>::type i
    ) {
        {++start};
        {--start};
        {start += i};
        {start -= i};
    }) || (!meta::is<Step, iota_tag> && requires(
        meta::as_lvalue<Start> start,
        typename iota_difference<Start, Stop>::type i,
        meta::as_const_ref<Step> step
    ) {
        {start += step};
        {start -= step};
        {start += i * step};
        {start -= i * step};
    });

    template <typename Start, typename Stop, typename Step>
    concept iota_loop = iota_unit<Start, Step> || (
        meta::integer<Step> &&
        requires(meta::as_lvalue<Start> start) {
            {++start};
        } && (strictly_positive<Step> || requires(meta::as_lvalue<Start> start) {
            {--start};
        })
    );

    template <typename Start, typename Stop, typename Step>
    concept iota_filter =
        !iota_unit<Start, Step> &&
        !iota_jump<Start, Stop, Step> &&
        !iota_loop<Start, Stop, Step> && (
            (meta::range<Step> && meta::yields<Step, bool>) ||
            (meta::iterator<Start> && requires(
                meta::as_lvalue<Start> start,
                meta::as_const_ref<Step> filter
            ) {
                {filter(*start)} -> meta::convertible_to<bool>;
            }) || (!meta::iterator<Start> && requires(
                meta::as_lvalue<Start> start,
                meta::as_const_ref<Step> filter
            ) {
                {filter(start)} -> meta::convertible_to<bool>;
            })
        );

    template <typename Stop>
    concept iota_infinite = meta::is<Stop, iota_tag>;

    template <typename Start, typename Stop, typename Step>
    concept iota_ordered =
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {!(start < stop)} -> meta::convertible_to<bool>;
        } && (strictly_positive<Step> || requires(
            meta::as_const_ref<Start> start,
            meta::as_const_ref<Stop> stop
        ) {
            {!(start > stop)} -> meta::convertible_to<bool>;
        });

    template <typename Start, typename Stop, typename Step>
    concept iota_exact =
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {start == stop} -> meta::convertible_to<bool>;
        };

    template <typename Start, typename Stop, typename Step>
    concept iota_counted =
        meta::integer<Stop> &&
        requires(meta::as_const_ref<Stop> stop, typename iota_difference<Start, Stop>::type index) {
            {index >= stop} -> meta::convertible_to<bool>;
        };

    template <typename Start, typename Stop, typename Step>
    concept iota_conditional =
        (iota_subrange<Start> && requires(
            meta::as_const_ref<Start> start,
            meta::as_const_ref<Stop> condition
        ) {
            {condition(*start)} -> meta::convertible_to<bool>;
        }) || (!iota_subrange<Start> && requires(
            meta::as_const_ref<Start> start,
            meta::as_const_ref<Stop> condition
        ) {
            {condition(start)} -> meta::convertible_to<bool>;
        });

    template <typename Start, typename Stop, typename Step>
    concept iota_concept =
        meta::copyable<meta::unqualify<Start>> && (
            iota_infinite<Stop> ||
            iota_ordered<Start, Stop, Step> ||
            iota_exact<Start, Stop, Step> ||
            iota_counted<Start, Stop, Step> ||
            iota_conditional<Start, Stop, Step>
        ) && (
            iota_unit<Start, Step> ||
            iota_jump<Start, Stop, Step> ||
            iota_loop<Start, Stop, Step> ||
            iota_filter<Start, Stop, Step>
        );

    constexpr AssertionError iota_negative_count() noexcept {
        return AssertionError("count cannot be negative");
    }

    constexpr AssertionError iota_zero_step() noexcept {
        return AssertionError("step size cannot be zero");
    }

    /// TODO: iota -> interval?

    /// TODO: add boolean predicate and masking support for step.

    /* Non-subrange iotas allow arbitrary `start`, `stop`, and `step` types, and
    attempt to minimize overhead as much as possible.  Unlike subranges, their
    `index()` will always be aligned in units of `step` (with filter expressions
    equating to a trivial step), since there is no underlying range to consider.  They
    also do not include the extra bounds-checking logic, since value-based iotas are
    assumed not to suffer from the same undefined behavior situation as subranges,
    which are often simply pointers to the individual elements. */
    template <typename Start, typename Stop, typename Step>
    struct iota_storage {
        using copy = iota_storage<meta::unqualify<Start>, Stop, Step>;
        using difference_type = iota_difference<Start, Stop>::type;

        [[no_unique_address]] impl::ref<Start> m_start {};
        [[no_unique_address]] impl::ref<Stop> m_stop {};
        [[no_unique_address]] impl::ref<Step> m_step {};
        [[no_unique_address]] difference_type m_index {};

        constexpr void swap(iota_storage& other)
            noexcept (requires{
                {m_start.swap(other.m_start)} noexcept;
                {m_stop.swap(other.m_stop)} noexcept;
                {m_step.swap(other.m_step)} noexcept;
                {std::ranges::swap(m_index, other.m_index)} noexcept;
            })
            requires (requires{
                {m_start.swap(other.m_start)};
                {m_stop.swap(other.m_stop)};
                {m_step.swap(other.m_step)};
                {std::ranges::swap(m_index, other.m_index)};
            })
        {
            m_start.swap(other.m_start);
            m_stop.swap(other.m_stop);
            m_step.swap(other.m_step);
            std::ranges::swap(m_index, other.m_index);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) start(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).m_start} noexcept;})
            requires (requires{{*std::forward<Self>(self).m_start};})
        {
            return (*std::forward<Self>(self).m_start);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) stop(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).m_stop} noexcept;})
            requires (!iota_infinite<Stop> && requires{{*std::forward<Self>(self).m_stop};})
        {
            return (*std::forward<Self>(self).m_stop);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) step(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).m_step} noexcept;})
            requires (!iota_unit<Start, Step> && requires{{*std::forward<Self>(self).m_step};})
        {
            return (*std::forward<Self>(self).m_step);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) curr(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).start()} noexcept;})
            requires (requires{{std::forward<Self>(self).start()};})
        {
            return (std::forward<Self>(self).start());
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) subscript(this Self&& self, difference_type n)
            noexcept (requires{
                {std::forward<Self>(self).start() + n} noexcept;
            })
            requires (iota_unit<Start, Step> && requires{
                {std::forward<Self>(self).start() + n};
            })
        {
            return (std::forward<Self>(self).start() + n);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) subscript(this Self&& self, difference_type n)
            noexcept (requires{
                {std::forward<Self>(self).start() + n * std::forward<Self>(self).step()} noexcept;
            })
            requires (!iota_unit<Start, Step> && requires{
                {std::forward<Self>(self).start() + n * std::forward<Self>(self).step()};
            })
        {
            return (std::forward<Self>(self).start() + n * std::forward<Self>(self).step());
        }

        [[nodiscard]] constexpr difference_type index() const
            noexcept (requires{
                {m_index} noexcept -> meta::nothrow::convertible_to<difference_type>;
            })
            requires (requires{
                {m_index} -> meta::convertible_to<difference_type>;
            })
        {
            return m_index;
        }

        [[nodiscard]] static constexpr bool empty() noexcept requires (iota_infinite<Stop>) {
            return false;
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {!(start() < stop())} noexcept -> meta::nothrow::convertible_to<bool>;
            } && (strictly_positive<Step> || requires{
                {step() < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {!(start() > stop())} noexcept -> meta::nothrow::convertible_to<bool>;
            }))
            requires (
                !iota_infinite<Stop> &&
                iota_ordered<Start, Stop, Step>
            )
        {
            if constexpr (strictly_positive<Step>) {
                return !(start() < stop());
            } else {
                if (step() < 0) {
                    return !(start() > stop());
                } else {
                    return !(start() < stop());
                }
            }
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {start() == stop()} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (
                !iota_infinite<Stop> &&
                !iota_ordered<Start, Stop, Step> &&
                iota_exact<Start, Stop, Step>
            )
        {
            return start() == stop();
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {index() >= stop()} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (
                !iota_infinite<Stop> &&
                !iota_ordered<Start, Stop, Step> &&
                !iota_exact<Start, Stop, Step> &&
                iota_counted<Start, Stop, Step>
            )
        {
            return index() >= stop();
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {stop()(curr())} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (
                !iota_infinite<Stop> &&
                !iota_ordered<Start, Stop, Step> &&
                !iota_exact<Start, Stop, Step> &&
                !iota_counted<Start, Stop, Step> &&
                iota_conditional<Start, Stop, Step>
            )
        {
            return stop()(curr());
        }

        [[nodiscard]] constexpr difference_type remaining() const
            noexcept (requires{{
                difference_type(stop()) - m_index
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (iota_counted<Start, Stop, Step> && requires{{
                difference_type(stop()) - m_index
            } -> meta::convertible_to<difference_type>;})
        {
            return difference_type(stop()) - m_index;
        }

        [[nodiscard]] constexpr auto remaining() const
            noexcept (requires{{stop() - start()} noexcept;})
            requires (!iota_counted<Start, Stop, Step> && requires{{stop() - start()};})
        {
            return stop() - start();
        }

        constexpr bool filter() const
            noexcept (requires{{step()(curr())} noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (iota_filter<Start, Stop, Step> && requires{
                {step()(curr())} -> meta::convertible_to<bool>;
            })
        {
            return step()(curr());
        }

        constexpr void first_filtered()
            noexcept (requires{
                {!empty() && !filter()} noexcept;
                {++start()} noexcept;
                {++m_index} noexcept;
            })
            requires (iota_filter<Start, Stop, Step> && requires{
                {!empty() && !filter()};
                {++start()};
                {++m_index};
            })
        {
            while (!empty() && !filter()) {
                ++start();
                ++m_index;
            }
        }

        constexpr void increment()
            noexcept (requires{
                {++start()} noexcept;
                {++m_index} noexcept;
            })
            requires (iota_unit<Start, Step> && requires{
                {++start()};
                {++m_index} noexcept;
            })
        {
            ++start();
            ++m_index;
        }

        template <typename T>
        constexpr void increment_by(const T& n)
            noexcept (requires{{start() += n} noexcept;} && (
                (meta::is<Step, iota_tag> && requires{
                    {m_index += difference_type(n)} noexcept;
                }) ||
                (!meta::is<Step, iota_tag> && requires{
                    {m_index += difference_type(n / step())} noexcept;
                })
            ))
            requires (iota_jump<Start, Stop, Step> && requires{{start() += n};} && (
                (meta::is<Step, iota_tag> && requires{
                    {m_index += difference_type(n)};
                }) ||
                (!meta::is<Step, iota_tag> && requires{
                    {m_index += difference_type(n / step())};
                })
            ))
        {
            start() += n;
            if constexpr (meta::is<Step, iota_tag>) {
                m_index += difference_type(n);
            } else {
                m_index += difference_type(n / step());
            }
        }

        template <meta::integer T>
        constexpr void increment_for(const T& n)
            noexcept (requires(T i) {
                {T{}} noexcept;
                {i < n} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {++i} noexcept;
                {++start()} noexcept;
            } && (strictly_positive<T> || requires(T i) {
                {n < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {i > n} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {--i} noexcept;
                {--start()} noexcept;
            }) && (
                (meta::is<Step, iota_tag> && requires{
                    {m_index += difference_type(n)} noexcept;
                }) ||
                (!meta::is<Step, iota_tag> && requires{
                    {m_index += difference_type(n / step())} noexcept;
                })
            ))
            requires (iota_loop<Start, Stop, Step> && requires(T i) {
                {T{}};
                {i < n} -> meta::explicitly_convertible_to<bool>;
                {++i};
                {++start()};
            } && (strictly_positive<T> || requires(T i) {
                {n < 0} -> meta::explicitly_convertible_to<bool>;
                {i > n} -> meta::explicitly_convertible_to<bool>;
                {--i};
                {--start()};
            }) && (
                (meta::is<Step, iota_tag> && requires{
                    {m_index += difference_type(n)};
                }) ||
                (!meta::is<Step, iota_tag> && requires{
                    {m_index += difference_type(n / step())};
                })
            ))
        {
            if constexpr (strictly_positive<T>) {
                for (T i = {}; i < n; ++i) ++start();
            } else {
                if (n < 0) {
                    for (T i = {}; i > n; --i) --start();
                } else {
                    for (T i = {}; i < n; ++i) ++start();
                }
            }
            if constexpr (meta::is<Step, iota_tag>) {
                m_index += difference_type(n);
            } else {
                m_index += difference_type(n / step());
            }
        }

        constexpr void increment_while()
            noexcept (requires{
                {++start()} noexcept;
                {++m_index} noexcept;
                {!empty() && !filter()} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
            })
            requires (iota_filter<Start, Stop, Step> && requires{
                {++start()};
                {++m_index};
                {!empty() && !filter()} -> meta::explicitly_convertible_to<bool>;
            })
        {
            do {
                ++start();
                ++m_index;
            } while (!empty() && !filter());
        }

        constexpr void decrement()
            noexcept (requires{
                {--start()} noexcept;
                {--m_index} noexcept;
            })
            requires (iota_unit<Start, Step> && requires{
                {--start()};
                {--m_index};
            })
        {
            --start();
            --m_index;
        }

        template <typename T>
        constexpr void decrement_by(const T& n)
            noexcept (requires{{start() -= n} noexcept;} && (
                (meta::is<Step, iota_tag> && requires{
                    {m_index -= difference_type(n)} noexcept;
                }) ||
                (!meta::is<Step, iota_tag> && requires{
                    {m_index -= difference_type(n / step())} noexcept;
                })
            ))
            requires (iota_jump<Start, Stop, Step> && requires{{start() -= n};} && (
                (meta::is<Step, iota_tag> && requires{
                    {m_index -= difference_type(n)};
                }) ||
                (!meta::is<Step, iota_tag> && requires{
                    {m_index -= difference_type(n / step())};
                })
            ))
        {
            start() -= n;
            if constexpr (meta::is<Step, iota_tag>) {
                m_index -= difference_type(n);
            } else {
                m_index -= difference_type(n / step());
            }
        }

        template <meta::integer T>
        constexpr void decrement_for(const T& n)
            noexcept (requires(T i) {
                {T{}} noexcept;
                {i < n} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {++i} noexcept;
                {--start()} noexcept;
                {--m_index} noexcept;
            } && (strictly_positive<T> || requires(T i) {
                {n < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {i > n} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {--i} noexcept;
                {++start()} noexcept;
            }) && (
                (meta::is<Step, iota_tag> && requires{
                    {m_index -= difference_type(n)} noexcept;
                }) ||
                (!meta::is<Step, iota_tag> && requires{
                    {m_index -= difference_type(n / step())} noexcept;
                })
            ))
            requires (iota_loop<Start, Stop, Step> && requires(T i) {
                {T{}};
                {i < n} -> meta::explicitly_convertible_to<bool>;
                {++i};
                {--start()};
                {--m_index};
            } && (strictly_positive<T> || requires(T i) {
                {n < 0} -> meta::explicitly_convertible_to<bool>;
                {i > n} -> meta::explicitly_convertible_to<bool>;
                {--i};
                {++start()};
            }) && (
                (meta::is<Step, iota_tag> && requires{
                    {m_index -= difference_type(n)};
                }) ||
                (!meta::is<Step, iota_tag> && requires{
                    {m_index -= difference_type(n / step())};
                })
            ))
        {
            if constexpr (strictly_positive<T>) {
                for (T i = {}; i < n; ++i) --start();
            } else {
                if (n < 0) {
                    for (T i = {}; i > n; --i) ++start();
                } else {
                    for (T i = {}; i < n; ++i) --start();
                }
            }
            if constexpr (meta::is<Step, iota_tag>) {
                m_index -= difference_type(n);
            } else {
                m_index -= difference_type(n / step());
            }
        }

        constexpr void decrement_while()
            noexcept (requires{
                {--start()} noexcept;
                {--m_index} noexcept;
                {
                    m_index >= 0 && !filter()
                } noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
            })
            requires (iota_filter<Start, Stop, Step> && requires{
                {--start()};
                {--m_index};
                {m_index >= 0 && !filter()} -> meta::explicitly_convertible_to<bool>;
            })
        {
            do {
                --start();
                --m_index;
            } while (m_index >= 0 && !filter());
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{copy{start(), m_stop, m_step, m_index}} noexcept;})
            requires (requires{{copy{start(), m_stop, m_step, m_index}};})
        {
            return copy{start(), m_stop, m_step, m_index};
        }

        [[nodiscard]] constexpr auto begin() &&
            noexcept (requires{{copy{
                std::move(start()),
                std::move(m_stop),
                std::move(m_step),
                std::move(m_index)
            }} noexcept;})
            requires (requires{{copy{
                std::move(start()),
                std::move(m_stop),
                std::move(m_step),
                std::move(m_index)
            }};})
        {
            return copy{
                std::move(start()),
                std::move(m_stop),
                std::move(m_step),
                std::move(m_index)
            };
        }
    };

    /* If the `start` index is an iterator, then iotas will store an additional index
    and include bounds-checking to guard against undefined behavior during constant
    evaluation.  Without these checks, non-trivial step sizes may cause the iterator to
    walk off the end of the underlying range, which can trip the compiler's UB
    sanitizer and cause the iota to fail compilation.  Additionally, since the start
    index is confined to iterators, the step size (if present) will necessarily be an
    integer or conditional, and the `index()` will always be aligned to the underlying
    range. */
    template <meta::iterator Start, typename Stop, typename Step>
    struct iota_storage<Start, Stop, Step> {
        using copy = iota_storage<meta::unqualify<Start>, Stop, Step>;
        using difference_type = iota_difference<Start, Stop>::type;

        [[no_unique_address]] impl::ref<Start> m_start {};
        [[no_unique_address]] impl::ref<Stop> m_stop {};
        [[no_unique_address]] impl::ref<Step> m_step {};
        [[no_unique_address]] difference_type m_index {};
        [[no_unique_address]] difference_type m_overflow {};

        constexpr void swap(iota_storage& other)
            noexcept (requires{
                {m_start.swap(other.m_start)} noexcept;
                {m_stop.swap(other.m_stop)} noexcept;
                {m_step.swap(other.m_step)} noexcept;
                {std::ranges::swap(m_index, other.m_index)} noexcept;
                {std::ranges::swap(m_overflow, other.m_overflow)} noexcept;
            })
            requires (requires{
                {m_start.swap(other.m_start)};
                {m_stop.swap(other.m_stop)};
                {m_step.swap(other.m_step)};
                {std::ranges::swap(m_index, other.m_index)};
                {std::ranges::swap(m_overflow, other.m_overflow)};
            })
        {
            m_start.swap(other.m_start);
            m_stop.swap(other.m_stop);
            m_step.swap(other.m_step);
            std::ranges::swap(m_index, other.m_index);
            std::ranges::swap(m_overflow, other.m_overflow);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) start(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).m_start} noexcept;})
            requires (requires{{*std::forward<Self>(self).m_start};})
        {
            return (*std::forward<Self>(self).m_start);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) stop(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).m_stop} noexcept;})
            requires (!iota_infinite<Stop> && requires{{*std::forward<Self>(self).m_stop};})
        {
            return (*std::forward<Self>(self).m_stop);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) step(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).m_step} noexcept;})
            requires (!iota_unit<Start, Step> && requires{{*std::forward<Self>(self).m_step};})
        {
            return (*std::forward<Self>(self).m_step);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) curr(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).start()} noexcept;})
            requires (requires{{*std::forward<Self>(self).start()};})
        {
            return (*std::forward<Self>(self).start());
        }

        template <typename Self>
        [[nodiscard]] constexpr auto subscript(this Self&& self, difference_type n)
            noexcept (requires{
                {*(std::forward<Self>(self).start() + n)} noexcept;
            })
            requires (iota_unit<Start, Step> && requires{
                {*(std::forward<Self>(self).start() + n)};
            })
        {
            return *(std::forward<Self>(self).start() + n);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto subscript(this Self&& self, difference_type n)
            noexcept (requires{
                {*(std::forward<Self>(self).start() + n * std::forward<Self>(self).step())} noexcept;
            })
            requires (!iota_unit<Start, Step> && requires{
                {*(std::forward<Self>(self).start() + n * std::forward<Self>(self).step())};
            })
        {
            return *(std::forward<Self>(self).start() + n * std::forward<Self>(self).step());
        }

        [[nodiscard]] constexpr difference_type index() const
            noexcept (requires{
                {m_index + m_overflow} noexcept -> meta::nothrow::convertible_to<difference_type>;
            })
            requires (requires{
                {m_index + m_overflow} -> meta::convertible_to<difference_type>;
            })
        {
            return m_index + m_overflow;
        }

        [[nodiscard]] constexpr difference_type remaining() const
            noexcept (requires{{
                difference_type(stop()) - m_index
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (iota_counted<Start, Stop, Step> && requires{{
                difference_type(stop()) - m_index
            } -> meta::convertible_to<difference_type>;})
        {
            return difference_type(stop()) - m_index;
        }

        [[nodiscard]] constexpr auto remaining() const
            noexcept (requires{{stop() - start()} noexcept;})
            requires (!iota_counted<Start, Stop, Step> && requires{{stop() - start()};})
        {
            return stop() - start();
        }
        [[nodiscard]] static constexpr bool empty() noexcept requires (iota_infinite<Stop>) {
            return false;
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {!(start() < stop())} noexcept -> meta::nothrow::convertible_to<bool>;
            } && (strictly_positive<Step> || requires{
                {step() < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {!(start() > stop())} noexcept -> meta::nothrow::convertible_to<bool>;
            }))
            requires (
                !iota_infinite<Stop> &&
                iota_ordered<Start, Stop, Step>
            )
        {
            if constexpr (strictly_positive<Step>) {
                return !(start() < stop());
            } else {
                if (step() < 0) {
                    return !(start() > stop());
                } else {
                    return !(start() < stop());
                }
            }
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {start() == stop()} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (
                !iota_infinite<Stop> &&
                !iota_ordered<Start, Stop, Step> &&
                iota_exact<Start, Stop, Step>
            )
        {
            return start() == stop();
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {index() >= stop()} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (
                !iota_infinite<Stop> &&
                !iota_ordered<Start, Stop, Step> &&
                !iota_exact<Start, Stop, Step> &&
                iota_counted<Start, Stop, Step>
            )
        {
            return index() >= stop();
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {stop()(curr())} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (
                !iota_infinite<Stop> &&
                !iota_ordered<Start, Stop, Step> &&
                !iota_exact<Start, Stop, Step> &&
                !iota_counted<Start, Stop, Step> &&
                iota_conditional<Start, Stop, Step>
            )
        {
            return stop()(curr());
        }

        constexpr bool filter() const
            noexcept (requires{{step()(curr())} noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (iota_filter<Start, Stop, Step> && requires{
                {step()(curr())} -> meta::convertible_to<bool>;
            })
        {
            return step()(curr());
        }

        constexpr void first_filtered()
            noexcept (requires{
                {!empty() && !filter()} noexcept;
                {++start()} noexcept;
                {++m_index} noexcept;
            })
            requires (iota_filter<Start, Stop, Step> && requires{
                {!empty() && !filter()};
                {++start()};
                {++m_index};
            })
        {
            while (!empty() && !filter()) {
                ++start();
                ++m_index;
            }
        }

        constexpr void increment()
            noexcept (requires{
                {
                    empty() || m_overflow < 0
                } noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {++m_overflow} noexcept;
                {++start()} noexcept;
                {++m_index} noexcept;
            })
            requires (iota_unit<Start, Step> && requires{
                {empty() || m_overflow < 0} -> meta::explicitly_convertible_to<bool>;
                {++m_overflow};
                {++start()};
                {++m_index} noexcept;
            })
        {
            if (empty() || m_overflow < 0) {
                ++m_overflow;
            } else {
                ++start();
                ++m_index;
            }
        }

        constexpr void increment_by(difference_type n)
            noexcept (requires(difference_type delta) {
                {empty()} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {m_overflow += n} noexcept;
                {m_overflow < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {n = m_overflow} noexcept;
                {m_overflow = 0} noexcept;
                {remaining()} noexcept -> meta::nothrow::convertible_to<difference_type>;
                {n > delta} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {start() += delta} noexcept;
                {m_overflow = n - delta} noexcept;
                {m_index += delta} noexcept;
            })
            requires (iota_jump<Start, Stop, Step> && requires(difference_type delta) {
                {empty()} -> meta::explicitly_convertible_to<bool>;
                {m_overflow += n};
                {m_overflow < 0} -> meta::explicitly_convertible_to<bool>;
                {n = m_overflow};
                {m_overflow = 0};
                {remaining()} -> meta::convertible_to<difference_type>;
                {n > delta} -> meta::explicitly_convertible_to<bool>;
                {start() += delta};
                {m_overflow = n - delta};
                {m_index += delta};
            })
        {
            if (empty()) {
                m_overflow += n;
                return;
            }
            if (m_overflow < 0) {
                m_overflow += n;
                if (m_overflow < 0) {
                    return;
                }
                n = m_overflow;
                m_overflow = 0;
            }
            difference_type delta = remaining();
            if (n > delta) {
                start() += delta;
                m_overflow = n - delta;
                m_index += delta;
            } else {
                start() += n;
                m_index += n;
            }
        }

        template <meta::integer T>
        constexpr void increment_for(const T& n)
            noexcept (requires(T i) {
                {T{}} noexcept;
                {i < n} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {++i} noexcept;
                {
                    empty() || m_overflow < 0
                } noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {++m_overflow} noexcept;
                {++m_index} noexcept;
                {++start()} noexcept;
            } && (strictly_positive<T> || requires(T i) {
                {n < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {i > n} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {--i} noexcept;
                {--start()} noexcept;
            }))
            requires (iota_loop<Start, Stop, Step> && requires(T i) {
                {T{}};
                {i < n} -> meta::explicitly_convertible_to<bool>;
                {++i};
                {empty() || m_overflow < 0} -> meta::explicitly_convertible_to<bool>;
                {++m_overflow};
                {++m_index};
                {++start()};
            } && (strictly_positive<T> || requires(T i) {
                {n < 0} -> meta::explicitly_convertible_to<bool>;
                {i > n} -> meta::explicitly_convertible_to<bool>;
                {--i};
                {--start()};
            }))
        {
            if constexpr (strictly_positive<T>) {
                for (T i = {}; i < n; ++i) {
                    if (empty() || m_overflow < 0) {
                        ++m_overflow;
                    } else {
                        ++m_index;
                        ++start();
                    }
                }
            } else {
                if (n < 0) {
                    for (T i = {}; i > n; --i) {
                        if (empty() || m_overflow < 0) {
                            ++m_overflow;
                        } else {
                            ++m_index;
                            --start();
                        }
                    }
                } else {
                    for (T i = {}; i < n; ++i) {
                        if (empty() || m_overflow < 0) {
                            ++m_overflow;
                        } else {
                            ++m_index;
                            ++start();
                        }
                    }
                }
            }
        }

        constexpr void increment_while()
            noexcept (requires{
                {
                    empty() || m_overflow < 0
                } noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {++m_overflow} noexcept;
                {++m_index} noexcept;
                {++start()} noexcept;
                {!empty() && !filter()} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
            })
            requires (iota_filter<Start, Stop, Step> && requires{
                {empty() || m_overflow < 0} -> meta::explicitly_convertible_to<bool>;
                {++m_overflow};
                {++m_index};
                {++start()};
                {!empty() && !filter()} -> meta::explicitly_convertible_to<bool>;
            })
        {
            if (empty() || m_overflow < 0) {
                ++m_overflow;
            } else {
                do {
                    ++m_index;
                    ++start();
                } while (!empty() && !filter());
            }
        }

        constexpr void decrement()
            noexcept (requires{
                {
                    m_index == 0 || m_overflow > 0
                } noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {--m_overflow} noexcept;
                {--start()} noexcept;
                {--m_index} noexcept;
            })
            requires (iota_unit<Start, Step> && requires{
                {m_index == 0 || m_overflow > 0} -> meta::explicitly_convertible_to<bool>;
                {--m_overflow};
                {--start()};
                {--m_index};
            })
        {
            if (m_index == 0 || m_overflow > 0) {
                --m_overflow;
            } else {
                --start();
                --m_index;
            }
        }

        constexpr void decrement_by(difference_type n)
            noexcept (requires {
                {m_index == 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {m_overflow -= n} noexcept;
                {m_overflow > 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {n = -m_overflow} noexcept;
                {m_overflow = 0} noexcept;
                {n > m_index} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {start() -= m_index} noexcept;
                {m_overflow = m_index - n} noexcept;
                {m_index = 0} noexcept;
                {m_index -= n} noexcept;
            })
            requires (iota_jump<Start, Stop, Step> && requires {
                {m_index == 0} -> meta::explicitly_convertible_to<bool>;
                {m_overflow -= n};
                {m_overflow > 0} -> meta::explicitly_convertible_to<bool>;
                {n = -m_overflow};
                {m_overflow = 0};
                {n > m_index} -> meta::explicitly_convertible_to<bool>;
                {start() -= m_index};
                {m_overflow = m_index - n};
                {m_index = 0};
                {m_index -= n};
            })
        {
            if (m_index == 0) {
                m_overflow -= n;
                return;
            }
            if (m_overflow > 0) {
                m_overflow -= n;
                if (m_overflow > 0) {
                    return;
                }
                n = -m_overflow;
                m_overflow = 0;
            }
            if (n > m_index) {
                start() -= m_index;
                m_overflow = m_index - n;
                m_index = 0;
            } else {
                start() -= n;
                m_index -= n;
            }
        }

        template <meta::integer T>
        constexpr void decrement_for(const T& n)
            noexcept (requires(T i) {
                {T{}} noexcept;
                {i < n} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {++i} noexcept;
                {
                    m_index == 0 || m_overflow > 0
                } noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {--m_overflow} noexcept;
                {--m_index} noexcept;
                {--start()} noexcept;
            } && (strictly_positive<T> || requires(T i) {
                {n < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {i > n} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {--i} noexcept;
                {++start()} noexcept;
            }))
            requires (iota_loop<Start, Stop, Step> && requires(T i) {
                {T{}};
                {i < n} -> meta::explicitly_convertible_to<bool>;
                {++i};
                {m_index == 0 || m_overflow > 0} -> meta::explicitly_convertible_to<bool>;
                {--m_overflow};
                {--m_index};
                {--start()};
            } && (strictly_positive<T> || requires(T i) {
                {n < 0} -> meta::explicitly_convertible_to<bool>;
                {i > n} -> meta::explicitly_convertible_to<bool>;
                {--i};
                {++start()};
            }))
        {
            if constexpr (strictly_positive<T>) {
                for (T i = {}; i < n; ++i) {
                    if (m_index == 0 || m_overflow > 0) {
                        --m_overflow;
                    } else {
                        --m_index;
                        --start();
                    }
                }
            } else {
                if (n < 0) {
                    for (T i = {}; i > n; --i) {
                        if (m_index == 0 || m_overflow > 0) {
                            --m_overflow;
                        } else {
                            --m_index;
                            ++start();
                        }
                    }
                } else {
                    for (T i = {}; i < n; ++i) {
                        if (m_index == 0 || m_overflow > 0) {
                            --m_overflow;
                        } else {
                            --m_index;
                            --start();
                        }
                    }
                }
            }
        }

        constexpr void decrement_while()
            noexcept (requires{
                {
                    m_index == 0 || m_overflow > 0
                } noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {--m_overflow} noexcept;
                {--m_index} noexcept;
                {--start()} noexcept;
                {
                    m_index >= 0 && !filter()
                } noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
            })
            requires (iota_filter<Start, Stop, Step> && requires{
                {m_index == 0 || m_overflow < 0} -> meta::explicitly_convertible_to<bool>;
                {--m_overflow};
                {--m_index};
                {--start()};
                {m_index >= 0 && !filter()} -> meta::explicitly_convertible_to<bool>;
            })
        {
            if (m_index == 0 || m_overflow > 0) {
                --m_overflow;
            } else {
                do {
                    --m_index;
                    --start();
                } while (m_index >= 0 && !filter());
            }
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{copy{start(), m_stop, m_step, m_index, m_overflow}} noexcept;})
            requires (requires{{copy{start(), m_stop, m_step, m_index, m_overflow}};})
        {
            return copy{start(), m_stop, m_step, m_index, m_overflow};
        }

        [[nodiscard]] constexpr auto begin() &&
            noexcept (requires{{copy{
                std::move(start()),
                std::move(m_stop),
                std::move(m_step),
                std::move(m_index),
                std::move(m_overflow)
            }} noexcept;})
            requires (requires{{copy{
                std::move(start()),
                std::move(m_stop),
                std::move(m_step),
                std::move(m_index),
                std::move(m_overflow)
            }};})
        {
            return copy{
                std::move(start()),
                std::move(m_stop),
                std::move(m_step),
                std::move(m_index),
                std::move(m_overflow)
            };
        }
    };

    /* A range adaptor that yields successive values in the interval `[start, stop)`,
    incrementing by `step` on each iteration.

    This class backs the 2- and 3-argument forms of `range()` via CTAD guides, and
    effectively replaces all of the following STL constructs:

        1.  `std::ranges::iota(start)` -> `range(start, {})`, which represents an
            infinite range beginning at `start`.
        2.  `std::ranges::iota(start, stop)` -> `range(start, stop)`, which represents
            a half-open range beginning at `start` and ending just before `stop`, using
            `++start` to obtain the next value, and `start == stop` to check for
            termination.
        3.  `std::ranges::subrange(begin, end)` -> `range(begin, end)`, which behaves
            just like (2), but uses iterators instead of values.
        4.  `std::ranges::counted(begin, count)` -> `range(begin, count)`, where
            `begin` is an iterator and `count` is a positive integer.

    If `step` is given, then `start` may initially be less than `stop`, and
    `start += step` will be used to obtain the next value if possible.  If no such
    operator is available, then `++start` or `--start` will be called in a loop
    depending on the sign of `step`.

    Both `stop` and `step` may also be given as function objects, which will be called
    with the current value and must return a boolean.  For `stop`, a return value of
    `true` indicates that the range should terminate at this index.  For `step`, it
    indicates that the current value should be included in the range, and `false`
    indicates that it should be skipped.  This form of `step` therefore acts as a
    filter predicate, and may also be given as a `range` yielding boolean values, which
    act as a mask.

    Ranges of this form expose `size()` and `ssize()` methods as long as
    `(stop - start) / step` is a valid expression yielding an integer-like type, and
    will also support indexing via the subscript operator in that case.  Iterating
    over the range equates to copying the current `start` value and incrementing it for
    each iteration, with the iterators always being totally ordered with respect to one
    another and the range modeling `std::borrowed_range`.  If `start` is also
    decrementable, then the iterators will model `std::bidirectional_iterator`.  If
    `start` supports random-access addition and subtraction with the step size, then
    the iterators will model `std::random_access_iterator` as well.  Since the `end()`
    iterator is an empty sentinel, the range will never model `std::common_range`.

    Interval ranges such as this may also be modified in-place using the `advance()`
    and `retreat()` methods, which behave identically to the increment and decrement
    operators for the iterators.  These methods can be accessed via the `->` operator
    on the range object itself, along with `start()`, `stop()`, `step()`, and `curr()`,
    which obtain the corresponding initializers (if any) and the current value.
    `stop()` and `step()` may be disabled if the range is infinite or lacks a step
    size.  `data()` is also provided if `start` models `std::contiguous_iterator` and
    no step size is given.

    Finally, these ranges are designed to avoid undefined behavior at all times, making
    them safe to use in constant-evaluation contexts.  In particular, the `start` value
    is guaranteed to never exceed `stop` under any circumstances, nor will it be
    decremented below its initial value.  Additional indices are used to track this,
    which are publicly accessible via the `index()` method, which reports the current
    position with respect to the initial value.  This index is zero for the initial
    value, and increases or decreases by `step` each time the range is advanced or
    retreated.  The `start` value will not be modified unless the index is valid.  Note
    that this adds a small amount of iteration overhead, but is necessary to ensure
    safety in all cases. */
    template <meta::not_rvalue Start, meta::not_rvalue Stop, meta::not_rvalue Step>
        requires (iota_concept<Start, Stop, Step>)
    struct iota : iota_tag {
        using start_type = Start;
        using stop_type = Stop;
        using step_type = Step;
        using iterator_category = iota_category<Start, Stop, Step>::type;
        using difference_type = iota_difference<Start, Stop>::type;
        using value_type = iota_value_type<Start>::type;
        using reference = iota_reference<Start>::type;
        using pointer = iota_pointer<Start>::type;
        using size_type = meta::as_unsigned<difference_type>;

        /// TODO: ordered -> bounded, filtered -> filter

        // static constexpr bool subrange = iota_subrange<Start>;
        // static constexpr bool infinite = iota_infinite<Stop>;
        // static constexpr bool ordered = iota_ordered<Start, Stop, Step>;
        // static constexpr bool exact = iota_exact<Start, Stop, Step>;
        // static constexpr bool counted = iota_counted<Start, Stop, Step>;
        // static constexpr bool conditional = iota_conditional<Start, Stop, Step>;
        // static constexpr bool unit = iota_unit<Start, Step>;
        // static constexpr bool jump = iota_jump<Start, Stop, Step>;
        // static constexpr bool loop = iota_loop<Start, Stop, Step>;
        // static constexpr bool filtered = iota_filter<Start, Stop, Step>;
        // static constexpr bool mask = iota_mask<Start, Stop, Step>;

    private:
        using copy = iota<meta::unqualify<Start>, Stop, Step>;
        using storage = iota_storage<Start, Stop, Step>;

        [[no_unique_address]] storage m_storage;

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) subscript(this Self&& self, difference_type n)
            noexcept (requires{{std::forward<Self>(self).m_storage.subscript(n)} noexcept;})
            requires (requires{{std::forward<Self>(self).m_storage.subscript(n)};})
        {
            return std::forward<Self>(self).m_storage.subscript(n);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto subscript(this Self&& self, difference_type n)
            noexcept (requires(copy tmp) {
                {std::forward<Self>(self).begin()} noexcept;
                {tmp += n} noexcept;
                {tmp.curr()} noexcept -> meta::nothrow::copyable;
            })
            requires (
                !requires{{std::forward<Self>(self).m_storage.subscript(n)};} &&
                requires(copy tmp) {
                    {std::forward<Self>(self).begin()};
                    {tmp += n};
                    {tmp.curr()} -> meta::copyable;
                }
            )
        {
            copy tmp = std::forward<Self>(self).begin();
            tmp += n;
            return tmp.curr();
        }

        constexpr void assert_positive_count()
            noexcept (
                !DEBUG ||
                !iota_counted<Start, Stop, Step> ||
                !requires{{m_storage.stop() < 0} -> meta::explicitly_convertible_to<bool>;}
            )
        {
            if constexpr (
                DEBUG &&
                iota_counted<Start, Stop, Step> &&
                requires{{m_storage.stop() < 0} -> meta::explicitly_convertible_to<bool>;}
            ) {
                if (m_storage.stop() < 0) {
                    throw iota_negative_count();
                }
            }
        }

        constexpr void assert_nonzero_step()
            noexcept (
                !DEBUG ||
                !requires{{m_storage.step() == 0} -> meta::explicitly_convertible_to<bool>;}
            )
        {
            if constexpr (
                DEBUG &&
                requires{{m_storage.step() == 0} -> meta::explicitly_convertible_to<bool>;}
            ) {
                if (m_storage.step() == 0) {
                    throw iota_zero_step();
                }
            }
        }

    public:
        [[nodiscard]] constexpr iota() = default;
        [[nodiscard]] constexpr iota(
            meta::forward<Start> start,
            meta::forward<Stop> stop,
            meta::forward<Step> step = {}
        )
            noexcept (requires{{storage{
                std::forward<Start>(start),
                std::forward<Stop>(stop),
                std::forward<Step>(step)
            }} noexcept;} && (
                !requires{{m_storage.first_filtered()};} ||
                requires{{m_storage.first_filtered()} noexcept;}
            ))
            requires (requires{{storage{
                std::forward<Start>(start),
                std::forward<Stop>(stop),
                std::forward<Step>(step)
            }};})
        :
            m_storage{
                std::forward<Start>(start),
                std::forward<Stop>(stop),
                std::forward<Step>(step)
            }
        {
            assert_positive_count();
            assert_nonzero_step();
            if constexpr (requires{{m_storage.first_filtered()};}) {
                m_storage.first_filtered();
            }
        }
        [[nodiscard]] constexpr iota(const storage& store)
            noexcept (requires{{storage{store}} noexcept;})
            requires (requires{{storage{store}};})
        :
            m_storage{store}
        {}
        [[nodiscard]] constexpr iota(storage&& store)
            noexcept (requires{{storage{std::move(store)}} noexcept;})
            requires (requires{{storage{std::move(store)}};})
        :
            m_storage{std::move(store)}
        {}

        constexpr void swap(iota& other)
            noexcept (requires{{m_storage.swap(other.m_storage)} noexcept;})
            requires (requires{{m_storage.swap(other.m_storage)};})
        {
            m_storage.swap(other.m_storage);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) start(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).m_storage.start()} noexcept;})
            requires (requires{{std::forward<Self>(self).m_storage.start()};})
        {
            return (std::forward<Self>(self).m_storage.start());
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) stop(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).m_storage.stop()} noexcept;})
            requires (!iota_infinite<Stop> && requires{
                {std::forward<Self>(self).m_storage.stop()};
            })
        {
            return (std::forward<Self>(self).m_storage.stop());
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) step(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).m_storage.step()} noexcept;})
            requires (!iota_unit<Start, Stop> && requires{
                {std::forward<Self>(self).m_storage.step()};
            })
        {
            return (std::forward<Self>(self).m_storage.step());
        }

        /// TODO: in the masked case, I'll need to wrap the step value in a proxy
        /// type that also stores the begin and end iterators, and forward the
        /// step() accessor accordingly.
        /// -> This can be a recursive iota which represents a subrange over the
        /// step range, which gets stored as a hidden `impl::ref` member, within the
        /// step proxy.

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) curr(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).m_storage.curr()} noexcept;})
            requires (requires{{std::forward<Self>(self).m_storage.curr()};})
        {
            return (std::forward<Self>(self).m_storage.curr());
        }

        [[nodiscard]] constexpr difference_type index() const
            noexcept (requires{{m_storage.index()} noexcept;})
            requires (requires{{m_storage.index()};})
        {
            return m_storage.index();
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{m_storage.empty()} noexcept;})
            requires (requires{{m_storage.empty()};})
        {
            return m_storage.empty();
        }

        [[nodiscard]] constexpr difference_type ssize() const
            noexcept (requires{
                {m_storage.remaining()} noexcept -> meta::integer;
                {m_storage.remaining()} noexcept -> meta::nothrow::convertible_to<difference_type>;
            } || requires{
                {difference_type(math::div::ceil<
                    meta::unqualify<decltype(m_storage.remaining())>,
                    int
                >{}(m_storage.remaining(), 1))} noexcept;
            })
            requires (!iota_infinite<Stop> && iota_unit<Start, Step> && (requires{
                {m_storage.remaining()} -> meta::integer;
                {m_storage.remaining()} -> meta::convertible_to<difference_type>;
            } || requires{
                {difference_type(math::div::ceil<
                    meta::unqualify<decltype(m_storage.remaining())>,
                    int
                >{}(m_storage.remaining(), 1))};
            }))
        {
            if constexpr (requires{
                {m_storage.remaining()} -> meta::integer;
                {m_storage.remaining()} -> meta::convertible_to<difference_type>;
            }) {
                return m_storage.remaining();
            } else {
                return difference_type(math::div::ceil<
                    meta::unqualify<decltype(m_storage.remaining())>,
                    int
                >{}(m_storage.remaining(), 1));
            }
        }

        [[nodiscard]] constexpr difference_type ssize() const
            noexcept (requires{{
                difference_type(math::div::ceil<
                    meta::unqualify<decltype(m_storage.remaining())>,
                    meta::unqualify<meta::as_const_ref<Step>>
                >{}(m_storage.remaining(), step()))
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (!iota_infinite<Stop> && !iota_unit<Start, Step> && requires{
                {difference_type(math::div::ceil<
                    meta::unqualify<decltype(m_storage.remaining())>,
                    meta::unqualify<meta::as_const_ref<Step>>
                >{}(m_storage.remaining(), step()))} -> meta::convertible_to<difference_type>;
            })
        {
            return difference_type(math::div::ceil<
                meta::unqualify<decltype(m_storage.remaining())>,
                meta::unqualify<meta::as_const_ref<Step>>
            >{}(m_storage.remaining(), step()));
        }

        [[nodiscard]] constexpr size_type size() const
            noexcept (requires{{size_type(ssize())} noexcept;})
            requires (requires{{size_type(ssize())};})
        {
            return size_type(ssize());
        }

        [[nodiscard]] constexpr auto data()
            noexcept (requires{{std::addressof(curr())} noexcept;})
            requires (
                meta::contiguous_iterator<Start> &&
                iota_unit<Start, Step> &&
                requires{
                    {std::addressof(curr())} -> meta::pointer;
                    {size()};
                }
            )
        {
            return std::addressof(curr());
        }

        [[nodiscard]] constexpr auto data() const
            noexcept (requires{{std::addressof(curr())} noexcept;})
            requires (
                meta::contiguous_iterator<Start> &&
                iota_unit<Start, Step> &&
                requires{
                    {std::addressof(curr())} -> meta::pointer;
                    {size()};
                }
            )
        {
            return std::addressof(curr());
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{copy{m_storage.begin()}} noexcept;})
            requires (requires{{copy{m_storage.begin()}};})
        {
            return copy{m_storage.begin()};
        }

        [[nodiscard]] constexpr auto begin() &&
            noexcept (requires{{copy{std::move(m_storage).begin()}} noexcept;})
            requires (requires{{copy{std::move(m_storage).begin()}};})
        {
            return copy{std::move(m_storage).begin()};
        }

        [[nodiscard]] static constexpr NoneType end() noexcept {
            return {};
        }

        constexpr void increment()
            noexcept (requires{{m_storage.increment()} noexcept;})
            requires (
                iota_unit<Start, Step> &&
                requires{{m_storage.increment()};}
            )
        {
            m_storage.increment();
        }

        constexpr void increment()
            noexcept (requires{{m_storage.increment_by(step())} noexcept;})
            requires (
                !iota_unit<Start, Step> &&
                iota_jump<Start, Stop, Step> &&
                requires{{m_storage.increment_by(step())};}
            )
        {
            m_storage.increment_by(step());
        }

        constexpr void increment()
            noexcept (requires{{m_storage.increment_for(step())} noexcept;})
            requires (
                !iota_unit<Start, Step> &&
                !iota_jump<Start, Stop, Step> &&
                iota_loop<Start, Stop, Step> &&
                requires{{m_storage.increment_for(step())};}
            )
        {
            m_storage.increment_for(step());
        }

        constexpr void increment()
            noexcept (requires{{m_storage.increment_while()} noexcept;})
            requires (
                !iota_unit<Start, Step> &&
                !iota_jump<Start, Stop, Step> &&
                !iota_loop<Start, Stop, Step> &&
                iota_filter<Start, Stop, Step> &&
                requires{{m_storage.increment_while()};}
            )
        {
            m_storage.increment_while();
        }

        constexpr void increment(difference_type n)
            noexcept ((meta::is<Step, iota_tag> && requires{
                {n < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {m_storage.decrement_by(-n)} noexcept;
                {m_storage.increment_by(n)} noexcept;
            }) || (!meta::is<Step, iota_tag> && requires{
                {n < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {m_storage.decrement_by(-n * step())} noexcept;
                {m_storage.increment_by(n * step())} noexcept;
            }))
            requires (iota_jump<Start, Stop, Step> && (
                (meta::is<Step, iota_tag> && requires{
                    {n < 0} -> meta::explicitly_convertible_to<bool>;
                    {m_storage.decrement_by(-n)};
                    {m_storage.increment_by(n)};
                }) || (!meta::is<Step, iota_tag> && requires{
                    {n < 0} -> meta::explicitly_convertible_to<bool>;
                    {m_storage.decrement_by(-n * step())};
                    {m_storage.increment_by(n * step())};
                })
            ))
        {
            if constexpr (meta::is<Step, iota_tag>) {
                if (n < 0) {
                    m_storage.decrement_by(-n);
                } else {
                    m_storage.increment_by(n);
                }
            } else {
                if (n < 0) {
                    m_storage.decrement_by(-n * step());
                } else {
                    m_storage.increment_by(n * step());
                }
            }
        }

        constexpr void increment(difference_type n)
            noexcept ((meta::is<Step, iota_tag> && requires{
                {n < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {m_storage.decrement_for(-n)} noexcept;
                {m_storage.increment_for(n)} noexcept;
            }) || (!meta::is<Step, iota_tag> && requires{
                {n < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {m_storage.decrement_for(-n * step())} noexcept;
                {m_storage.increment_for(n * step())} noexcept;
            }))
            requires (!iota_jump<Start, Stop, Step> && iota_loop<Start, Stop, Step> && (
                (meta::is<Step, iota_tag> && requires{
                    {n < 0} -> meta::explicitly_convertible_to<bool>;
                    {m_storage.decrement_for(-n)};
                    {m_storage.increment_for(n)};
                }) || (!meta::is<Step, iota_tag> && requires{
                    {n < 0} -> meta::explicitly_convertible_to<bool>;
                    {m_storage.decrement_for(-n * step())};
                    {m_storage.increment_for(n * step())};
                })
            ))
        {
            if constexpr (meta::is<Step, iota_tag>) {
                if (n < 0) {
                    m_storage.decrement_for(-n);
                } else {
                    m_storage.increment_for(n);
                }
            } else {
                if (n < 0) {
                    m_storage.decrement_for(-n * step());
                } else {
                    m_storage.increment_for(n * step());
                }
            }
        }

        constexpr void decrement()
            noexcept (requires{{m_storage.decrement()} noexcept;})
            requires (
                iota_unit<Start, Step> &&
                requires{{m_storage.decrement()};}
            )
        {
            m_storage.decrement();
        }

        constexpr void decrement()
            noexcept (requires{{m_storage.decrement_by(step())} noexcept;})
            requires (
                !iota_unit<Start, Step> &&
                iota_jump<Start, Stop, Step> &&
                requires{{m_storage.decrement_by(step())};}
            )
        {
            m_storage.decrement_by(step());
        }

        constexpr void decrement()
            noexcept (requires{{m_storage.decrement_for(step())} noexcept;})
            requires (
                !iota_unit<Start, Step> &&
                !iota_jump<Start, Stop, Step> &&
                iota_loop<Start, Stop, Step> &&
                requires{{m_storage.decrement_for(step())};}
            )
        {
            m_storage.decrement_for(step());
        }

        constexpr void decrement()
            noexcept (requires{{m_storage.decrement_while()} noexcept;})
            requires (
                !iota_unit<Start, Step> &&
                !iota_jump<Start, Stop, Step> &&
                !iota_loop<Start, Stop, Step> &&
                iota_filter<Start, Stop, Step> &&
                requires{{m_storage.decrement_while()};}
            )
        {
            m_storage.decrement_while();
        }

        constexpr void decrement(difference_type n)
            noexcept ((meta::is<Step, iota_tag> && requires{
                {n < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {m_storage.increment_by(-n)} noexcept;
                {m_storage.decrement_by(n)} noexcept;
            }) || (!meta::is<Step, iota_tag> && requires{
                {n < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {m_storage.increment_by(-n * step())} noexcept;
                {m_storage.decrement_by(n * step())} noexcept;
            }))
            requires (iota_jump<Start, Stop, Step> && (
                (meta::is<Step, iota_tag> && requires{
                    {n < 0} -> meta::explicitly_convertible_to<bool>;
                    {m_storage.increment_by(-n)};
                    {m_storage.decrement_by(n)};
                }) || (!meta::is<Step, iota_tag> && requires{
                    {n < 0} -> meta::explicitly_convertible_to<bool>;
                    {m_storage.increment_by(-n * step())};
                    {m_storage.decrement_by(n * step())};
                })
            ))
        {
            if constexpr (meta::is<Step, iota_tag>) {
                if (n < 0) {
                    m_storage.increment_by(-n);
                } else {
                    m_storage.decrement_by(n);
                }
            } else {
                if (n < 0) {
                    m_storage.increment_by(-n * step());
                } else {
                    m_storage.decrement_by(n * step());
                }
            }
        }

        constexpr void decrement(difference_type n)
            noexcept ((meta::is<Step, iota_tag> && requires{
                {n < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {m_storage.increment_for(-n)} noexcept;
                {m_storage.decrement_for(n)} noexcept;
            }) || (!meta::is<Step, iota_tag> && requires{
                {n < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {m_storage.increment_for(-n * step())} noexcept;
                {m_storage.decrement_for(n * step())} noexcept;
            }))
            requires (!iota_jump<Start, Stop, Step> && iota_loop<Start, Stop, Step> && (
                (meta::is<Step, iota_tag> && requires{
                    {n < 0} -> meta::explicitly_convertible_to<bool>;
                    {m_storage.increment_for(-n)};
                    {m_storage.decrement_for(n)};
                }) || (!meta::is<Step, iota_tag> && requires{
                    {n < 0} -> meta::explicitly_convertible_to<bool>;
                    {m_storage.increment_for(-n * step())};
                    {m_storage.decrement_for(n * step())};
                })
            ))
        {
            if constexpr (meta::is<Step, iota_tag>) {
                if (n < 0) {
                    m_storage.increment_for(-n);
                } else {
                    m_storage.decrement_for(n);
                }
            } else {
                if (n < 0) {
                    m_storage.increment_for(-n * step());
                } else {
                    m_storage.decrement_for(n * step());
                }
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator*(this Self&& self)
            noexcept (requires(difference_type i) {{
                std::forward<Self>(self).curr()
            } noexcept -> meta::nothrow::convertible_to<meta::common_type<
                meta::remove_rvalue<decltype((std::forward<Self>(self).curr()))>,
                meta::remove_rvalue<decltype((std::forward<Self>(self).subscript(i)))>
            >>;})
            -> meta::common_type<
                meta::remove_rvalue<decltype((std::forward<Self>(self).curr()))>,
                meta::remove_rvalue<decltype((std::forward<Self>(self).subscript(
                    std::declval<difference_type>())
                ))>
            >
            requires (requires(difference_type i) {
                {std::forward<Self>(self).subscript(i)};
                {
                    std::forward<Self>(self).curr()
                } -> meta::convertible_to<meta::common_type<
                    meta::remove_rvalue<decltype((std::forward<Self>(self).curr()))>,
                    meta::remove_rvalue<decltype((std::forward<Self>(self).subscript(i)))>
                >>;
            })
        {
            return std::forward<Self>(self).curr();
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).curr()} noexcept;})
            requires (
                !requires(difference_type i) {
                    {std::forward<Self>(self).subscript(i)};
                } &&
                requires{{std::forward<Self>(self).curr()};}
            )
        {
            return (std::forward<Self>(self).curr());
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{impl::arrow{*std::forward<Self>(self)}} noexcept;})
            requires (requires{{impl::arrow{*std::forward<Self>(self)}};})
        {
            return impl::arrow{*std::forward<Self>(self)};
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator[](this Self&& self, difference_type i)
            noexcept (requires{{
                std::forward<Self>(self).subscript(i)
            } noexcept -> meta::nothrow::convertible_to<meta::common_type<
                meta::remove_rvalue<decltype((std::forward<Self>(self).curr()))>,
                meta::remove_rvalue<decltype((std::forward<Self>(self).subscript(i)))>
            >>;})
            -> meta::common_type<
                meta::remove_rvalue<decltype((std::forward<Self>(self).curr()))>,
                meta::remove_rvalue<decltype((std::forward<Self>(self).subscript(i)))>
            >
            requires (requires{
                {std::forward<Self>(self).curr()};
                {
                    std::forward<Self>(self).subscript(i)
                } -> meta::convertible_to<meta::common_type<
                    meta::remove_rvalue<decltype((std::forward<Self>(self).curr()))>,
                    meta::remove_rvalue<decltype((std::forward<Self>(self).subscript(i)))>
                >>;
            })
        {
            return std::forward<Self>(self).subscript(i);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, difference_type i)
            noexcept (requires{{std::forward<Self>(self).subscript(i)} noexcept;})
            requires (
                !requires{{std::forward<Self>(self).curr()};} &&
                requires{{std::forward<Self>(self).subscript(i)};}
            )
        {
            return (std::forward<Self>(self).subscript(i));
        }

        constexpr iota& operator++()
            noexcept (requires{{increment()} noexcept;})
            requires (requires{{increment()};})
        {
            increment();
            return *this;
        }

        [[nodiscard]] constexpr copy operator++(int)
            noexcept (requires{
                {begin()} noexcept;
                {++*this} noexcept;
            })
            requires (requires{
                {begin()};
                {++*this};
            })
        {
            copy tmp = begin();
            ++*this;
            return tmp;
        }

        constexpr iota& operator+=(difference_type i)
            noexcept (requires{{increment(i)} noexcept;})
            requires (requires{{increment(i)};})
        {
            increment(i);
            return *this;
        }

        [[nodiscard]] constexpr copy operator+(difference_type i) const
            noexcept (requires(copy tmp) {
                {begin()} noexcept;
                {tmp += i} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {tmp += i};
            })
        {
            copy tmp = begin();
            tmp += i;
            return tmp;
        }

        constexpr iota& operator--()
            noexcept (requires{{decrement()} noexcept;})
            requires (requires{{decrement()};})
        {
            decrement();
            return *this;
        }

        [[nodiscard]] constexpr copy operator--(int)
            noexcept (requires{
                {begin()} noexcept;
                {--*this} noexcept;
            })
            requires (requires{
                {begin()};
                {--*this};
            })
        {
            copy tmp = begin();
            --*this;
            return tmp;
        }

        constexpr iota& operator-=(difference_type i)
            noexcept (requires{{decrement(i)} noexcept;})
            requires (requires{{decrement(i)};})
        {
            decrement(i);
            return *this;
        }

        [[nodiscard]] constexpr copy operator-(difference_type i) const
            noexcept (requires(copy tmp) {
                {begin()} noexcept;
                {tmp -= i} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {tmp -= i};
            })
        {
            copy tmp = begin();
            tmp -= i;
            return tmp;
        }

        /// TODO: fix this distance operator

        // template <meta::is<Start> T>
        // [[nodiscard]] constexpr difference_type operator-(const iota<T, Stop, Step>& other) const
        //     noexcept (
        //         (has_step && requires{
        //             {step() < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
        //             {
        //                 (index() - other.index()) / -step()
        //             } noexcept -> meta::nothrow::convertible_to<difference_type>;
        //             {
        //                 (index() - other.index()) / step()
        //             } noexcept -> meta::nothrow::convertible_to<difference_type>;
        //         }) || (!has_step && requires{
        //             {
        //                 index() - other.index()
        //             } noexcept -> meta::nothrow::convertible_to<difference_type>;
        //         })
        //     )
        //     requires (
        //         (has_step && requires{
        //             {step() < 0} -> meta::explicitly_convertible_to<bool>;
        //             {(index() - other.index()) / -step()} -> meta::convertible_to<difference_type>;
        //             {(index() - other.index()) / step()} -> meta::convertible_to<difference_type>;
        //         }) || (!has_step && requires{
        //             {index() - other.index()} -> meta::convertible_to<difference_type>;
        //         })
        //     )
        // {
        //     if constexpr (has_step) {
        //         if (step() < 0) {
        //             return (index() - other.index()) / -step();
        //         } else {
        //             return (index() - other.index()) / step();
        //         }
        //     } else {
        //         return index() - other.index();
        //     }
        // }

        [[nodiscard]] friend constexpr difference_type operator-(const iota& self, NoneType)
            noexcept (requires{
                {-self.ssize()} noexcept -> meta::nothrow::convertible_to<difference_type>;
            })
            requires (requires{
                {-self.ssize()} -> meta::nothrow::convertible_to<difference_type>;
            })
        {
            return -self.ssize();
        }

        [[nodiscard]] friend constexpr difference_type operator-(NoneType, const iota& self)
            noexcept (requires{{self.ssize()} noexcept;})
            requires (requires{{self.ssize()};})
        {
            return self.ssize();
        }

        template <meta::is<Start> T>
        [[nodiscard]] constexpr bool operator==(const iota<T, Stop, Step>& other) const
            noexcept (requires{{index() == other.index()} noexcept;})
        {
            return index() == other.index();
        }

        template <meta::is<Start> T>
        [[nodiscard]] constexpr auto operator<=>(const iota<T, Stop, Step>& other) const
            noexcept (requires{{index() <=> other.index()} noexcept;})
        {
            return index() <=> other.index();
        }

        [[nodiscard]] friend constexpr bool operator<(const iota& self, NoneType)
            noexcept (requires{{!self.empty()} noexcept;})
        {
            return !self.empty();
        }

        [[nodiscard]] friend constexpr bool operator<(NoneType, const iota& self) noexcept {
            return false;
        }

        [[nodiscard]] friend constexpr bool operator<=(const iota& self, NoneType) noexcept {
            return true;
        }

        [[nodiscard]] friend constexpr bool operator<=(NoneType, const iota& self)
            noexcept (requires{{self.empty()} noexcept;})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator==(const iota& self, NoneType)
            noexcept (requires{{self.empty()} noexcept;})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator==(NoneType, const iota& self)
            noexcept (requires{{self.empty()} noexcept;})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator!=(const iota& self, NoneType)
            noexcept (requires{{!self.empty()} noexcept;})
        {
            return !self.empty();
        }

        [[nodiscard]] friend constexpr bool operator!=(NoneType, const iota& self)
            noexcept (requires{{!self.empty()} noexcept;})
        {
            return !self.empty();
        }

        [[nodiscard]] friend constexpr bool operator>=(const iota& self, NoneType)
            noexcept (requires{{self.empty()} noexcept;})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator>=(NoneType, const iota& self)
            noexcept (requires{{self.empty()} noexcept;})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator>(const iota& self, NoneType) noexcept {
            return false;
        }

        [[nodiscard]] friend constexpr bool operator>(NoneType, const iota& self)
            noexcept (requires{{!self.empty()} noexcept;})
        {
            return !self.empty();
        }

        [[nodiscard]] friend constexpr auto operator<=>(const iota& self, NoneType)
            noexcept (requires{{self.empty()} noexcept;})
        {
            return self.empty() ? std::strong_ordering::equal : std::strong_ordering::less;
        }

        [[nodiscard]] friend constexpr auto operator<=>(NoneType, const iota& self)
            noexcept (requires{{self.empty()} noexcept;})
        {
            return self.empty() ? std::strong_ordering::equal : std::strong_ordering::greater;
        }
    };

    template <typename Start, typename Stop = iota_tag, typename Step = iota_tag>
    iota(Start&&, Stop&&, Step&& = {}) -> iota<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >;

}


namespace iter {

    /* A generalized `swap()` operator that allows any type in the `bertrand::iter`
    namespace that exposes a `.swap()` member method to be used in conjunction with
    `std::ranges::swap()`. */
    template <typename T>
    constexpr void swap(T& lhs, T& rhs)
        noexcept (requires{{lhs.swap(rhs)} noexcept;})
        requires (requires{{lhs.swap(rhs)};})
    {
        lhs.swap(rhs);
    }

    template <meta::not_rvalue C = impl::empty_range>
    struct range;

    template <typename C>
    range(range<C>&) -> range<range<C>&>;

    template <typename C>
    range(const range<C>&) -> range<const range<C>&>;

    template <typename C>
    range(range<C>&&) -> range<range<C>>;

    template <typename C>
    range(C&&) -> range<meta::remove_rvalue<C>>;

    template <typename Start, typename Stop = impl::iota_tag, typename Step = impl::iota_tag>
    range(Start&&, Stop&&, Step&& = {}) -> range<impl::iota<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >>;

}


namespace impl {

    template <meta::not_rvalue T>
    struct range_iterator;

    template <typename T>
    constexpr auto make_range_iterator(T&& iter)
        noexcept (requires{{range_iterator<meta::remove_rvalue<T>>{std::forward<T>(iter)}} noexcept;})
        requires (requires{{range_iterator<meta::remove_rvalue<T>>{std::forward<T>(iter)}};})
    {
        return range_iterator<meta::remove_rvalue<T>>{std::forward<T>(iter)};
    }

    template <typename T>
    constexpr bool _is_range_iterator = false;
    template <typename T>
    constexpr bool _is_range_iterator<range_iterator<T>> = true;
    template <typename T>
    concept is_range_iterator = _is_range_iterator<meta::unqualify<T>>;

    template <typename T>
    struct range_iterator_traits {
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = meta::remove_reference<T>;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::as_pointer<value_type>;
    };
    template <meta::iterator T>
    struct range_iterator_traits<T> {
        using iterator_category = meta::iterator_category<T>;
        using difference_type = meta::iterator_difference_type<T>;
        using value_type = meta::iterator_value_type<T>;
        using reference = meta::iterator_reference_type<T>;
        using pointer = meta::iterator_pointer_type<T>;
    };

    /* A wrapper for an iterator over an `iter::range()` object.  This acts as a
    transparent adaptor for the underlying iterator type, and does not change its
    behavior in any way.  The only caveat is for nested ranges, whereby the wrapped
    iterator will be another instance of this class, and the dereference type will be
    promoted to a range. */
    template <meta::not_rvalue T>
    struct range_iterator {
    private:
        using traits = range_iterator_traits<T>;

    public:
        using iterator_category = traits::iterator_category;
        using difference_type = traits::difference_type;
        using value_type = traits::value_type;
        using reference = traits::reference;
        using pointer = traits::pointer;

        T iter;

        [[nodiscard]] constexpr decltype(auto) operator*()
            noexcept (
                (is_range_iterator<T> && requires{{iter::range(*iter)} noexcept;}) ||
                (!is_range_iterator<T> && requires{{*iter} noexcept;})
            )
            requires (
                (is_range_iterator<T> && requires{{iter::range(*iter)};}) ||
                (!is_range_iterator<T> && requires{{*iter};})
            )
        {
            if constexpr (is_range_iterator<T>) {
                return (iter::range(*iter));
            } else {
                return (*iter);
            }
        }

        [[nodiscard]] constexpr decltype(auto) operator*() const
            noexcept (
                (is_range_iterator<T> && requires{{iter::range(*iter)} noexcept;}) ||
                (!is_range_iterator<T> && requires{{*iter} noexcept;})
            )
            requires (
                (is_range_iterator<T> && requires{{iter::range(*iter)};}) ||
                (!is_range_iterator<T> && requires{{*iter};})
            )
        {
            if constexpr (is_range_iterator<T>) {
                return (iter::range(*iter));
            } else {
                return (*iter);
            }
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{impl::arrow(*iter)} noexcept;})
            requires (requires{{impl::arrow(*iter)};})
        {
            return impl::arrow(*iter);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow(*iter)} noexcept;})
            requires (requires{{impl::arrow(*iter)};})
        {
            return impl::arrow(*iter);
        }

        [[nodiscard]] constexpr decltype(auto) operator[](difference_type n)
            noexcept (
                (is_range_iterator<T> && requires{{iter::range(iter[n])} noexcept;}) ||
                (!is_range_iterator<T> && requires{{iter[n]} noexcept;})
            )
            requires (
                (is_range_iterator<T> && requires{{iter::range(iter[n])};}) ||
                (!is_range_iterator<T> && requires{{iter[n]};})
            )
        {
            if constexpr (is_range_iterator<T>) {
                return (iter::range(iter[n]));
            } else {
                return (iter[n]);
            }
        }

        [[nodiscard]] constexpr decltype(auto) operator[](difference_type n) const
            noexcept (
                (is_range_iterator<T> && requires{{iter::range(iter[n])} noexcept;}) ||
                (!is_range_iterator<T> && requires{{iter[n]} noexcept;})
            )
            requires (
                (is_range_iterator<T> && requires{{iter::range(iter[n])};}) ||
                (!is_range_iterator<T> && requires{{iter[n]};})
            )
        {
            if constexpr (is_range_iterator<T>) {
                return (iter::range(iter[n]));
            } else {
                return (iter[n]);
            }
        }

        constexpr range_iterator& operator++()
            noexcept (requires{{++iter} noexcept;})
            requires (requires{{++iter};})
        {
            ++iter;
            return *this;
        }

        [[nodiscard]] range_iterator operator++(int)
            noexcept (requires{{range_iterator{iter++}} noexcept;})
            requires (requires{{range_iterator{iter++}};})
        {
            return {iter++};
        }

        [[nodiscard]] friend constexpr range_iterator operator+(
            const range_iterator& self,
            difference_type n
        ) noexcept (requires{{range_iterator{self.iter + n}} noexcept;})
            requires (requires{{range_iterator{self.iter + n}};})
        {
            return {self.iter + n};
        }

        [[nodiscard]] friend constexpr range_iterator operator+(
            difference_type n,
            const range_iterator& self
        ) noexcept (requires{{range_iterator{self.iter + n}} noexcept;})
            requires (requires{{range_iterator{self.iter + n}};})
        {
            return {self.iter + n};
        }

        constexpr range_iterator& operator+=(difference_type n)
            noexcept (requires{{iter += n} noexcept;})
            requires (requires{{iter += n};})
        {
            iter += n;
            return *this;
        }

        constexpr range_iterator& operator--()
            noexcept (requires{{--iter} noexcept;})
            requires (requires{{--iter};})
        {
            --iter;
            return *this;
        }

        [[nodiscard]] constexpr range_iterator operator--(int)
            noexcept (requires{{range_iterator{iter--}} noexcept;})
            requires (requires{{range_iterator{iter--}};})
        {
            return {iter--};
        }

        [[nodiscard]] constexpr range_iterator operator-(difference_type n) const
            noexcept (requires{{range_iterator{iter - n}} noexcept;})
            requires (requires{{range_iterator{iter - n}};})
        {
            return {iter - n};
        }

        template <typename U>
        [[nodiscard]] constexpr difference_type operator-(const range_iterator<U>& other) const
            noexcept (requires{
                {iter - other.iter} noexcept -> meta::nothrow::convertible_to<difference_type>;
            })
            requires (requires{
                {iter - other.iter} -> meta::convertible_to<difference_type>;
            })
        {
            return iter - other.iter;
        }

        constexpr range_iterator& operator-=(difference_type n)
            noexcept (requires{{iter -= n} noexcept;})
            requires (requires{{iter -= n};})
        {
            iter -= n;
            return *this;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator<(const range_iterator<U>& other) const
            noexcept (requires{
                {iter < other.iter} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {iter < other.iter} -> meta::convertible_to<bool>;
            })
        {
            return iter < other.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator<=(const range_iterator<U>& other) const
            noexcept (requires{
                {iter <= other.iter} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {iter <= other.iter} -> meta::convertible_to<bool>;
            })
        {
            return iter <= other.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator==(const range_iterator<U>& other) const
            noexcept (requires{
                {iter == other.iter} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {iter == other.iter} -> meta::convertible_to<bool>;
            })
        {
            return iter == other.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator!=(const range_iterator<U>& other) const
            noexcept (requires{
                {iter != other.iter} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {iter != other.iter} -> meta::convertible_to<bool>;
            })
        {
            return iter != other.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator>=(const range_iterator<U>& other) const
            noexcept (requires{
                {iter >= other.iter} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {iter >= other.iter} -> meta::convertible_to<bool>;
            })
        {
            return iter >= other.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator>(const range_iterator<U>& other) const
            noexcept (requires{
                {iter > other.iter} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {iter > other.iter} -> meta::convertible_to<bool>;
            })
        {
            return iter > other.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr auto operator<=>(const range_iterator<U>& other) const
            noexcept (requires{{iter <=> other.iter} noexcept;})
            requires (requires{{iter <=> other.iter};})
        {
            return iter <=> other.iter;
        }
    };

    /* A wrapper for an iterator over an `impl::unpack()` object.  This acts as a
    transparent adaptor for the underlying iterator type, and does not change its
    behavior in any way.  The only caveat is for nested ranges, whereby the wrapped
    iterator will be another instance of this class, and the dereference type will be
    promoted to an `unpack` range. */
    template <meta::not_rvalue T>
    struct unpack_iterator;

    template <typename T>
    constexpr auto make_unpack_iterator(T&& iter)
        noexcept (requires{{unpack_iterator<meta::remove_rvalue<T>>{std::forward<T>(iter)}} noexcept;})
        requires (requires{{unpack_iterator<meta::remove_rvalue<T>>{std::forward<T>(iter)}};})
    {
        return unpack_iterator<meta::remove_rvalue<T>>{std::forward<T>(iter)};
    }

    template <typename T>
    constexpr bool _is_unpack_iterator = false;
    template <typename T>
    constexpr bool _is_unpack_iterator<unpack_iterator<T>> = true;
    template <typename T>
    concept is_unpack_iterator = _is_unpack_iterator<meta::unqualify<T>>;

    /* A trivial subclass of `range` that allows the range to be destructured when
    used as an argument to a Bertrand function. */
    template <meta::not_rvalue C> requires (meta::iterable<C>)
    struct unpack : iter::range<C> {
        using iter::range<C>::range;


        /* Get a forward iterator to the start of the range. */
        [[nodiscard]] constexpr auto begin()
            noexcept (requires{
                {impl::make_unpack_iterator(std::ranges::begin(*this->__value))} noexcept;
            })
            requires (requires{
                {impl::make_unpack_iterator(std::ranges::begin(*this->__value))};
            })
        {
            return impl::make_unpack_iterator(std::ranges::begin(*this->__value));
        }

        /* Get a forward iterator to the start of the range. */
        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{
                {impl::make_unpack_iterator(std::ranges::begin(*this->__value))} noexcept;
            })
            requires (requires{
                {impl::make_unpack_iterator(std::ranges::begin(*this->__value))};
            })
        {
            return impl::make_unpack_iterator(std::ranges::begin(*this->__value));
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
            noexcept (requires{
                {impl::make_unpack_iterator(std::ranges::end(*this->__value))} noexcept;
            })
            requires (requires{
                {impl::make_unpack_iterator(std::ranges::end(*this->__value))};
            })
        {
            return impl::make_unpack_iterator(std::ranges::end(*this->__value));
        }

        /* Get a forward iterator to one past the last element of the range. */
        [[nodiscard]] constexpr auto end() const
            noexcept (requires{
                {impl::make_unpack_iterator(std::ranges::end(*this->__value))} noexcept;
            })
            requires (requires{
                {impl::make_unpack_iterator(std::ranges::end(*this->__value))};
            })
        {
            return impl::make_unpack_iterator(std::ranges::end(*this->__value));
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
            noexcept (requires{
                {impl::make_unpack_iterator(std::ranges::rbegin(*this->__value))} noexcept;})
            requires (requires{
                {impl::make_unpack_iterator(std::ranges::rbegin(*this->__value))};})
        {
            return impl::make_unpack_iterator(std::ranges::rbegin(*this->__value));
        }

        /* Get a reverse iterator to the last element of the range. */
        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{
                {impl::make_unpack_iterator(std::ranges::rbegin(*this->__value))} noexcept;})
            requires (requires{
                {impl::make_unpack_iterator(std::ranges::rbegin(*this->__value))};})
        {
            return impl::make_unpack_iterator(std::ranges::rbegin(*this->__value));
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
            noexcept (requires{
                {impl::make_unpack_iterator(std::ranges::rend(*this->__value))} noexcept;
            })
            requires (requires{
                {impl::make_unpack_iterator(std::ranges::rend(*this->__value))};})
        {
            return impl::make_unpack_iterator(std::ranges::rend(*this->__value));
        }

        /* Get a reverse iterator to one before the first element of the range. */
        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{
                {impl::make_unpack_iterator(std::ranges::rend(*this->__value))} noexcept;
            })
            requires (requires{
                {impl::make_unpack_iterator(std::ranges::rend(*this->__value))};})
        {
            return impl::make_unpack_iterator(std::ranges::rend(*this->__value));
        }

        /* Get a reverse iterator to one before the first element of the range. */
        [[nodiscard]] constexpr auto crend() const
            noexcept (requires{{rend()} noexcept;})
            requires (requires{{rend()};})
        {
            return rend();
        }




    };

    template <typename C>
    unpack(unpack<C>&) -> unpack<unpack<C>&>;

    template <typename C>
    unpack(const unpack<C>&) -> unpack<const unpack<C>&>;

    template <typename C>
    unpack(unpack<C>&&) -> unpack<unpack<C>>;

    template <typename C> requires (!meta::unpack<C> && meta::iterable<C>)
    unpack(C&&) -> unpack<meta::remove_rvalue<C>>;

    template <typename C> requires (!meta::iterable<C> && meta::tuple_like<C>)
    unpack(C&&) -> unpack<impl::tuple_range<meta::remove_rvalue<C>>>;

    template <typename C> requires (!meta::iterable<C> && !meta::tuple_like<C>)
    unpack(C&&) -> unpack<impl::single_range<meta::remove_rvalue<C>>>;

    template <meta::not_rvalue T>
    struct unpack_iterator {
    private:
        using traits = range_iterator_traits<T>;

    public:
        using iterator_category = traits::iterator_category;
        using difference_type = traits::difference_type;
        using value_type = traits::value_type;
        using reference = traits::reference;
        using pointer = traits::pointer;

        T iter;

        [[nodiscard]] constexpr decltype(auto) operator*()
            noexcept (
                (is_unpack_iterator<T> && requires{{impl::unpack(*iter)} noexcept;}) ||
                (!is_unpack_iterator<T> && requires{{*iter} noexcept;})
            )
            requires (
                (is_unpack_iterator<T> && requires{{impl::unpack(*iter)};}) ||
                (!is_unpack_iterator<T> && requires{{*iter};})
            )
        {
            if constexpr (is_unpack_iterator<T>) {
                return (impl::unpack(*iter));
            } else {
                return (*iter);
            }
        }

        [[nodiscard]] constexpr decltype(auto) operator*() const
            noexcept (
                (is_unpack_iterator<T> && requires{{impl::unpack(*iter)} noexcept;}) ||
                (!is_unpack_iterator<T> && requires{{*iter} noexcept;})
            )
            requires (
                (is_unpack_iterator<T> && requires{{impl::unpack(*iter)};}) ||
                (!is_unpack_iterator<T> && requires{{*iter};})
            )
        {
            if constexpr (is_unpack_iterator<T>) {
                return (impl::unpack(*iter));
            } else {
                return (*iter);
            }
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{impl::arrow(*iter)} noexcept;})
            requires (requires{{impl::arrow(*iter)};})
        {
            return impl::arrow(*iter);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow(*iter)} noexcept;})
            requires (requires{{impl::arrow(*iter)};})
        {
            return impl::arrow(*iter);
        }

        [[nodiscard]] constexpr decltype(auto) operator[](difference_type n)
            noexcept (
                (is_unpack_iterator<T> && requires{{impl::unpack(iter[n])} noexcept;}) ||
                (!is_unpack_iterator<T> && requires{{iter[n]} noexcept;})
            )
            requires (
                (is_unpack_iterator<T> && requires{{impl::unpack(iter[n])};}) ||
                (!is_unpack_iterator<T> && requires{{iter[n]};})
            )
        {
            if constexpr (is_unpack_iterator<T>) {
                return (impl::unpack(iter[n]));
            } else {
                return (iter[n]);
            }
        }

        [[nodiscard]] constexpr decltype(auto) operator[](difference_type n) const
            noexcept (
                (is_unpack_iterator<T> && requires{{impl::unpack(iter[n])} noexcept;}) ||
                (!is_unpack_iterator<T> && requires{{iter[n]} noexcept;})
            )
            requires (
                (is_unpack_iterator<T> && requires{{impl::unpack(iter[n])};}) ||
                (!is_unpack_iterator<T> && requires{{iter[n]};})
            )
        {
            if constexpr (is_unpack_iterator<T>) {
                return (impl::unpack(iter[n]));
            } else {
                return (iter[n]);
            }
        }

        constexpr unpack_iterator& operator++()
            noexcept (requires{{++iter} noexcept;})
            requires (requires{{++iter};})
        {
            ++iter;
            return *this;
        }

        [[nodiscard]] unpack_iterator operator++(int)
            noexcept (requires{{unpack_iterator{iter++}} noexcept;})
            requires (requires{{unpack_iterator{iter++}};})
        {
            return {iter++};
        }

        [[nodiscard]] friend constexpr unpack_iterator operator+(
            const unpack_iterator& self,
            difference_type n
        ) noexcept (requires{{unpack_iterator{self.iter + n}} noexcept;})
            requires (requires{{unpack_iterator{self.iter + n}};})
        {
            return {self.iter + n};
        }

        [[nodiscard]] friend constexpr unpack_iterator operator+(
            difference_type n,
            const unpack_iterator& self
        ) noexcept (requires{{unpack_iterator{self.iter + n}} noexcept;})
            requires (requires{{unpack_iterator{self.iter + n}};})
        {
            return {self.iter + n};
        }

        constexpr unpack_iterator& operator+=(difference_type n)
            noexcept (requires{{iter += n} noexcept;})
            requires (requires{{iter += n};})
        {
            iter += n;
            return *this;
        }

        constexpr unpack_iterator& operator--()
            noexcept (requires{{--iter} noexcept;})
            requires (requires{{--iter};})
        {
            --iter;
            return *this;
        }

        [[nodiscard]] constexpr unpack_iterator operator--(int)
            noexcept (requires{{unpack_iterator{iter--}} noexcept;})
            requires (requires{{unpack_iterator{iter--}};})
        {
            return {iter--};
        }

        [[nodiscard]] constexpr unpack_iterator operator-(difference_type n) const
            noexcept (requires{{unpack_iterator{iter - n}} noexcept;})
            requires (requires{{unpack_iterator{iter - n}};})
        {
            return {iter - n};
        }

        template <typename U>
        [[nodiscard]] constexpr difference_type operator-(const unpack_iterator<U>& other) const
            noexcept (requires{
                {iter - other.iter} noexcept -> meta::nothrow::convertible_to<difference_type>;
            })
            requires (requires{
                {iter - other.iter} -> meta::convertible_to<difference_type>;
            })
        {
            return iter - other.iter;
        }

        constexpr unpack_iterator& operator-=(difference_type n)
            noexcept (requires{{iter -= n} noexcept;})
            requires (requires{{iter -= n};})
        {
            iter -= n;
            return *this;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator<(const unpack_iterator<U>& other) const
            noexcept (requires{
                {iter < other.iter} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {iter < other.iter} -> meta::convertible_to<bool>;
            })
        {
            return iter < other.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator<=(const unpack_iterator<U>& other) const
            noexcept (requires{
                {iter <= other.iter} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {iter <= other.iter} -> meta::convertible_to<bool>;
            })
        {
            return iter <= other.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator==(const unpack_iterator<U>& other) const
            noexcept (requires{
                {iter == other.iter} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {iter == other.iter} -> meta::convertible_to<bool>;
            })
        {
            return iter == other.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator!=(const unpack_iterator<U>& other) const
            noexcept (requires{
                {iter != other.iter} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {iter != other.iter} -> meta::convertible_to<bool>;
            })
        {
            return iter != other.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator>=(const unpack_iterator<U>& other) const
            noexcept (requires{
                {iter >= other.iter} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {iter >= other.iter} -> meta::convertible_to<bool>;
            })
        {
            return iter >= other.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr bool operator>(const unpack_iterator<U>& other) const
            noexcept (requires{
                {iter > other.iter} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {iter > other.iter} -> meta::convertible_to<bool>;
            })
        {
            return iter > other.iter;
        }

        template <typename U>
        [[nodiscard]] constexpr auto operator<=>(const unpack_iterator<U>& other) const
            noexcept (requires{{iter <=> other.iter} noexcept;})
            requires (requires{{iter <=> other.iter};})
        {
            return iter <=> other.iter;
        }
    };


    /// TODO: all of this crap now needs to be looked at.

    template <typename L, typename R>
    consteval ValueError range_size_mismatch() noexcept {
        static constexpr static_str msg =
            "Size mismatch during range assignment: " + demangle<L>() + " = " + demangle<R>();
        return ValueError(msg);
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

    template <typename... Ts>
    concept at_run_time = (meta::type_identity<Ts> && ...);

    template <typename... Ts>
    concept at_compile_time = (!meta::type_identity<Ts> && ...);

    template <typename... Ts>
    concept at_concept = at_run_time<Ts...> || at_compile_time<Ts...>;

}


namespace iter {

    /* Range-based multidimensional indexing operator.  Accepts either signed integer
    indices with Python-style wraparound or predicate functions, which accept a value
    from the container and return a boolean.  The indexing will stop at the first
    element for which the function returns true.

    This specialization accepts any number of indices as non-type template parameters,
    and applies them to an arbitrary container when called.  If more than one index is
    given, then they will be applied sequentially, equating to a chain of
    `container[i][j][k]...` calls.  If any results support tuple-like indexing, then
    the corresponding access will be performed at compile time instead, yielding an
    exact return type. */
    template <auto... Is> requires (impl::at_concept<decltype(Is)...>)
    struct at {
    private:
        /// TODO: document the internals here, since they are rather complicated.

        template <auto J, typename C>
        static constexpr bool index =
            requires(C c) {{meta::unpack_tuple<J>(std::forward<C>(c))};} ||
            (meta::wraparound<C> && requires(C c) {{std::forward<C>(c)[J]};}) ||
            requires(C c) {{std::forward<C>(c)[
                size_t(impl::normalize_index(std::ranges::ssize(c), J))
            ]};};

        template <auto J, typename C> requires (!meta::range<C> && index<J, C>)
        static constexpr decltype(auto) call(C&& c)
            noexcept (
                requires{{meta::unpack_tuple<J>(std::forward<C>(c))} noexcept;} || (
                !requires{{meta::unpack_tuple<J>(std::forward<C>(c))};} && (
                    (meta::wraparound<C> && requires{{std::forward<C>(c)[J]} noexcept;}) || (
                        (!meta::wraparound<C> || !requires(C c) {{std::forward<C>(c)[J]};}) &&
                        requires{{std::forward<C>(c)[
                            size_t(impl::normalize_index(std::ranges::ssize(c), J))
                        ]} noexcept;}
                    )
                )
            ))
        {
            if constexpr (requires{{meta::unpack_tuple<J>(std::forward<C>(c))};}) {
                return (meta::unpack_tuple<J>(std::forward<C>(c)));
            } else if constexpr (meta::wraparound<C> && requires(C c) {{std::forward<C>(c)[J]};}) {
                return (std::forward<C>(c)[J]);
            } else {
                return (std::forward<C>(c)[
                    size_t(impl::normalize_index(std::ranges::ssize(c), J))
                ]);
            }
        }

        template <size_t N>
        struct destructure {
            template <typename F, meta::tuple_like C>
            static constexpr decltype(auto) operator()(const F& f, C&& c)
                requires (N == meta::tuple_size<C> || (
                    requires {
                        {f(meta::unpack_tuple<N>(std::forward<C>(c)))};
                    } && (N + 1 == meta::tuple_size<C> || requires{
                        {destructure<N + 1>{}(f, std::forward<C>(c))};
                    })
                ))
            {
                if constexpr (N == meta::tuple_size<C>) {
                    /// TODO: think of a better error message, and possibly factor it out
                    /// to reduce binary size.
                    throw ValueError("no matching element found");
                } else {
                    decltype(auto) x = meta::unpack_tuple<N>(std::forward<C>(c));
                    if (f(std::forward<decltype(x)>(x))) {
                        return (std::forward<decltype(x)>(x));
                    }
                    if constexpr (N + 1 == meta::tuple_size<C>) {
                        /// TODO: think of a better error message, and possibly factor it out
                        /// to reduce binary size.
                        throw ValueError("no matching element found");
                    } else {
                        return (destructure<N + 1>{}(f, std::forward<C>(c)));
                    }
                }
            }
        };

        template <auto F, typename C> requires (!meta::range<C> && !index<F, C>)
        static constexpr decltype(auto) call(C&& c)
            requires (requires{{destructure<0>{}(F, std::forward<C>(c))};} || (
                meta::iterable<meta::as_lvalue<C>> &&
                requires(decltype((*std::ranges::begin(c))) x) {
                    {F(std::forward<decltype(x)>(x))} -> meta::convertible_to<bool>;
                    {meta::remove_rvalue<decltype(x)>(std::forward<decltype(x)>(x))};
                }
            ))
        {
            if constexpr (requires{{destructure<0>{}(F, std::forward<C>(c))};}) {
                return (destructure<0>{}(F, std::forward<C>(c)));
            } else {
                for (auto&& x : c) {
                    if (F(std::forward<decltype(x)>(x))) {
                        return (meta::remove_rvalue<decltype(x)>(std::forward<decltype(x)>(x)));
                    }
                }
                /// TODO: think of a better error message.
                throw ValueError("no element found matching predicate");
            }
        }

        template <auto J, meta::range R>
        static constexpr decltype(auto) call(R&& r)
            noexcept (requires{{call<J>(*std::forward<R>(r).__value)} noexcept;})
            requires (
                !meta::range<decltype((*std::forward<R>(r).__value))> &&
                requires{{call<J>(*std::forward<R>(r).__value)};}
            )
        {
            return (call<J>(*std::forward<R>(r).__value));
        }

        template <auto J, meta::range R>
        static constexpr decltype(auto) call(R&& r)
            noexcept (requires{
                {iter::range(call<J>(*std::forward<R>(r).__value))} noexcept;
            })
            requires (
                meta::range<decltype((*std::forward<R>(r).__value))> &&
                requires{{iter::range(call<J>(*std::forward<R>(r).__value))};}
            )
        {
            return (iter::range(call<J>(*std::forward<R>(r).__value)));
        }

        template <auto J, meta::unpack R>
        static constexpr decltype(auto) call(R&& r)
            noexcept (requires{
                {impl::unpack(call<J>(*std::forward<R>(r).__value))} noexcept;
            })
            requires (
                meta::range<decltype((*std::forward<R>(r).__value))> &&
                requires{{impl::unpack(call<J>(*std::forward<R>(r).__value))};}
            )
        {
            return (impl::unpack(call<J>(*std::forward<R>(r).__value)));
        }

        template <auto J, auto... Js>
        struct accumulate {
            template <typename C>
            static constexpr decltype(auto) operator()(C&& c)
                noexcept ((sizeof...(Js) == 0 && requires{
                    {call<J>(std::forward<C>(c))} noexcept;
                }) || (sizeof...(Js) > 0 && requires{
                    {accumulate<Js...>{}(call<J>(std::forward<C>(c)))} noexcept;
                }))
                requires ((sizeof...(Js) == 0 && requires{
                    {call<J>(std::forward<C>(c))};
                }) || (sizeof...(Js) > 0 && requires{
                    {accumulate<Js...>{}(call<J>(std::forward<C>(c)))};
                }))
            {
                if constexpr (sizeof...(Js) == 0) {
                    return (call<J>(std::forward<C>(c)));
                } else {
                    return (accumulate<Js...>{}(call<J>(std::forward<C>(c))));
                }
            }
        };

    public:
        template <typename C>
        static constexpr decltype(auto) operator()(C&& c)
            noexcept (sizeof...(Is) == 0 || requires{
                {accumulate<Is...>{}(std::forward<C>(c))} noexcept;
            })
            requires (sizeof...(Is) == 0 || requires{
                {accumulate<Is...>{}(std::forward<C>(c))};
            })
        {
            if constexpr (sizeof...(Is) == 0) {
                return (std::forward<C>(c));
            } else {
                return (accumulate<Is...>{}(std::forward<C>(c)));
            }
        }
    };

    /* Range-based multidimensional indexing operator.  Accepts either signed integer
    indices with Python-style wraparound or predicate functions, which accept a value
    from the container and return a boolean.  The indexing will stop at the first
    element for which the function returns true.

    This specialization accepts any number of indices as run-time constructor
    arguments, and applies them to an arbitrary container when called.  If more than
    one index is given, then they will be applied sequentially, equating to a chain of
    `container[i][j][k]...` calls.  If the container is tuple-like, but not otherwise
    subscriptable, then the indexing will be done using a vtable dispatch that maps
    indices to the appropriate element, possibly returning a union if multiple result
    types are valid. */
    template <auto... Is>
        requires (impl::at_concept<decltype(Is)...> && impl::at_run_time<decltype(Is)...>)
    struct at<Is...> {
        /// TODO: document the internals here, since they are rather complicated.

        [[no_unique_address]] impl::basic_tuple<typename decltype(Is)::type...> idx;

        [[nodiscard]] constexpr at() = default;
        [[nodiscard]] constexpr at(meta::forward<typename decltype(Is)::type>... args)
            noexcept (meta::nothrow::constructible_from<
                impl::basic_tuple<typename decltype(Is)::type...>,
                meta::forward<typename decltype(Is)::type>...
            >)
            requires (meta::constructible_from<
                impl::basic_tuple<typename decltype(Is)::type...>,
                meta::forward<typename decltype(Is)::type>...
            >)
        :
            idx{std::forward<typename decltype(Is)::type>(args)...}
        {}

    private:
        template <size_t N, typename C>
        static constexpr bool index =
            (meta::wraparound<C> && requires(C c) {{std::forward<C>(c)[idx.template get<N>()]};}) ||
            requires(C c) {{std::forward<C>(c)[
                size_t(impl::normalize_index(std::ranges::ssize(c), idx.template get<N>()))
            ]};} || requires(C c) {{impl::tuple_range(std::forward<C>(c))[
                size_t(impl::normalize_index(meta::tuple_size<C>, idx.template get<N>()))
            ]};};

        template <size_t N, typename C> requires (!meta::range<C> && index<N, C>)
        constexpr decltype(auto) call(C&& c) const
            noexcept ((meta::wraparound<C> && requires{
                {std::forward<C>(c)[idx.template get<N>()]} noexcept;
            }) || (
                (!meta::wraparound<C> || !requires{
                    {std::forward<C>(c)[idx.template get<N>()]};
                }) && (
                    requires{{std::forward<C>(c)[
                        size_t(impl::normalize_index(std::ranges::ssize(c), idx.template get<N>()))
                    ]} noexcept;} || (
                        !requires{{std::forward<C>(c)[
                            size_t(impl::normalize_index(std::ranges::ssize(c), idx.template get<N>()))
                        ]};} && requires{{impl::tuple_range(std::forward<C>(c))[
                            size_t(impl::normalize_index(meta::tuple_size<C>, idx.template get<N>()))
                        ]} noexcept;}
                    )
                )
            ))
        {
            if constexpr (meta::wraparound<C> && requires{
                {std::forward<C>(c)[idx.template get<N>()]};
            }) {
                return (std::forward<C>(c)[idx.template get<N>()]);
            } else if constexpr (requires{{std::forward<C>(c)[
                size_t(impl::normalize_index(std::ranges::ssize(c), idx.template get<N>()))
            ]};}) {
                return (std::forward<C>(c)[
                    size_t(impl::normalize_index(std::ranges::ssize(c), idx.template get<N>()))
                ]);
            } else {
                return (impl::tuple_range(std::forward<C>(c))[
                    size_t(impl::normalize_index(meta::tuple_size<C>, idx.template get<N>()))
                ]);
            }
        }

        template <size_t N, typename C> requires (!meta::range<C> && !index<N, C>)
        constexpr decltype(auto) call(C&& c) const
            requires ((
                meta::iterable<meta::as_lvalue<C>> &&
                requires(
                    decltype((idx.template get<N>())) f,
                    decltype((*std::ranges::begin(c))) x
                ) {
                    {f(std::forward<decltype(x)>(x))} -> meta::convertible_to<bool>;
                    {meta::remove_rvalue<decltype(x)>(std::forward<decltype(x)>(x))};
                }
            ) || (
                !meta::iterable<meta::as_lvalue<C>> &&
                requires(
                    decltype((idx.template get<N>())) f,
                    decltype((*impl::tuple_range(std::forward<C>(c)).begin())) x
                ) {
                    {f(std::forward<decltype(x)>(x))} -> meta::convertible_to<bool>;
                    {meta::remove_rvalue<decltype(x)>(std::forward<decltype(x)>(x))};
                }
            ))
        {
            const auto& f = idx.template get<N>();
            if constexpr (meta::iterable<meta::as_lvalue<C>>) {
                for (auto&& x : c) {
                    if (f(std::forward<decltype(x)>(x))) {
                        return (meta::remove_rvalue<decltype(x)>(std::forward<decltype(x)>(x)));
                    }
                }
            } else {
                auto t = impl::tuple_range(std::forward<C>(c));
                for (auto&& x : t) {
                    if (f(std::forward<decltype(x)>(x))) {
                        return (meta::remove_rvalue<decltype(x)>(std::forward<decltype(x)>(x)));
                    }
                }
            }
            /// TODO: think of a better error message.
            throw ValueError("no element found matching predicate");
        }

        template <size_t N, meta::range R>
        constexpr decltype(auto) call(R&& r) const
            noexcept (requires{{call<N>(*std::forward<R>(r).__value)} noexcept;})
            requires (
                !meta::range<decltype((*std::forward<R>(r).__value))> &&
                requires{{call<N>(*std::forward<R>(r).__value)};}
            )
        {
            return (call<N>(*std::forward<R>(r).__value));
        }

        template <size_t N, meta::range R>
        constexpr decltype(auto) call(R&& r) const
            noexcept (requires{
                {iter::range(call<N>(*std::forward<R>(r).__value))} noexcept;
            })
            requires (
                meta::range<decltype((*std::forward<R>(r).__value))> &&
                requires{{iter::range(call<N>(*std::forward<R>(r).__value))};}
            )
        {
            return (iter::range(call<N>(*std::forward<R>(r).__value)));
        }

        template <size_t N, meta::unpack R>
        constexpr decltype(auto) call(R&& r) const
            noexcept (requires{
                {impl::unpack(call<N>(*std::forward<R>(r).__value))} noexcept;
            })
            requires (
                meta::range<decltype((*std::forward<R>(r).__value))> &&
                requires{{impl::unpack(call<N>(*std::forward<R>(r).__value))};}
            )
        {
            return (impl::unpack(call<N>(*std::forward<R>(r).__value)));
        }

    public:
        template <size_t N = 0, typename C> requires (N == sizeof...(Is))
        constexpr decltype(auto) operator()(C&& c) const
            noexcept (requires{{std::forward<C>(c)} noexcept;})
            requires (requires{{std::forward<C>(c)};})
        {
            return (std::forward<C>(c));
        }

        template <size_t N = 0, typename C> requires (N < sizeof...(Is))
        constexpr decltype(auto) operator()(C&& c) const
            noexcept ((N + 1 == sizeof...(Is) && requires{
                {call<N>(std::forward<C>(c))} noexcept;
            }) || (N + 1 < sizeof...(Is) && requires{
                {(*this).template operator()<N + 1>(call<N>(std::forward<C>(c)))} noexcept;
            }))
            requires ((N + 1 == sizeof...(Is) && requires{
                {call<N>(std::forward<C>(c))};
            }) || (N + 1 < sizeof...(Is) && requires{
                {(*this).template operator()<N + 1>(call<N>(std::forward<C>(c)))};
            }))
        {
            if constexpr (N + 1 == sizeof...(Is)) {
                return (call<N>(std::forward<C>(c)));
            } else {
                return ((*this).template operator()<N + 1>(call<N>(std::forward<C>(c))));
            }
        }

        /// TODO: provide a `.iter(c)` method that returns an iterator to the given
        /// index, rather than a reference, applying the same optimizations as `slice`.
        /// Also, maybe subscripting can be allowed for unsized containers as well,
        /// in which case we would obtain the correct value by forwarding the index
        /// directly, or obtaining the value using iterators as a fallback.
    };

    template <typename... Is>
    at(Is&&...) -> at<type<Is>...>;

    /// TODO: what about `.keys()` and `.values()`?


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
    template <meta::not_rvalue C>
    struct range : impl::range_tag {
        [[no_unique_address]] impl::ref<C> __value;

        [[nodiscard]] constexpr range() = default;
        [[nodiscard]] constexpr range(const range&) = default;
        [[nodiscard]] constexpr range(range&&) = default;

        template <typename... A> requires (sizeof...(A) > 0)
        [[nodiscard]] constexpr range(A&&... args)
            noexcept (meta::nothrow::constructible_from<impl::ref<C>, A...>)
            requires (!meta::range<C> && requires{{impl::ref<C>{std::forward<A>(args)...}};})
        :
            __value(std::forward<A>(args)...)
        {}

        template <typename... A> requires (sizeof...(A) > 0)
        [[nodiscard]] constexpr explicit range(A&&... args)
            noexcept (meta::nothrow::constructible_from<impl::ref<C>, A...>)
            requires (meta::range<C> && meta::constructible_from<impl::ref<C>, A...>)
        :
            __value(std::forward<A>(args)...)
        {}

        template <typename Start, typename Stop = impl::iota_tag, typename Step = impl::iota_tag>
        [[nodiscard]] constexpr explicit range(
            Start&& start,
            Stop&& stop,
            Step&& step = {}
        )
            requires (meta::inherits<C, impl::iota_tag>)
        :
            __value(
                std::forward<Start>(start),
                std::forward<Stop>(stop),
                std::forward<Step>(step)
            )
        {}

        /* `swap()` operator between ranges. */
        constexpr void swap(range& other)
            noexcept (meta::nothrow::swappable<impl::ref<C>>)
            requires (meta::swappable<impl::ref<C>>)
        {
            std::ranges::swap(__value, other.__value);
        }

        /* Dereferencing a range promotes it into a trivial `unpack` subclass, which allows
        it to be destructured when used as an argument to a Bertrand function. */
        template <typename Self>
        [[nodiscard]] constexpr auto operator*(this Self&& self)
            noexcept (requires{{impl::unpack(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{impl::unpack(*std::forward<Self>(self).__value)};})
        {
            return impl::unpack(*std::forward<Self>(self).__value);
        }

        /* Indirectly access a member of the wrapped container. */
        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (
                meta::inherits<C, impl::iota_tag> ||
                requires{{impl::arrow{*std::forward<Self>(self).__value}} noexcept;}
            )
            requires (
                meta::inherits<C, impl::iota_tag> ||
                requires{{impl::arrow{*std::forward<Self>(self).__value}};}
            )
        {
            if constexpr (meta::inherits<C, impl::iota_tag>) {
                return std::addressof(*std::forward<Self>(self).__value);
            } else {
                return impl::arrow{*std::forward<Self>(self).__value};
            }
        }

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
            noexcept (meta::nothrow::has_size<C>)
            requires (meta::has_size<C>)
        {
            return std::ranges::size(*__value);
        }

        /* Forwarding `ssize()` operator for the underlying container, provided the
        container supports it. */
        [[nodiscard]] constexpr auto ssize() const
            noexcept (meta::nothrow::has_ssize<C>)
            requires (meta::has_ssize<C>)
        {
            return std::ranges::ssize(*__value);
        }

        /* Forwarding `empty()` operator for the underlying container, provided the
        container supports it. */
        [[nodiscard]] constexpr bool empty() const
            noexcept (meta::nothrow::has_empty<C>)
            requires (meta::has_empty<C>)
        {
            return std::ranges::empty(*__value);
        }

        /* Tuple-like range accessor, provided the underlying container supports
        `get<I>()`.  Automatically applies Python-style wraparound for negative
        indices, and allows multidimensional indexing if the container is a tuple of
        tuples.  This is equivalent to `at<Is...>{}(container)`. */
        template <auto... Is, typename Self>
        constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{at<Is...>{}(std::forward<Self>(self))} noexcept;})
            requires (requires{{at<Is...>{}(std::forward<Self>(self))};})
        {
            return (at<Is...>{}(std::forward<Self>(self)));
        }

        /* Integer subscript operator.  Accepts one or more signed integers and
        retrieves the corresponding element from the underlying container after
        applying Python-style wraparound for negative indices, which converts the index
        to an unsigned integer.  If multiple indices are given, then each successive
        index after the first will be used to subscript the previous result.  This is
        equivalent to `at{is...}(container)`. */
        template <typename Self, typename... Is>
        constexpr decltype(auto) operator[](this Self&& self, Is&&... is)
            noexcept (requires{{at{std::forward<Is>(is)...}(std::forward<Self>(self))} noexcept;})
            requires (requires{{at{std::forward<Is>(is)...}(std::forward<Self>(self))};})
        {
            return (at{std::forward<Is>(is)...}(std::forward<Self>(self)));
        }

        /* Get a forward iterator to the start of the range. */
        [[nodiscard]] constexpr auto begin()
            noexcept (requires{
                {impl::make_range_iterator(std::ranges::begin(*__value))} noexcept;
            })
            requires (requires{{impl::make_range_iterator(std::ranges::begin(*__value))};})
        {
            return impl::make_range_iterator(std::ranges::begin(*__value));
        }

        /* Get a forward iterator to the start of the range. */
        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{
                {impl::make_range_iterator(std::ranges::begin(*__value))} noexcept;
            })
            requires (requires{{impl::make_range_iterator(std::ranges::begin(*__value))};})
        {
            return impl::make_range_iterator(std::ranges::begin(*__value));
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
            noexcept (requires{
                {impl::make_range_iterator(std::ranges::end(*__value))} noexcept;
            })
            requires (requires{{impl::make_range_iterator(std::ranges::end(*__value))};})
        {
            return impl::make_range_iterator(std::ranges::end(*__value));
        }

        /* Get a forward iterator to one past the last element of the range. */
        [[nodiscard]] constexpr auto end() const
            noexcept (requires{
                {impl::make_range_iterator(std::ranges::end(*__value))} noexcept;
            })
            requires (requires{{impl::make_range_iterator(std::ranges::end(*__value))};})
        {
            return impl::make_range_iterator(std::ranges::end(*__value));
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
            noexcept (requires{
                {impl::make_range_iterator(std::ranges::rbegin(*__value))} noexcept;
            })
            requires (requires{{impl::make_range_iterator(std::ranges::rbegin(*__value))};})
        {
            return impl::make_range_iterator(std::ranges::rbegin(*__value));
        }

        /* Get a reverse iterator to the last element of the range. */
        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{
                {impl::make_range_iterator(std::ranges::rbegin(*__value))} noexcept;
            })
            requires (requires{{impl::make_range_iterator(std::ranges::rbegin(*__value))};})
        {
            return impl::make_range_iterator(std::ranges::rbegin(*__value));
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
            noexcept (requires{
                {impl::make_range_iterator(std::ranges::rend(*__value))} noexcept;
            })
            requires (requires{{impl::make_range_iterator(std::ranges::rend(*__value))};})
        {
            return impl::make_range_iterator(std::ranges::rend(*__value));
        }

        /* Get a reverse iterator to one before the first element of the range. */
        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{
                {impl::make_range_iterator(std::ranges::rend(*__value))} noexcept;
            })
            requires (requires{{impl::make_range_iterator(std::ranges::rend(*__value))};})
        {
            return impl::make_range_iterator(std::ranges::rend(*__value));
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

        constexpr range& operator=(const range&) = default;
        constexpr range& operator=(range&&) = default;

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
        /// later in zip.h, once `zip{}` has been defined.

    };

    /* A special case of `range` that allows it to adapt to tuple-like types that are
    not otherwise iterable.  This works by dispatching to a reference array that gets
    populated when the range is constructed as long as the tuple contains only a single
    type, or a static vtable filled with function pointers that extract the
    corresponding value when called.  In the latter case, the return type will be
    promoted to a `Union` in order to model heterogenous tuples. */
    template <meta::not_rvalue C>
        requires (!meta::range<C> && !meta::iterable<C> && meta::tuple_like<C>)
    struct range<C> : range<impl::tuple_range<C>> {
        using range<impl::tuple_range<C>>::range;
    };

    /* A special case of `range` that contains only a single, non-iterable element.
    Default-constructing this range will create a range of zero elements instead. */
    template <meta::not_rvalue C>
        requires (!meta::range<C> && !meta::iterable<C> && !meta::tuple_like<C>)
    struct range<C> : range<impl::single_range<C>> {
        using range<impl::single_range<C>>::range;
    };

}


/// TODO: since sequences now encode whether they are sized at compile time, there's
/// no need for `has_size()` and any related behavior.


/// TODO: maybe sequence.h is a good idea, since I could define strings earlier and
/// avoid circular dependencies.  Strings only really depend on ranges and unpack
/// wrappers.  Also, as soon as strings are defined, I can define tuples, which may
/// simplify the `shape()` stuff.


namespace impl {

    /// TODO: reintroduce a `copy` flag that controls whether a `.copy()` member
    /// function gets compiled, which will do a deep copy of the underlying container.
    /// Otherwise, the ordinary copy constructor defaults to atomic reference counting.

    /* A simple aggregate that encodes the shape and capabilities of a `sequence<T>`
    type, allowing some type information to persist after erasure.  The full class is
    defined as:

        ```
        struct sequence_flags {
            uint8_t dims = 0;
            bool size = false;
            bool subscript = false;
            bool data = false;
            bool reverse = false;
        };
        ```

    Where each field has the following behavior:

        1.  `ndims`: indicates the number of dimensions that the sequence supports,
            which is always equal to the length of its final `shape()` member.  If
            this is set to zero (the default), then no `shape()` will be
            generated.  Note that only the lower 6 bits are significant, since
            Python's buffer protocol does not currently support sequences with more
            than 64 dimensions.
        2.  `size`: if `true`, then the sequence will expose a `size()` member function
            and insert a corresponding entry into the internal vtable.  Otherwise, no
            `size()` member will be generated.  Typically, if `ndims > 0`, then
            `size()` should be equal to the product of the values of `shape()`,
            although such behavior is technically implementation-defined.  All bertrand
            containers conform to that expectation, but user-defined classes may not,
            and it is not a strict requirement.
        3.  `subscript`: if `true`, then the sequence will support (possibly
            multidimensional) indexing via the `[]` operator using unsigned integer
            offsets.  If this flag is set in conjunction with `size`, then the
            sequence's subscript operator will also allow signed inputs, applying
            Python-style wraparound for negative values, as well as automatic bounds
            checking in debug builds.  If `ndims > 1`, then the subscript operator
            will also support multidimensional indexing, where each argument after the
            first expands to a series of nested subscripts (akin to
            `sequence[i][j][k]...`), up to the maximum number of dimensions.
        4.  `data`: if `true`, then the sequence will expose a `data()` member function
            that returns a pointer to an underlying data array, adding an entry to the
            internal vtable.  If Python bindings are generated for such a sequence,
            then the buffer protocol may be used to expose the data array to Python.
        5.  `reverse`: if set, then the sequence will support reverse iteration as well
            as forward iteration.

    Typically, all of these flags will be inferred from a given initializer using CTAD.
    If that is not available for some reason, then the `sequence` template can be
    explicitly specialized using aggregate initialization:

        ```
        auto func() -> sequence<T, {.ndims = 3, .size = true, .subscript = true, ...}> {
            // a 3-dimensional container holding elements of type `T` that supports
            // `size()`, `[i, j, k]`, etc.
        }

        auto func() -> decltype(sequence(container)) {
            // if a container is handy, which deduces all the required flags
        }
        ```

    If no flags are given, then `sequence<T>` devolves to a simple, unsized,
    forward-iterable range yielding values of type `T`. */
    struct sequence_flags {
        uint8_t ndims = 0;
        bool size = false;
        bool subscript = false;
        bool data = false;
        bool reverse = false;
    };

    template <typename C>
    constexpr uint8_t infer_sequence_dims = 0;
    template <typename C>
        requires (requires(C c) {
            {c.shape()} -> meta::tuple_like;
            {c.shape()} -> meta::yields<size_t>;
        })
    constexpr uint8_t infer_sequence_dims<C> =
        meta::tuple_size<decltype(std::declval<C>().shape())>;

    /* Infer the `sequence_flags` for a container of type `C` using SFINAE. */
    template <typename C>
    static constexpr sequence_flags infer_sequence_flags {
        .dims = infer_sequence_dims<C>,
        .size = meta::has_size<C>,
        .subscript = meta::indexable<C, size_t>,
        .data = meta::has_data<C>,
        .reverse = meta::reverse_iterable<C>
    };


    /// TODO: update and streamline error messages.  This might not be doable until
    /// after strings are defined, since otherwise I get undefined template errors.

    template <meta::not_void T> requires (meta::not_rvalue<T>)
    struct sequence_iterator;

    namespace sequence_dtor {
        using ptr = void(*)(void*);

        template <typename C>
        constexpr void _fn(void* ptr) {
            if constexpr (!meta::lvalue<C> || !meta::has_address<C>) {
                delete reinterpret_cast<meta::as_pointer<C>>(ptr);
            }
        }

        template <typename C>
        constexpr ptr fn = &_fn<C>;
    }

    namespace sequence_size {
        using ptr = size_t(*)(void*);
        using err = TypeError(*)();

        template <meta::const_ref C> requires (meta::has_size<C>)
        constexpr size_t _fn(void* ptr) {
            return std::ranges::size(*reinterpret_cast<meta::as_pointer<C>>(ptr));
        }

        template <meta::const_ref C> requires (DEBUG && !meta::has_size<C>)
        constexpr TypeError _error() {
            /// TODO: provide the demangled type name in the error message for clarity.
            return TypeError("underlying container does not support size()");
        }

        template <meta::const_ref C>
        constexpr ptr fn = nullptr;
        template <meta::const_ref C> requires (meta::has_size<C>)
        constexpr ptr fn<C> = &_fn<C>;

        template <meta::const_ref C>
        constexpr err error = nullptr;
        template <meta::const_ref C> requires (DEBUG && !meta::has_size<C>)
        constexpr err error<C> = &_error<C>;
    }

    namespace sequence_empty {
        using ptr = bool(*)(void*);
        using err = TypeError(*)();

        template <meta::const_ref C> requires (meta::has_empty<C>)
        constexpr bool _fn(void* ptr) {
            return std::ranges::empty(*reinterpret_cast<meta::as_pointer<C>>(ptr));
        }

        template <meta::const_ref C> requires (DEBUG && !meta::has_empty<C>)
        constexpr TypeError _error() {
            /// TODO: provide the demangled type name in the error message for clarity.
            return TypeError("underlying container does not support empty()");
        }

        template <meta::const_ref C>
        constexpr ptr fn = nullptr;
        template <meta::const_ref C> requires (meta::has_empty<C>)
        constexpr ptr fn<C> = &_fn<C>;

        template <meta::const_ref C>
        constexpr err error = nullptr;
        template <meta::const_ref C> requires (DEBUG && !meta::has_empty<C>)
        constexpr err error<C> = &_error<C>;
    }

    namespace sequence_subscript {
        template <meta::not_void T> requires (meta::not_rvalue<T>)
        using ptr = T(*)(void*, size_t);
        template <meta::not_void T> requires (meta::not_rvalue<T>)
        using err = TypeError(*)();

        template <meta::lvalue C, meta::not_void T>
            requires (meta::not_rvalue<T> && meta::index_returns<T, C, size_t>)
        constexpr T _fn(void* ptr, size_t i) {
            return (*reinterpret_cast<meta::as_pointer<C>>(ptr))[i];
        }

        template <meta::lvalue C, meta::not_void T>
            requires (meta::not_rvalue<T> && DEBUG && !meta::index_returns<T, C, size_t>)
        constexpr TypeError _error() {
            /// TODO: provide the demangled type name in the error message for clarity.
            return TypeError("underlying container does not support indexing");
        }

        template <meta::lvalue C, meta::not_void T>
        constexpr ptr<T> fn = nullptr;
        template <meta::lvalue C, meta::not_void T>
            requires (meta::not_rvalue<T> && meta::index_returns<T, C, size_t>)
        constexpr ptr<T> fn<C, T> = &_fn<C, T>;

        template <meta::lvalue C, meta::not_void T>
        constexpr err<T> error = nullptr;
        template <meta::lvalue C, meta::not_void T>
            requires (meta::not_rvalue<T> && DEBUG && !meta::index_returns<T, C, size_t>)
        constexpr err<T> error<C, T> = &_error<C, T>;
    }

    namespace sequence_begin {
        template <meta::not_void T> requires (meta::not_rvalue<T>)
        using ptr = sequence_iterator<T>(*)(void*);

        template <meta::lvalue C, meta::not_void T>
            requires (meta::not_rvalue<T> && meta::yields<C, T>)
        constexpr sequence_iterator<T> _fn(void* ptr) {
            return {
                reinterpret_cast<meta::as_pointer<C>>(ptr)->begin(),
                reinterpret_cast<meta::as_pointer<C>>(ptr)->end()
            };
        }

        template <meta::lvalue C, meta::not_void T>
            requires (meta::not_rvalue<T> && meta::yields<C, T>)
        constexpr ptr<T> fn = &_fn<C, T>;
    }

    /* Sequences are reference counted in order to allow for efficient copy/move
    semantics.  The only difference from a typical `shared_ptr` is that lvalue
    initializers will not be copied onto the heap, and will instead simply reference
    their current location as an optimization.  Copying large containers is potentially
    expensive, and should be done explicitly by manually moving or copying the
    value.

    This class represents the control block for the sequence, which stores all the
    function pointers needed to emulate the standard range interface, so that they
    don't have to be stored on the sequence itself.  A block of this form will always
    be heap allocated to back the underlying container when it is first converted into
    a sequence. */
    template <meta::not_void T> requires (meta::not_rvalue<T>)
    struct sequence_control {
        std::atomic<size_t> count = 0;
        const sequence_dtor::ptr dtor = nullptr;
        const sequence_size::ptr size_fn = nullptr;
        const sequence_empty::ptr empty_fn = nullptr;
        const sequence_subscript::ptr<T> subscript_fn = nullptr;
        const sequence_begin::ptr<T> begin_fn = nullptr;
    };

    /* When compiled in debug mode, the control block stores extra function pointers
    that throw detailed errors if `size()`, `empty()`, or `operator[]` are used when
    they are not available. */
    template <meta::not_void T> requires (meta::not_rvalue<T> && DEBUG)
    struct sequence_control<T> {
        std::atomic<size_t> count = 0;
        const sequence_dtor::ptr dtor = nullptr;
        const sequence_size::ptr size_fn = nullptr;
        const sequence_size::err size_err = nullptr;
        const sequence_empty::ptr empty_fn = nullptr;
        const sequence_empty::err empty_err = nullptr;
        const sequence_subscript::ptr<T> subscript_fn = nullptr;
        const sequence_subscript::err<T> subscript_err = nullptr;
        const sequence_begin::ptr<T> begin_fn = nullptr;
    };

    /* The underlying container type for a sequence serves as the public-facing
    component of its shared pointer-like memory model, which updates reference counts
    whenever it is copied or destroyed.  Note that because the underlying container
    type is erased, there's no way to tell at compile time whether it supports size,
    empty, or subscript access, so additional boolean properties are exposed to check
    for these, which must be used to avoid undefined behavior. */
    template <meta::not_void T> requires (meta::not_rvalue<T>)
    struct sequence {
        using type = T;
        using size_type = size_t;
        using index_type = ssize_t;

    private:
        void* data = nullptr;
        sequence_control<T>* control = nullptr;

        constexpr void decref() {
            if (control && control->count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::atomic_thread_fence(std::memory_order_acquire);
                control->dtor(data);
                delete control;
            }
        }

    public:
        [[nodiscard]] constexpr sequence() = default;

        template <meta::yields<T> C> requires (meta::lvalue<C> && meta::has_address<C>)
        [[nodiscard]] constexpr sequence(C&& c) noexcept (meta::nothrow::has_address<C>) :
            data(const_cast<meta::as_pointer<meta::remove_const<C>>>(std::addressof(c))),
            control(new sequence_control<T>{
                .count = 1,
                .dtor = sequence_dtor::fn<C>,  // no-op for lvalue references
                .size_fn = sequence_size::fn<meta::as_const_ref<C>>,
                .empty_fn = sequence_empty::fn<meta::as_const_ref<C>>,
                .subscript_fn = sequence_subscript::fn<meta::as_lvalue<C>, T>,
                .begin_fn = sequence_begin::fn<meta::as_lvalue<C>, T>
            })
        {}

        template <meta::yields<T> C> requires (!meta::lvalue<C> || !meta::has_address<C>)
        [[nodiscard]] constexpr sequence(C&& c) :
            data(new meta::unqualify<C>(std::forward<C>(c))),
            control(new sequence_control<T>{
                .count = 1,
                .dtor = sequence_dtor::fn<C>,  // calls `delete` on `data`
                .size_fn = sequence_size::fn<meta::as_const_ref<C>>,
                .empty_fn = sequence_empty::fn<meta::as_const_ref<C>>,
                .subscript_fn = sequence_subscript::fn<meta::as_lvalue<C>, T>,
                .begin_fn = sequence_begin::fn<meta::as_lvalue<C>, T>
            })
        {}

        [[nodiscard]] constexpr sequence(const sequence& other) noexcept :
            data(other.data),
            control(other.control)
        {
            if (control) {
                control->count.fetch_add(1, std::memory_order_relaxed);
            }
        }

        [[nodiscard]] constexpr sequence(sequence&& other) noexcept :
            data(other.data),
            control(other.control)
        {
            other.data = nullptr;
            other.control = nullptr;
        }

        constexpr sequence& operator=(const sequence& other) {
            if (this != &other) {
                decref();
                data = other.data;
                control = other.control;
                if (control) {
                    control->count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            return *this;
        }

        constexpr sequence& operator=(sequence&& other) {
            if (this != &other) {
                decref();
                data = other.data;
                control = other.control;
                other.data = nullptr;
                other.control = nullptr;
            }
            return *this;
        }

        constexpr ~sequence() {
            decref();
            data = nullptr;
            control = nullptr;
        }

        constexpr void swap(sequence& other) noexcept {
            std::swap(data, other.data);
            std::swap(control, other.control);
        }

        [[nodiscard]] constexpr bool has_size() const noexcept {
            return control->size_fn != nullptr;
        }

        [[nodiscard]] constexpr size_type size() const {
            return control->size_fn(data);
        }

        [[nodiscard]] constexpr index_type ssize() const {
            return index_type(control->size_fn(data));
        }

        [[nodiscard]] constexpr bool has_empty() const noexcept {
            return control->empty_fn != nullptr;
        }

        [[nodiscard]] constexpr bool empty() const {
            return control->empty_fn(data);
        }

        [[nodiscard]] constexpr bool has_subscript() const noexcept {
            return control->subscript_fn != nullptr;
        }

        [[nodiscard]] constexpr T operator[](index_type i) const {
            if (has_size() && i < 0) {
                i += index_type(control->size_fn(data));
            }
            return control->subscript_fn(data, size_type(i));
        }

        [[nodiscard]] constexpr sequence_iterator<T> begin() const {
            return control->begin_fn(data);
        }

        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }
    };

    /* When compiled in debug mode, the sequence inserts extra error paths for when the
    size, empty, or subscript operators are applied to a container which does not
    support them, using the extra function pointers in the control block. */
    template <meta::not_void T> requires (meta::not_rvalue<T> && DEBUG)
    struct sequence<T> {
        using type = T;
        using size_type = size_t;
        using index_type = ssize_t;

    private:
        void* data = nullptr;
        sequence_control<T>* control = nullptr;

        constexpr void decref() {
            if (control && control->count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::atomic_thread_fence(std::memory_order_acquire);
                control->dtor(data);
                delete control;
            }
        }

    public:
        [[nodiscard]] constexpr sequence() = default;

        template <meta::yields<T> C> requires (meta::lvalue<C> && meta::has_address<C>)
        [[nodiscard]] constexpr sequence(C&& c) noexcept (meta::nothrow::has_address<C>) :
            data(const_cast<meta::as_pointer<meta::remove_const<C>>>(std::addressof(c))),
            control(new sequence_control<T>{
                .count = 1,
                .dtor = sequence_dtor::fn<C>,  // no-op for lvalue references
                .size_fn = sequence_size::fn<meta::as_const_ref<C>>,
                .size_err = sequence_size::error<meta::as_const_ref<C>>,
                .empty_fn = sequence_empty::fn<meta::as_const_ref<C>>,
                .empty_err = sequence_empty::error<meta::as_const_ref<C>>,
                .subscript_fn = sequence_subscript::fn<meta::as_lvalue<C>, T>,
                .subscript_err = sequence_subscript::error<meta::as_lvalue<C>, T>,
                .begin_fn = sequence_begin::fn<meta::as_lvalue<C>, T>
            })
        {}

        template <meta::yields<T> C> requires (!meta::lvalue<C> || !meta::has_address<C>)
        [[nodiscard]] constexpr sequence(C&& c) :
            data(new meta::unqualify<C>(std::forward<C>(c))),
            control(new sequence_control<T>{
                .count = 1,
                .dtor = sequence_dtor::fn<C>,  // calls `delete` on `data`
                .size_fn = sequence_size::fn<meta::as_const_ref<C>>,
                .size_err = sequence_size::error<meta::as_const_ref<C>>,
                .empty_fn = sequence_empty::fn<meta::as_const_ref<C>>,
                .empty_err = sequence_empty::error<meta::as_const_ref<C>>,
                .subscript_fn = sequence_subscript::fn<meta::as_lvalue<C>, T>,
                .subscript_err = sequence_subscript::error<meta::as_lvalue<C>, T>,
                .begin_fn = sequence_begin::fn<meta::as_lvalue<C>, T>
            })
        {}


        [[nodiscard]] constexpr sequence(const sequence& other) noexcept :
            data(other.data),
            control(other.control)
        {
            if (control) {
                control->count.fetch_add(1, std::memory_order_relaxed);
            }
        }

        [[nodiscard]] constexpr sequence(sequence&& other) noexcept :
            data(other.data),
            control(other.control)
        {
            other.data = nullptr;
            other.control = nullptr;
        }

        constexpr sequence& operator=(const sequence& other) {
            if (this != &other) {
                decref();
                data = other.data;
                control = other.control;
                if (control) {
                    control->count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            return *this;
        }

        constexpr sequence& operator=(sequence&& other) {
            if (this != &other) {
                decref();
                data = other.data;
                control = other.control;
                other.data = nullptr;
                other.control = nullptr;
            }
            return *this;
        }

        constexpr ~sequence() {
            decref();
            data = nullptr;
            control = nullptr;
        }

        constexpr void swap(sequence& other) noexcept {
            std::swap(data, other.data);
            std::swap(control, other.control);
        }

        [[nodiscard]] constexpr bool has_size() const noexcept {
            return control->size_fn != nullptr;
        }

        [[nodiscard]] constexpr size_type size() const {
            if (control->size_err != nullptr) {
                throw control->size_err();
            }
            return control->size_fn(data);
        }

        [[nodiscard]] constexpr index_type ssize() const {
            if (control->size_err != nullptr) {
                throw control->size_err();
            }
            return index_type(control->size_fn(data));
        }

        [[nodiscard]] constexpr bool has_empty() const noexcept {
            return control->empty_fn != nullptr;
        }

        [[nodiscard]] constexpr bool empty() const {
            if (control->empty_err != nullptr) {
                throw control->empty_err();
            }
            return control->empty_fn(data);
        }

        [[nodiscard]] constexpr bool has_subscript() const noexcept {
            return control->subscript_fn != nullptr;
        }

        [[nodiscard]] constexpr T operator[](index_type i) const {
            index_type n = i;

            if (has_size()) {
                index_type size = index_type(control->size_fn(data));
                n += size * (n < 0);
                if (n < 0 || n >= size) {
                    /// TODO: fix this error message
                    throw IndexError(
                        "index " + std::to_string(i) +
                        " out of range for sequence of size " + std::to_string(size)
                    );
                }

            } else if (n < 0) {
                throw IndexError(
                    "negative index " + std::to_string(i) + " not allowed for sequence"
                );
            }

            return control->subscript_fn(data, size_type(n));
        }

        [[nodiscard]] constexpr sequence_iterator<T> begin() const {
            return control->begin_fn(data);
        }

        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }
    };

    /* Iterators also have to be type-erased and stored as void pointers.  Reference
    counting is not allowed in this case, however, and copying the iterator means
    allocating a new iterator to store the copy.  The only reason we don't use
    `std::unique_ptr` here is that it doesn't generally play well with void pointers,
    so it's just easier to implement our own. */
    struct sequence_iter_storage {
        using copy = void*(*)(void*);
        using destroy = void(*)(void*);

        template <typename T>
        static constexpr void* ctor(T&& obj) {
            return new meta::unqualify<T>(std::forward<T>(obj));
        }

        template <meta::not_reference T>
        static constexpr void* _copy(void* ptr) {
            return new meta::unqualify<T>(*reinterpret_cast<meta::as_pointer<T>>(ptr));
        }

        template <meta::not_reference T>
        static constexpr void dtor(void* ptr) {
            delete reinterpret_cast<meta::as_pointer<T>>(ptr);
        }

        void* begin = nullptr;
        copy begin_copy = nullptr;
        destroy begin_dtor = nullptr;

        void* end = nullptr;
        copy end_copy = nullptr;
        destroy end_dtor = nullptr;

        [[nodiscard]] constexpr sequence_iter_storage() noexcept = default;

        template <typename Begin, typename End>
        [[nodiscard]] constexpr sequence_iter_storage(Begin&& b, End&& e) :
            begin(ctor(std::forward<Begin>(b))),
            begin_copy(&_copy<meta::remove_reference<Begin>>),
            begin_dtor(&dtor<meta::remove_reference<Begin>>),
            end(ctor(std::forward<End>(e))),
            end_copy(&_copy<meta::remove_reference<End>>),
            end_dtor(&dtor<meta::remove_reference<End>>)
        {}

        [[nodiscard]] constexpr sequence_iter_storage(const sequence_iter_storage& other) :
            begin(other.begin ? other.begin_copy(other.begin) : nullptr),
            begin_copy(other.begin_copy),
            begin_dtor(other.begin_dtor),
            end(other.end ? other.end_copy(other.end) : nullptr),
            end_copy(other.end_copy),
            end_dtor(other.end_dtor)
        {}

        [[nodiscard]] constexpr sequence_iter_storage(sequence_iter_storage&& other) noexcept :
            begin(other.begin),
            begin_copy(other.begin_copy),
            begin_dtor(other.begin_dtor),
            end(other.end),
            end_copy(other.end_copy),
            end_dtor(other.end_dtor)
        {
            other.begin = nullptr;
            other.end = nullptr;
        }

        constexpr sequence_iter_storage& operator=(const sequence_iter_storage& other) {
            if (this != &other) {
                if (begin) begin_dtor(begin);
                if (end) end_dtor(end);
                begin = other.begin ? other.begin_copy(other.begin) : nullptr;
                begin_copy = other.begin_copy;
                begin_dtor = other.begin_dtor;
                end = other.end ? other.end_copy(other.end) : nullptr;
                end_copy = other.end_copy;
                end_dtor = other.end_dtor;
            }
            return *this;
        }

        constexpr sequence_iter_storage& operator=(sequence_iter_storage& other) {
            if (this != &other) {
                if (begin) begin_dtor(begin);
                if (end) end_dtor(end);
                begin = other.begin;
                begin_copy = other.begin_copy;
                begin_dtor = other.begin_dtor;
                end = other.end;
                end_copy = other.end_copy;
                end_dtor = other.end_dtor;
                other.begin = nullptr;
                other.end = nullptr;
            }
            return *this;
        }

        constexpr ~sequence_iter_storage() {
            if (begin) begin_dtor(begin);
            if (end) end_dtor(end);
        }

        constexpr void swap(sequence_iter_storage& other) noexcept {
            std::swap(begin, other.begin);
            std::swap(begin_copy, other.begin_copy);
            std::swap(begin_dtor, other.begin_dtor);
            std::swap(end, other.end);
            std::swap(end_copy, other.end_copy);
            std::swap(end_dtor, other.end_dtor);
        }
    };

    namespace sequence_deref {
        template <meta::not_void T> requires (meta::not_rvalue<T>)
        using ptr = T(*)(void*);

        template <meta::const_ref Begin, meta::not_void T>
            requires (meta::not_rvalue<T> && meta::dereference_returns<T, Begin>)
        constexpr T _fn(void* ptr) {
            return **reinterpret_cast<meta::as_pointer<Begin>>(ptr);
        }

        template <meta::const_ref Begin, meta::not_void T>
            requires (meta::not_rvalue<T> && meta::dereference_returns<T, Begin>)
        constexpr ptr<T> fn = &_fn<Begin, T>;
    }

    namespace sequence_increment {
        using ptr = void(*)(void*);

        template <meta::lvalue Begin>
            requires (meta::not_const<Begin> && meta::has_preincrement<Begin>)
        constexpr void _fn(void* ptr) {
            ++*reinterpret_cast<meta::as_pointer<Begin>>(ptr);
        }

        template <meta::lvalue Begin>
            requires (meta::not_const<Begin> && meta::has_preincrement<Begin>)
        constexpr ptr fn = &_fn<Begin>;
    }

    namespace sequence_compare {
        using ptr = bool(*)(void*, void*);

        template <meta::const_ref Begin, meta::const_ref End>
            requires (meta::eq_returns<bool, Begin, End>)
        constexpr bool _fn(void* lhs, void* rhs) {
            return
                *reinterpret_cast<meta::as_pointer<Begin>>(lhs) ==
                *reinterpret_cast<meta::as_pointer<End>>(rhs);
        }

        template <meta::const_ref Begin, meta::const_ref End>
            requires (meta::eq_returns<bool, Begin, End>)
        constexpr ptr fn = &_fn<Begin, End>;
    }

    /// TODO: it might be best to have sequence model `std::common_range` (i.e. make
    /// the begin and end iterators always the same type).  That would require some
    /// careful design, but would make sequences more compatible with the standard
    /// library algorithms if possible.
    /// -> sequence_iter_storage would only have to store a single iterator, and the
    /// overall iterator would be initialized with the required function pointers,
    /// which would be manually generated in both cases with the right semantics.

    /* A type-erased wrapper for an iterator that dereferences to type `T` and its
    corresponding sentinel.  This is the type of iterator returned by a `sequence<T>`
    range, and always models `std::input_iterator`, as well as possibly
    `std::output_iterator<U>` if `U` is assignable to `T`. */
    template <meta::not_void T> requires (meta::not_rvalue<T>)
    struct sequence_iterator {
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = meta::remove_reference<T>;
        using reference = meta::as_lvalue<T>;
        using pointer = meta::as_pointer<T>;

    private:
        sequence_iter_storage storage;
        sequence_deref::ptr<T> deref_fn = nullptr;
        sequence_increment::ptr increment_fn = nullptr;
        sequence_compare::ptr compare_fn = nullptr;

    public:
        [[nodiscard]] constexpr sequence_iterator() = default;

        template <meta::iterator Begin, meta::sentinel_for<Begin> End>
            requires (meta::dereference_returns<T, meta::as_const_ref<Begin>>)
        [[nodiscard]] constexpr sequence_iterator(Begin&& b, End&& e)
            noexcept (requires{
                {std::make_unique<meta::remove_reference<Begin>>(std::forward<Begin>(b))} noexcept;
                {std::make_unique<meta::remove_reference<End>>(std::forward<End>(e))} noexcept;
            })
        :
            storage(std::forward<Begin>(b), std::forward<End>(e)),
            deref_fn(sequence_deref::fn<meta::as_const_ref<Begin>, T>),
            increment_fn(sequence_increment::fn<meta::as_lvalue<meta::remove_const<Begin>>>),
            compare_fn(sequence_compare::fn<meta::as_const_ref<Begin>, meta::as_const_ref<End>>)
        {}

        [[nodiscard]] constexpr T operator*() const {
            return deref_fn(storage.begin);
        }

        [[nodiscard]] constexpr auto operator->() const {
            return impl::arrow(deref_fn(storage.begin));
        }

        constexpr sequence_iterator& operator++() {
            increment_fn(storage.begin);
            return *this;
        }

        [[nodiscard]] constexpr sequence_iterator operator++(int) {
            sequence_iterator tmp = *this;
            increment_fn(storage.begin);
            return tmp;
        }

        [[nodiscard]] friend constexpr bool operator==(const sequence_iterator& lhs, NoneType) {
            return lhs.compare_fn(lhs.storage.begin, lhs.storage.end);
        }

        [[nodiscard]] friend constexpr bool operator==(NoneType, const sequence_iterator& rhs) {
            return rhs.compare_fn(rhs.storage.begin, rhs.storage.end);
        }

        [[nodiscard]] friend constexpr bool operator!=(const sequence_iterator& lhs, NoneType) {
            return !lhs.compare_fn(lhs.storage.begin, lhs.storage.end);
        }

        [[nodiscard]] friend constexpr bool operator!=(NoneType, const sequence_iterator& rhs) {
            return !rhs.compare_fn(rhs.storage.begin, rhs.storage.end);
        }
    };

}


namespace iter {

    /* A type-erased range that forgets the underlying container type, and presents only as
    a sequence of some type.

    Because of their use as monadic expression templates, ranges can quickly become deeply
    nested and brittle, especially when used with conditionals which may return slightly
    different types for each branch.  A similar problem exists with C++ function objects,
    which can be mitigated by using `std::function`, which `sequence<T>` is perfectly
    analogous to, but for iterable containers.

    The sequence constructor works by taking an arbitrary container type `C` that meets
    the criteria and moving it onto the heap as a reference-counted void pointer.  It then
    generates a table of function pointers that emulate the standard range interface for
    `C`, including `size()`, `empty()`, `operator[]`, and basic input/output iterators.
    Users should note that erasing the container type in this way can substantially reduce
    iteration performance, especially for large containers and/or hot loops.  Non-erased
    ranges should therefore be preferred whenever possible, and erasure should be
    considered only as a last resort to satisfy the type checker.

    Bertrand instantiates this type internally when generating bindings for ranges that
    have no direct equivalent in the target language, and therefore cannot be translated
    normally.  In that case, as long as the range's yield type is a valid expression in
    the other language, then the rest of the range interface can be abstracted away, and
    the binding can be generated anyway, albeit at a performance cost. */
    template <meta::not_void T, impl::sequence_flags flags = {}> requires (meta::not_rvalue<T>)
    struct sequence : range<impl::sequence<T>>, impl::sequence_tag {
        using __type = range<impl::sequence<T>>::__type;

        /* Initializing a sequence from an rvalue container will move the container onto
        the heap, where it will be reference counted until it is no longer needed.  If the
        initializer is an lvalue instead, then the sequence will simply take its address
        without relocating it or requiring an allocation.  This prevents a full copy of the
        container, but forces the user to manage the lifetime externally, ensuring that the
        underlying container always outlives the sequence. */
        template <meta::yields<T> C>
        [[nodiscard]] constexpr sequence(C&& c)
            noexcept (requires{
                {range<impl::sequence<T>>(impl::sequence<T>{std::forward<C>(c)})} noexcept;
            })
        :
            range<impl::sequence<T>>(impl::sequence<T>{std::forward<C>(c)})
        {}

        /* Swap the underlying containers for two sequences. */
        constexpr void swap(sequence& other) noexcept
            requires (requires{this->__value->swap(other.__value);})
        {
            this->__value->swap(other.__value);
        }

        /* True if the underlying container supports `size()` checks.  False otherwise. */
        [[nodiscard]] bool has_size() const noexcept
            requires (requires{this->__value->has_size();})
        {
            return this->__value->has_size();
        }

        /* Return the current size of the sequence, assuming `has_size()` evaluates to
        true.  If `has_size()` is false, and the program is compiled in debug mode, then
        this function will throw a TypeError. */
        [[nodiscard]] constexpr auto size() const {
            return this->__value->size();
        }

        /* Identical to `size()`, except that the result is a signed integer. */
        [[nodiscard]] constexpr auto ssize() const {
            return this->__value->ssize();
        }

        /* True if the underlying container supports `empty()` checks.  False otherwise. */
        [[nodiscard]] constexpr bool has_empty() const noexcept {
            return this->__value->has_empty();
        }

        /* Returns true if the underlying container is empty or false otherwise, assuming
        `has_empty()` evaluates to true.  If `has_empty()` is false, and the program is
        compiled in debug mode, then this function will throw a TypeError. */
        [[nodiscard]] constexpr bool empty() const {
            return this->__value->empty();
        }

        /* True if the underlying container supports `operator[]` accessing.  False
        otherwise. */
        [[nodiscard]] constexpr bool has_subscript() const noexcept {
            return this->__value->has_subscript();
        }

        /* Index into the sequence, applying Python-style wraparound for negative
        indices if the sequence has a known size.  Otherwise, the index must be
        non-negative, and will be converted to `size_type`.  If the index is out of
        bounds after normalizing, and the program is compiled in debug mode, then this
        function will throw a TypeError. */
        [[nodiscard]] constexpr T operator[](__type::index_type i) const {
            return (*this->__value)[i];
        }
    };

    template <meta::iterable C>
    sequence(C&& c) -> sequence<
        meta::remove_rvalue<meta::yield_type<C>>,
        impl::infer_sequence_flags<C>
    >;

}


}


namespace std {

    namespace ranges {

        template <>
        inline constexpr bool enable_borrowed_range<bertrand::impl::empty_range> = true;

        /// TODO: borrowed range support for single ranges and optionals if the
        /// underlying type is an lvalue or models borrowed_range.

        template <typename Start, typename Stop, typename Step>
        constexpr bool enable_borrowed_range<bertrand::impl::iota<Start, Stop, Step>> = true;

        template <typename C>
        constexpr bool enable_borrowed_range<bertrand::iter::range<C>> =
            std::ranges::borrowed_range<C>;

        template <typename C>
        constexpr bool enable_borrowed_range<bertrand::impl::unpack<C>> =
            std::ranges::borrowed_range<C>;

    }

    template <>
    struct tuple_size<bertrand::impl::empty_range> : std::integral_constant<size_t, 0> {};

    template <typename T>
    struct tuple_size<bertrand::impl::single_range<T>> : std::integral_constant<size_t, 1> {};

    template <typename T>
    struct tuple_size<bertrand::impl::tuple_range<T>> : std::integral_constant<
        size_t,
        bertrand::meta::tuple_size<T>
    > {};

    template <bertrand::meta::tuple_like T>
    struct tuple_size<bertrand::iter::range<T>> : std::integral_constant<
        size_t,
        bertrand::meta::tuple_size<T>
    > {};

    template <size_t I>
    struct tuple_element<I, bertrand::impl::empty_range> {
        using type = const bertrand::NoneType&;
    };

    template <size_t I, typename T> requires (I == 0)
    struct tuple_element<I, bertrand::impl::single_range<T>> {
        using type = T;
    };

    template <size_t I, typename T> requires (I < bertrand::meta::tuple_size<T>)
    struct tuple_element<I, bertrand::impl::tuple_range<T>> {
        using type = decltype((bertrand::meta::unpack_tuple<I>(std::declval<T>())));
    };

    template <size_t I, bertrand::meta::tuple_like T> requires (I < bertrand::meta::tuple_size<T>)
    struct tuple_element<I, bertrand::iter::range<T>> {
        using type = decltype((bertrand::meta::unpack_tuple<I>(std::declval<T>())));
    };

    template <ssize_t I, bertrand::meta::range R>
        requires (
            bertrand::meta::tuple_like<R> &&
            bertrand::impl::valid_index<bertrand::meta::tuple_size<R>, I>
        )
    constexpr decltype(auto) get(R&& r)
        noexcept (requires{{bertrand::meta::unpack_tuple<I>(std::forward<R>(r))} noexcept;})
        requires (requires{{bertrand::meta::unpack_tuple<I>(std::forward<R>(r))};})
    {
        return (bertrand::meta::unpack_tuple<I>(std::forward<R>(r)));
    }

}


namespace bertrand {

// static constexpr auto arr = std::array{
//     std::array{1, 2},
//     std::array{2, 3},
//     std::array{3, 4}
// };
// static constexpr auto y3 = iter::at<0, [](int x) { return x == 2; }>{}(arr);
// static_assert(y3 == 2);


// static constexpr std::tuple tup {1, 2, 3.5};
// static constexpr std::tuple tup2 {std::tuple{1, 2}, std::tuple{2, 2}, std::tuple{3.5, 3.5}};
// static constexpr auto r1 = iter::range(tup2);
// static constexpr auto r2 = iter::range(iter::range(tup2));
// static constexpr auto x1 = r1[0];
// static constexpr auto x2 = r2[0];
// static constexpr auto y1 = r1[0, 1];
// static constexpr auto y2 = r2[0, 1];
// static constexpr auto y4 = iter::at<2, 1>{}(tup2);
// static constexpr auto y5 = r1[0, [](int x) { return x == 2; }];
// static_assert(y2 == y1);
// static_assert(y5 == 2);
// static_assert([] {
//     for (auto&& x : r2) {
//         for (auto&& y : x) {
//             if (y != 1 && y != 2 && y != 3.5) {
//                 return false;
//             }
//         }
//     }

//     // for (auto&& i : iter::range(tup)) {
//     //     if (i != 1 && i != 2 && i != 3.5) {
//     //         return false;
//     //     }
//     // }
//     // // if (iter::range<Optional<int>>().size() == 0) {
//     // //     return false;
//     // // }
//     // iter::range r = std::tuple{1, 2, 3};
//     // for (auto&& i : iter::range(5)) {
//     //     if (i != 5) {
//     //         return false;
//     //     }
//     // }
//     return true;
// }());


// static constexpr iter::range r10 = std::tuple<int, double>{1, 2.5};
// // static_assert(r10[1] == 2.5);




static constexpr std::array<int, 3> arr {1, 2, 3};

static constexpr auto r11 = impl::iota(arr.begin(), 3, [](int x) { return x % 2 == 1; });
static constexpr auto r12 = iter::range(arr.begin(), 3);
// static constexpr auto r13 = iter::range(arr);
// static_assert(r11.size() == 3);
// static_assert(r12.size() == 2);


// static_assert(meta::random_access_iterator<decltype(r12.begin())>);
// static_assert(meta::contiguous_iterator<decltype(r12.begin())>);
// static_assert(std::ranges::contiguous_range<decltype(r12)>);


static_assert([] {
    // auto it = r12.begin();
    // if (*it != 1) return false;
    // it += 1;
    // if (*it != 2) return false;
    // it -= 1;
    // if (*it != 1) return false;

    if (r12[-3] != 1) return false;

    for (auto&& i : r12) {
        if (i != 1 && i != 2 && i != 3) {
            return false;
        }
    }
    return true;
}());


static constexpr auto r13 = iter::range(1.5, 3.5, 1.5);
static_assert(r13->start() < r13->stop());
static_assert(!r13.empty());


static_assert([] {
    for (auto&& i : iter::range(1.5, 3.5, 1.5)) {
        if (i != 1.5 && i != 3.0) {
            return false;
        }
    }
    return true;
}());


}


#endif  // BERTRAND_ITER_RANGE_H