#ifndef BERTRAND_ITER_RANGE_H
#define BERTRAND_ITER_RANGE_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/union.h"


namespace bertrand {


namespace impl {
    struct range_tag {};

    /// TODO: perhaps the same inheritance trick can be used to simplify `unpack` similar
    /// to `range`

    template <meta::not_rvalue C> requires (meta::iterable<C>)
    struct unpack;

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

    constexpr void swap(empty_range& lhs, empty_range& rhs) noexcept {}

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

    template <typename T>
    constexpr void swap(single_range<T>& lhs, single_range<T>& rhs)
        noexcept (requires{{lhs.swap(rhs)} noexcept;})
        requires (requires{{lhs.swap(rhs)};})
    {
        lhs.swap(rhs);
    }

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
    constexpr void swap(tuple_range<T>& lhs, tuple_range<T>& rhs)
        noexcept (requires{{lhs.swap(rhs)} noexcept;})
        requires (requires{{lhs.swap(rhs)};})
    {
        lhs.swap(rhs);
    }

    /* A tag indicating an iota without an upper bound, which will increment forever,
    forming an infinite loop.  Typically, this type is listed as an implicit default
    for the `range(start, {})` and `range(start, {}, step)` iota constructors.
    Providing an empty initializer for the `stop` index will trigger this
    specialization, without interfering with the other iota constructors. */
    struct infinite {};


    /// TODO: enable_borrowed_range should always be enabled for iotas.



    template <typename T>
    concept strictly_positive = meta::unsigned_integer<T> || !requires(meta::as_const_ref<T> t) {
        {t < 0} -> meta::explicitly_convertible_to<bool>;
    };

    template <typename Start, typename Step>
    concept iota_increment_simple = requires(Start start, Step step) {
        {start += std::forward<Step>(step)};
    };
    template <typename Start, typename Step>
    concept iota_increment_by_count = requires(Start start, Step step, meta::unqualify<Step> i) {
        {meta::unqualify<Step>{}};
        {i < step} -> meta::explicitly_convertible_to<bool>;
        {++i};
        {++start};
    } && (strictly_positive<Step> || requires(Start start, Step step, meta::unqualify<Step> i) {
        {step < 0} -> meta::explicitly_convertible_to<bool>;
        {i > step} -> meta::explicitly_convertible_to<bool>;
        {--i};
        {--start};
    });
    /// TODO: figure out how masks and predicate functions need to work
    // template <typename Start, typename Step>
    // concept iota_increment_mask = requires(Start start, Step step) {
    //     {++start};
    // };
    // template <typename Start, typename Step>
    // concept iota_increment_predicate = requires(Start start, meta::as_const_ref<Step> step) {
    //     {++start};
    //     {step(start)} -> meta::convertible_to<bool>;
    // };

    template <typename Start, typename Step>
    concept iota_decrement_simple = requires(Start start, Step step) {
        {start -= std::forward<Step>(step)};
    };
    template <typename Start, typename Step>
    concept iota_decrement_by_count = requires(Start start, Step step, meta::unqualify<Step> i) {
        {meta::unqualify<Step>{}};
        {i > step} -> meta::explicitly_convertible_to<bool>;
        {--i};
        {--start};
    } && (strictly_positive<Step> || requires(Start start, Step step, meta::unqualify<Step> i) {
        {step < 0} -> meta::explicitly_convertible_to<bool>;
        {i < step} -> meta::explicitly_convertible_to<bool>;
        {++i};
        {++start};
    });
    /// TODO: figure out how masks and predicate functions need to work
    // template <typename Start, typename Step>
    // concept iota_decrement_predicate = requires(Start start, meta::as_const_ref<Step> step) {
    //     {--start};
    //     {step(start)} -> meta::convertible_to<bool>;
    // };


    /// TODO: there are several implementations needed to robustly detect whether an
    /// iota is empty or not.  These should possibly be hidden under `iota_value` where
    /// appropriate.

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
            requires{
                {step < 0} -> meta::explicitly_convertible_to<bool>;
                {!(start > stop)} -> meta::convertible_to<bool>;
            }
        ))
    {
        if constexpr (!strictly_positive<Step>) {
            if (step < 0) {
                return !(start > stop);
            }
        }
        return !(start < stop);
    }

    /* A wrapper for an arbitrary iterator that allows it to be used as the start index
    of a counted range, where the stop index is given as another integer. */
    template <meta::unqualified T> requires (meta::iterator<T>)
    struct iota_counted {
        using iterator_category = meta::iterator_category<T>;
        using difference_type = meta::iterator_difference_type<T>;
        using value_type = meta::iterator_value_type<T>;
        using reference = meta::iterator_reference_type<T>;
        using pointer = meta::iterator_pointer_type<T>;

        T iter;
        difference_type index = 0;

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).iter} noexcept;})
            requires (requires{{*std::forward<Self>(self).iter};})
        {
            return (*std::forward<Self>(self).iter);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{impl::arrow_proxy(*std::forward<Self>(self))} noexcept;})
            requires (requires{{impl::arrow_proxy(*std::forward<Self>(self))};})
        {
            return impl::arrow_proxy(*std::forward<Self>(self));
        }

        template <typename Self>        
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, ssize_t n)
            noexcept (requires{{std::forward<Self>(self).iter[n]} noexcept;})
            requires (requires{{std::forward<Self>(self).iter[n]};})
        {
            return (std::forward<Self>(self).iter[n]);
        }

        constexpr iota_counted& operator++()
            noexcept (requires{{++iter} noexcept;})
            requires (requires{{++iter};})
        {
            ++iter;
            ++index;
            return *this;
        }

        [[nodiscard]] constexpr iota_counted operator++(int)
            noexcept (requires{
                {iota_counted{*this}} noexcept;
                {++iter} noexcept;
            })
            requires (requires{
                {iota_counted(*this)};
                {++iter};
            })
        {
            iota_counted tmp = *this;
            ++tmp;
            return tmp;
        }

        [[nodiscard]] friend constexpr iota_counted operator+(
            const iota_counted& self,
            difference_type n
        )
            noexcept (requires{{iota_counted{self.iter + n, self.index + n}} noexcept;})
            requires (requires{{iota_counted{self.iter + n, self.index + n}};})
        {
            return {self.iter + n, self.index + n};
        }

        [[nodiscard]] friend constexpr iota_counted operator+(
            difference_type n,
            const iota_counted& self
        )
            noexcept (requires{{iota_counted{self.iter + n, self.index + n}} noexcept;})
            requires (requires{{iota_counted{self.iter + n, self.index + n}};})
        {
            return {self.iter + n, self.index + n};
        }

        constexpr iota_counted& operator+=(difference_type n)
            noexcept (requires{{iter + n} noexcept;})
            requires (requires{{iter + n};})
        {
            iter += n;
            index += n;
            return *this;
        }

        constexpr iota_counted& operator--()
            noexcept (requires{{--iter} noexcept;})
            requires (requires{{--iter};})
        {
            --iter;
            --index;
            return *this;
        }

        [[nodiscard]] constexpr iota_counted operator--(int)
            noexcept (requires{
                {iota_counted{*this}} noexcept;
                {--iter} noexcept;
            })
            requires (requires{
                {iota_counted(*this)};
                {--iter};
            })
        {
            iota_counted tmp = *this;
            --tmp;
            return tmp;
        }

        [[nodiscard]] constexpr iota_counted operator-(difference_type n) const
            noexcept (requires{{iota_counted{iter - n, index - n}} noexcept;})
            requires (requires{{iota_counted{iter - n, index - n}};})
        {
            return {iter - n, index - n};
        }

        [[nodiscard]] constexpr difference_type operator-(const iota_counted& other) const
            noexcept (requires{{index - other.index} noexcept;})
            requires (requires{{index - other.index};})
        {
            return index - other.index;
        }

        constexpr iota_counted& operator-=(difference_type n)
            noexcept (requires{{iter - n} noexcept;})
            requires (requires{{iter - n};})
        {
            iter -= n;
            index -= n;
            return *this;
        }

        [[nodiscard]] constexpr bool operator==(difference_type stop) const noexcept {
            return index == stop;
        }

        [[nodiscard]] constexpr auto operator<=>(difference_type stop) const noexcept {
            return index <=> stop;
        }
    };

    /* A wrapper for the `start` index of an iota object, which abstract the increment
    and decrement operators with respect to arbitrary step sizes. */
    template <meta::not_rvalue T>
    struct iota_value {
        using reference = meta::as_lvalue<T>;
        using const_reference = meta::as_const_ref<T>;

        [[no_unique_address]] impl::ref<T> value;

        constexpr void swap(iota_value& other)
            noexcept (requires{{value.swap(other.value)} noexcept;})
            requires (requires{{value.swap(other.value)};})
        {
            value.swap(other.value);
        }

        constexpr iota_value& operator++() {
            ++*value;
            return *this;
        }

        template <typename Step>
        constexpr iota_value& operator+=(Step&& step)
            noexcept (requires{{*value += std::forward<Step>(step)} noexcept;})
            requires (iota_increment_simple<reference, Step>)
        {
            *value += std::forward<Step>(step);
            return *this;
        }

        template <typename Step>
        constexpr iota_value& operator+=(Step&& step)
            noexcept (requires(meta::unqualify<Step> i) {
                {meta::unqualify<Step>{}} noexcept;
                {i < step} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {++i} noexcept;
                {++*value} noexcept;
            } && (strictly_positive<Step> || requires(meta::unqualify<Step> i) {
                {step < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {i > step} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {--i} noexcept;
                {--*value} noexcept;
            }))
            requires (
                !iota_increment_simple<reference, Step> &&
                iota_increment_by_count<reference, Step>
            )
        {
            if constexpr (strictly_positive<Step>) {
                for (meta::unqualify<Step> i = {}; i < step; ++i) {
                    ++*value;
                }
            } else {
                if (step < 0) {
                    for (meta::unqualify<Step> i = {}; i > step; --i) {
                        --*value;
                    }
                } else {
                    for (meta::unqualify<Step> i = {}; i < step; ++i) {
                        ++*value;
                    }
                }
            }
            return *this;
        }

        constexpr iota_value& operator--()
            noexcept (requires{{--*value} noexcept;})
            requires (requires{{--*value};})
        {
            --*value;
            return *this;
        }

        template <typename Step>
        constexpr iota_value& operator-=(Step&& step)
            noexcept (requires{{*value -= std::forward<Step>(step)} noexcept;})
            requires (iota_decrement_simple<reference, Step>)
        {
            *value -= std::forward<Step>(step);
            return *this;
        }

        template <typename Step>
        constexpr iota_value& operator-=(Step&& step)
            noexcept (requires(meta::unqualify<Step> i) {
                {meta::unqualify<Step>{}} noexcept;
                {i < step} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {++i} noexcept;
                {++*value} noexcept;
            } && (strictly_positive<Step> || requires(meta::unqualify<Step> i) {
                {step < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {i > step} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {--i} noexcept;
                {--*value} noexcept;
            }))
            requires (
                !iota_decrement_simple<reference, Step> &&
                iota_decrement_by_count<reference, Step>
            )
        {
            if constexpr (strictly_positive<Step>) {
                for (meta::unqualify<Step> i = {}; i > step; --i) {
                    --*value;
                }
            } else {
                if (step < 0) {
                    for (meta::unqualify<Step> i = {}; i < step; ++i) {
                        ++*value;
                    }
                } else {
                    for (meta::unqualify<Step> i = {}; i > step; --i) {
                        --*value;
                    }
                }
            }
            return *this;
        }

        template <typename Step>
        [[nodiscard]] constexpr meta::unqualify<T> operator+(Step&& step) const
            noexcept (requires{{
                *value + std::forward<Step>(step)
            } noexcept -> meta::nothrow::convertible_to<meta::unqualify<T>>;})
            requires (requires{{
                *value + std::forward<Step>(step)
            } -> meta::convertible_to<meta::unqualify<T>>;})
        {
            return *value + std::forward<Step>(step);
        }

        template <typename Step>
        [[nodiscard]] constexpr meta::unqualify<T> operator+(Step&& step) const
            noexcept (requires(iota_value tmp) {
                {iota_value{value}} noexcept;
                {tmp += std::forward<Step>(step)} noexcept;
                {meta::unqualify<T>{*std::move(tmp).value}} noexcept;
            })
            requires (!requires{{
                *value + std::forward<Step>(step)
            } -> meta::convertible_to<meta::unqualify<T>>;} && requires(iota_value tmp) {
                {iota_value{value}};
                {tmp += std::forward<Step>(step)};
                {meta::unqualify<T>{*std::move(tmp).value}};
            })
        {
            iota_value tmp {value};
            tmp += std::forward<Step>(step);
            return *std::move(tmp).value;
        }

        template <typename Step>
        [[nodiscard]] constexpr meta::unqualify<T> operator-(Step&& step) const
            noexcept (requires{{
                *value - std::forward<Step>(step)
            } noexcept -> meta::nothrow::convertible_to<meta::unqualify<T>>;})
            requires (requires{{
                *value - std::forward<Step>(step)
            } -> meta::convertible_to<meta::unqualify<T>>;})
        {
            return *value - std::forward<Step>(step);
        }

        template <typename Step>
        [[nodiscard]] constexpr meta::unqualify<T> operator-(Step&& step) const
            noexcept (requires(iota_value tmp) {
                {iota_value{value}} noexcept;
                {tmp -= std::forward<Step>(step)} noexcept;
                {meta::unqualify<T>{*std::move(tmp).value}} noexcept;
            })
            requires (!requires{{
                *value - std::forward<Step>(step)
            } -> meta::convertible_to<meta::unqualify<T>>;} && requires(iota_value tmp) {
                {iota_value{value}};
                {tmp -= std::forward<Step>(step)};
                {meta::unqualify<T>{*std::move(tmp).value}};
            })
        {
            iota_value tmp {value};
            tmp -= std::forward<Step>(step);
            return *std::move(tmp).value;
        }

        /// TODO: distance(stop), which will be used instead of multiplying by
        /// size.



        /// TODO: comparisons don't account for the sign of the step size.  That's
        /// done elsewhere


        [[nodiscard]] constexpr bool operator<(infinite) const noexcept { return true; }

        template <typename Stop>
        [[nodiscard]] constexpr bool operator<(const Stop& stop) const
            noexcept (requires{{*value < stop} noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{*value < stop} -> meta::convertible_to<bool>;})
        {
            return *value < stop;
        }

        [[nodiscard]] constexpr bool operator<=(infinite) const noexcept { return true; }

        template <typename Stop>
        [[nodiscard]] constexpr bool operator<=(const Stop& stop) const
            noexcept (requires{{*value <= stop} noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{*value <= stop} -> meta::convertible_to<bool>;})
        {
            return *value <= stop;
        }

        [[nodiscard]] constexpr bool operator==(infinite) const noexcept { return false; }

        template <typename Stop>
        [[nodiscard]] constexpr bool operator==(const Stop& stop) const
            noexcept (requires{{*value == stop} noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{*value == stop} -> meta::convertible_to<bool>;})
        {
            return *value == stop;
        }

        [[nodiscard]] constexpr bool operator!=(infinite) const noexcept { return true; }

        template <typename Stop>
        [[nodiscard]] constexpr bool operator!=(const Stop& stop) const
            noexcept (requires{{*value != stop} noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{*value != stop} -> meta::convertible_to<bool>;})
        {
            return *value != stop;
        }

        [[nodiscard]] constexpr bool operator>=(infinite) const noexcept { return false; }

        template <typename Stop>
        [[nodiscard]] constexpr bool operator>=(const Stop& stop) const
            noexcept (requires{{*value >= stop} noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{*value >= stop} -> meta::convertible_to<bool>;})
        {
            return *value >= stop;
        }

        [[nodiscard]] constexpr bool operator>(infinite) const noexcept { return false; }

        template <typename Stop>
        [[nodiscard]] constexpr bool operator>(const Stop& stop) const
            noexcept (requires{{*value > stop} noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{*value > stop} -> meta::convertible_to<bool>;})
        {
            return *value > stop;
        }
    };

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

    /* Iota iterators will use the difference type between `stop` and `start` if
    available and integer-like.  Otherwise, if `stop` is `infinite`, and start has an
    integer difference with respect to itself, then we use that type.  Lastly, we
    default to `std::ptrdiff_t` as a fallback. */
    template <typename Start, typename Stop>
    struct iota_difference { using type = std::ptrdiff_t; };
    template <typename Start, typename Stop>
        requires (requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {stop - start} -> meta::integer;
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
            {start - start} -> meta::integer;
        })
    struct iota_difference<Start, Stop> {
        using type = decltype(
            std::declval<meta::as_const_ref<Start>>() -
            std::declval<meta::as_const_ref<Stop>>()
        );
    };

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

    /* Iota iterators default to modeling `std::input_iterator` only.  If `Start` is
    comparable with itself, then the iterator can be upgraded to model
    `std::forward_iterator`.  If `Start` is also decrementable, then the iterator can
    be further upgraded to model `std::bidirectional_iterator`.  Lastly, if `Start`
    also supports addition, subtraction, and ordered comparisons, then the iterator can
    be upgraded to model `std::random_access_iterator`. */
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

    /* The `->` operator for iota iterators will prefer to recursively call the same
    iterator on `start`. */
    template <typename T>
    struct iota_pointer { using type = meta::as_pointer<T>; };
    template <meta::has_arrow T>
    struct iota_pointer<T> { using type = meta::arrow_type<T>; };
    template <meta::has_address T> requires (!meta::has_arrow<T>)
    struct iota_pointer<T> { using type = meta::address_type<T>; }; 

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
        [[no_unique_address]] iota_value<Start> start;
        [[no_unique_address]] impl::ref<Stop> stop;
        [[no_unique_address]] impl::ref<Step> step;

        [[nodiscard]] constexpr iota()
            noexcept (requires{
                {iota_value<Start>{}} noexcept;
                {impl::ref<Stop>{}} noexcept;
                {impl::ref<Step>{1}} noexcept;
            })
            requires (requires{
                {iota_value<Start>{}};
                {impl::ref<Stop>{}};
                {impl::ref<Step>{1}};
            })
        :
            start{},
            stop{},
            step{1}
        {};
        [[nodiscard]] constexpr iota(
            meta::forward<Start> start,
            meta::forward<Stop> stop,
            meta::forward<Step> step
        )
            noexcept (!DEBUG && requires{
                {iota_value<Start>{std::forward<Start>(start)}} noexcept;
                {impl::ref<Stop>{std::forward<Stop>(stop)}} noexcept;
                {impl::ref<Step>{std::forward<Step>(step)}} noexcept;
            })
        :
            start{std::forward<Start>(start)},
            stop{std::forward<Stop>(stop)},
            step{std::forward<Step>(step)}
        {
            if constexpr (
                DEBUG &&
                requires{{*this->step == 0} -> meta::explicitly_convertible_to<bool>;}
            ) {
                if (*this->step == 0) {
                    throw ValueError("step size cannot be zero");
                }
            }
        }

        constexpr void swap(iota& other)
            noexcept (requires{
                {start.swap(other.start)} noexcept;
                {stop.swap(other.stop)} noexcept;
                {step.swap(other.step)} noexcept;
            })
            requires (requires{
                {start.swap(other.start)};
                {stop.swap(other.stop)};
                {step.swap(other.step)};
            })
        {
            start.swap(other.start);
            stop.swap(other.stop);
            step.swap(other.step);
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{iota_empty(start, stop, step)} noexcept;})
            requires (requires{{iota_empty(start, stop, step)};})
        {
            return iota_empty(start, stop, step);
        }

        [[nodiscard]] constexpr size_t size() const
            noexcept (requires{{size_t((*stop - *start.value) / *step) * !empty()} noexcept;})
            requires (requires{{size_t((*stop - *start.value) / *step) * !empty()};})
        {
            return size_t((*stop - *start.value) / *step) * !empty();
        }

        [[nodiscard]] constexpr ssize_t ssize() const
            noexcept (requires{{ssize_t((*stop - *start.value) / *step) * !empty()} noexcept;})
            requires (requires{{ssize_t((*stop - *start.value) / *step) * !empty()};})
        {
            return ssize_t((*stop - *start.value) / *step) * !empty();
        }

        [[nodiscard]] constexpr decltype(auto) operator[](size_t i) const
            noexcept (requires{{start + *step * i} noexcept;})
            requires (requires{{start + *step * i};})
        {
            return (start + *step * i);
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{iota_iterator<Start, Stop, Step>{start, stop, step}} noexcept;})
            requires (requires{{iota_iterator<Start, Stop, Step>{start, stop, step}};})
        {
            return iota_iterator<Start, Stop, Step>{start, stop, step};
        }

        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }
    };


    /// TODO: the 2-argument specialization of `iota` should be pretty much good to go
    /// outside of the concept and iterator refactors, which are still ongoing.

    /* A specialization of `iota` that lacks a step size.  This causes the iteration
    algorithm to use prefix `++` rather than `+= step` to get the next value, which
    allows us to ignore negative step sizes and increase performance. */
    template <typename Start, typename Stop> requires (iota_concept<Start, Stop, void>)
    struct iota<Start, Stop, void> {
        [[no_unique_address]] iota_value<Start> start;
        [[no_unique_address]] impl::ref<Stop> stop;

        [[nodiscard]] constexpr iota() = default;
        [[nodiscard]] constexpr iota(meta::forward<Stop> stop)
            noexcept (requires{
                {iota_value<Start>{}} noexcept;
                {impl::ref<Stop>{std::forward<Stop>(stop)}} noexcept;
            })
            requires (requires{
                {iota_value<Start>{}};
                {impl::ref<Stop>{std::forward<Stop>(stop)}};
            })
        :
            start{},
            stop{std::forward<Stop>(stop)}
        {}
        [[nodiscard]] constexpr iota(meta::forward<Start> start, meta::forward<Stop> stop)
            noexcept (requires{
                {iota_value<Start>{std::forward<Start>(start)}} noexcept;
                {impl::ref<Stop>{std::forward<Stop>(stop)}} noexcept;
            })
            requires (requires{
                {iota_value<Start>{std::forward<Start>(start)}};
                {impl::ref<Stop>{std::forward<Stop>(stop)}};
            })
        :
            start{std::forward<Start>(start)},
            stop{std::forward<Stop>(stop)}
        {}

        constexpr void swap(iota& other)
            noexcept (requires{
                {start.swap(other.start)} noexcept;
                {stop.swap(other.stop)} noexcept;
            })
            requires (requires{
                {start.swap(other.start)};
                {stop.swap(other.stop)};
            })
        {
            start.swap(other.start);
            stop.swap(other.stop);
        }

        [[nodiscard]] constexpr size_t size() const
            noexcept (requires{{size_t(*stop - *start.value)} noexcept;})
            requires (requires{{size_t(*stop - *start.value)};})
        {
            return size_t(*stop - *start.value);
        }

        [[nodiscard]] constexpr size_t size() const
            noexcept (requires{{size_t(*stop)} noexcept;})
            requires (!requires{{size_t(*stop - *start.value)};} && requires{{size_t(*stop)};})
        {
            return size_t(*stop);
        }

        [[nodiscard]] constexpr ssize_t ssize() const
            noexcept (requires{{ssize_t(size())} noexcept;})
            requires (requires{{ssize_t(size())};})
        {
            return ssize_t(size());
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{start == *stop} noexcept;})
            requires (requires{{start == *stop};})
        {
            return start == *stop;
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{size() == 0} noexcept;})
            requires (!requires{{start == *stop};} && requires{{size() == 0};})
        {
            return size() == 0;
        }

        [[nodiscard]] constexpr decltype(auto) operator[](size_t i) const
            noexcept (requires{{start + i} noexcept;})
            requires (requires{{start + i};})
        {
            return (start + i);
        }

        /// TODO: if we're in the counted case, then the iterator type should wrap the
        /// `start` index in an `iota_counted` class.

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{iota_iterator<Start, Stop, void>{start, stop}} noexcept;})
            requires (requires{{iota_iterator<Start, Stop, void>{start, stop}};})
        {
            return iota_iterator<Start, Stop, void>{start, stop};
        }

        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }
    };

    template <typename Stop> requires (iota_concept<Stop, Stop, void>)
    iota(Stop) -> iota<Stop, Stop, void>;

    template <typename Start, typename Stop> requires (iota_concept<Start, Stop, void>)
    iota(Start, Stop) -> iota<Start, Stop, void>;

    template <typename Start, typename Stop, typename Step>
        requires (iota_concept<Start, Stop, Step>)
    iota(Start, Stop, Step) -> iota<Start, Stop, Step>;

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


namespace iter {

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

    template <typename Start, typename Stop = impl::infinite>
    range(Start&&, Stop&&) -> range<impl::iota<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        void
    >>;

    template <typename Start, typename Stop = impl::infinite, typename Step>
    range(Start&&, Stop&&, Step&&) -> range<impl::iota<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >>;

}


namespace impl {

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

    template <meta::not_rvalue T>
    struct range_iterator;

    template <meta::iterator T>
    constexpr range_iterator<meta::remove_rvalue<T>> make_range_iterator(T&& iter)
        noexcept (meta::nothrow::constructible_from<range_iterator<meta::remove_rvalue<T>>, T>)
        requires (meta::constructible_from<range_iterator<meta::remove_rvalue<T>>, T>)
    {
        return {std::forward<T>(iter)};
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
            noexcept (requires{{impl::arrow_proxy(*iter)} noexcept;})
            requires (requires{{impl::arrow_proxy(*iter)};})
        {
            return impl::arrow_proxy(*iter);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow_proxy(*iter)} noexcept;})
            requires (requires{{impl::arrow_proxy(*iter)};})
        {
            return impl::arrow_proxy(*iter);
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

    template <meta::iterator T>
    constexpr unpack_iterator<meta::remove_rvalue<T>> make_unpack_iterator(T&& iter)
        noexcept (meta::nothrow::constructible_from<unpack_iterator<meta::remove_rvalue<T>>, T>)
        requires (meta::constructible_from<unpack_iterator<meta::remove_rvalue<T>>, T>)
    {
        return {std::forward<T>(iter)};
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
            noexcept (requires{{impl::arrow_proxy(*iter)} noexcept;})
            requires (requires{{impl::arrow_proxy(*iter)};})
        {
            return impl::arrow_proxy(*iter);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow_proxy(*iter)} noexcept;})
            requires (requires{{impl::arrow_proxy(*iter)};})
        {
            return impl::arrow_proxy(*iter);
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

    template <typename C>
    constexpr void swap(unpack<C>& lhs, unpack<C>& rhs)
        noexcept (requires{{lhs.swap(rhs)} noexcept;})
        requires (requires{{lhs.swap(rhs)};})
    {
        lhs.swap(rhs);
    }




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
            requires (!meta::range<C> && meta::constructible_from<impl::ref<C>, A...>)
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
            noexcept (requires{{meta::to_arrow(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::to_arrow(*std::forward<Self>(self).__value)};})
        {
            return meta::to_arrow(*std::forward<Self>(self).__value);
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

        /// TODO: documentation for the tuple-like and runtime index operators should
        /// reflect their newfound symmetry with `iter::at{}`.

        /* Forwarding `get<I>()` accessor, provided the underlying container is
        tuple-like.  Automatically applies Python-style wraparound for negative
        indices, and allows multidimensional indexing if the container is a tuple of
        tuples. */
        template <auto... Is, typename Self>
        constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{at<Is...>{}(std::forward<Self>(self))} noexcept;})
            requires (requires{{at<Is...>{}(std::forward<Self>(self))};})
        {
            return (at<Is...>{}(std::forward<Self>(self)));
        }

        /* Integer indexing operator.  Accepts one or more signed integers and
        retrieves the corresponding element from the underlying container after
        applying Python-style wraparound for negative indices, which converts the index
        to an unsigned integer.  If multiple indices are given, then each successive
        index after the first will be used to subscript the previous result. */
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

    /// TODO: document these specializations of range.

    template <meta::not_rvalue C>
        requires (!meta::range<C> && !meta::iterable<C> && meta::tuple_like<C>)
    struct range<C> : range<impl::tuple_range<C>> {
        using range<impl::tuple_range<C>>::range;
    };

    template <meta::not_rvalue C>
        requires (!meta::range<C> && !meta::iterable<C> && !meta::tuple_like<C>)
    struct range<C> : range<impl::single_range<C>> {
        using range<impl::single_range<C>>::range;
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


}


namespace std {

    /// TODO: enable_borrowed_range

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

static constexpr auto arr = std::array{
    std::array{1, 2},
    std::array{2, 3},
    std::array{3, 4}
};
static constexpr auto y3 = iter::at<0, [](int x) { return x == 2; }>{}(arr);
static_assert(y3 == 2);


static constexpr std::tuple tup {1, 2, 3.5};
static constexpr std::tuple tup2 {std::tuple{1, 2}, std::tuple{2, 2}, std::tuple{3.5, 3.5}};
static constexpr auto r1 = iter::range(tup2);
static constexpr auto r2 = iter::range(iter::range(tup2));
static constexpr auto x1 = r1[0];
static constexpr auto x2 = r2[0];
static constexpr auto y1 = r1[0, 1];
static constexpr auto y2 = r2[0, 1];
static constexpr auto y4 = iter::at<2, 1>{}(tup2);
static constexpr auto y5 = r1[0, [](int x) { return x == 2; }];
static_assert(y2 == y1);
static_assert(y5 == 2);
static_assert([] {
    for (auto&& x : r2) {
        for (auto&& y : x) {
            if (y != 1 && y != 2 && y != 3.5) {
                return false;
            }
        }
    }

    // for (auto&& i : iter::range(tup)) {
    //     if (i != 1 && i != 2 && i != 3.5) {
    //         return false;
    //     }
    // }
    // // if (iter::range<Optional<int>>().size() == 0) {
    // //     return false;
    // // }
    // iter::range r = std::tuple{1, 2, 3};
    // for (auto&& i : iter::range(5)) {
    //     if (i != 5) {
    //         return false;
    //     }
    // }
    return true;
}());


static constexpr iter::range r10 = std::tuple<int, double>{1, 2.5};
// static_assert(r10[1] == 2.5);


}


#endif  // BERTRAND_ITER_RANGE_H