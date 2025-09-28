#ifndef BERTRAND_ITER_RANGE_H
#define BERTRAND_ITER_RANGE_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/math.h"
#include "bertrand/union.h"


namespace bertrand {


namespace impl {
    struct range_tag {};
    struct single_range_tag {};
    struct tuple_range_tag {};
    struct iota_tag {};
    struct subrange_tag {};
    struct owning_subrange_tag {};
    struct unpack_tag {};
    struct sequence_tag {};

    template <typename T>
    constexpr bool range_transparent = false;

    template <meta::inherits<single_range_tag> T>
    constexpr bool range_transparent<T> = true;

    template <meta::inherits<tuple_range_tag> T>
    constexpr bool range_transparent<T> = true;

}


namespace meta {

    /* Detect whether a type is a `range`.  If additional types are provided, then they
    equate to a convertibility check against the range's yield type.  If more than one
    type is provided, then the yield type must be tuple-like, and destructurable to the
    given types. */
    template <typename T, typename... Rs>
    concept range = inherits<T, impl::range_tag> && (
        sizeof...(Rs) == 0 ||
        (sizeof...(Rs) == 1 && convertible_to<yield_type<T>, first_type<Rs...>>) ||
        structured_with<yield_type<T>, Rs...>
    );

    /* Detect the nesting level of a `range` as an unsigned integer.  Zero refers to
    no nesting (meaning the range directly adapts an underlying container), while 1
    indicates a range of ranges, and so on. */
    template <typename T>
    constexpr size_t nested_range = 0;
    template <meta::range T> requires (meta::range<decltype(*::std::declval<T>().__value)>)
    constexpr size_t nested_range<T> =
        nested_range<decltype(*::std::declval<T>().__value)> + 1;

    /* A refinement of `meta::range<T, Rs...>` that only matches iota ranges (i.e.
    those of the form `[start, stop[, step]]`, where `start` is not an iterator). */
    template <typename T, typename... Rs>
    concept iota = range<T, Rs...> && requires(T r) {
        {*r.__value} -> inherits<impl::iota_tag>;
    };

    /* A refinement of `meta::range<T, Rs...>` that only matches subranges (i.e.
    those of the form `[start, stop[, step]]`, where `start` is an iterator type). */
    template <typename T, typename... Rs>
    concept subrange = range<T, Rs...> && requires(T r) {
        {*r.__value} -> inherits<impl::subrange_tag>;
    };

    /* A refinement of `meta::subrange<T, Rs...>` that only matches subranges which
    own the underlying container, extending its lifespan.  Such ranges are heavier than
    their non-borrowed alternatives, and disable `std::ranges::borrowed_range`. */
    template <typename T, typename... Rs>
    concept owning_subrange = subrange<T, Rs...> && requires(T r) {
        {*r.__value} -> inherits<impl::owning_subrange_tag>;
    };

    /* A refinement of `meta::range<T, Rs...>` that only matches unpacked ranges, which
    are produced by the prefix `*` operator, and may have special effects when provided
    to a range algorithm or function call. */
    template <typename T, typename... Rs>
    concept unpack = range<T, Rs...> && inherits<T, impl::unpack_tag>;

    /* A refinement of `meta::range<T, Rs...>` that only matches type-erased sequences,
    where the underlying container type is hidden from the user. */
    template <typename T, typename... Rs>
    concept sequence = range<T, Rs...> && inherits<T, impl::sequence_tag>;

    /* A refinement of `meta::range<T, Rs...>` that specifies that the range's begin
    and end iterators are the same type.  Ranges of this form may be required for
    legacy algorithms, and simplify some iterator access patterns. */
    template <typename T, typename... Rs>
    concept common_range = range<T, Rs...> && ::std::ranges::common_range<T>;

    /* A refinement of `meta::range<T>` that specifies that the range's begin iterator
    satisfies `std::output_iterator`, meaning that its dereference type can be assigned
    to. */
    template <typename T, typename V>
    concept output_range = range<T> && output_iterator<begin_type<T>, V>;

    /* A refinement of `meta::range<T, Rs...>` that specifies that the range's begin
    iterator is equality comparable against itself. */
    template <typename T, typename... Rs>
    concept forward_range = range<T, Rs...> && forward_iterator<begin_type<T>>;

    /* A refinement of `meta::forward_range<T, Rs...>` that specifies that the range's
    begin iterator can be decremented as well as incremented. */
    template <typename T, typename... Rs>
    concept bidirectional_range =
        forward_range<T, Rs...> && bidirectional_iterator<begin_type<T>>;

    /* A refinement of `meta::bidirectional_range<T, Rs...>` that specifies that the
    range's begin iterator can be randomly accessed (i.e. advanced by more than one
    index at a time and supports distance, subscripting, etc.). */
    template <typename T, typename... Rs>
    concept random_access_range =
        bidirectional_range<T, Rs...> && random_access_iterator<begin_type<T>>;

    /* A refinement of `meta::random_access_range<T, Rs...>` that specifies that the
    range's begin iterator is contiguous (i.e. the elements are laid out in a single
    contiguous block of memory). */
    template <typename T, typename... Rs>
    concept contiguous_range =
        random_access_range<T, Rs...> && contiguous_iterator<begin_type<T>>;

    namespace detail {

        template <meta::range T>
        constexpr bool prefer_constructor<T> = true;

        template <meta::range T>
        constexpr bool wraparound<T> = true;

        /// TODO: declare wraparound<T> for each of the underlying containers, so that
        /// `range` never needs to do it more than once.
        /// -> wraparound may not be needed if ranges just forward to the container's
        /// indexing operator.


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
    struct single_range : single_range_tag {
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
            noexcept (requires{{std::ranges::swap(__value, other.__value)} noexcept;})
            requires (requires{{std::ranges::swap(__value, other.__value)};})
        {
            std::ranges::swap(__value, other.__value);
        }

        [[nodiscard]] constexpr size_t size() const noexcept { return __value != None; }
        [[nodiscard]] constexpr ssize_t ssize() const noexcept { return __value != None; }
        [[nodiscard]] constexpr bool empty() const noexcept { return __value == None; }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept {
            return (*std::forward<Self>(self).__value);
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

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).__value} noexcept;})
            requires (requires{{*std::forward<Self>(self).__value};})
        {
            return (*std::forward<Self>(self).__value);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) back(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).__value} noexcept;})
            requires (requires{{*std::forward<Self>(self).__value};})
        {
            return (*std::forward<Self>(self).__value);
        }

        template <size_t I, typename Self> requires (I == 0)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{meta::get<I>(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::get<I>(*std::forward<Self>(self).__value)};})
        {
            return (meta::get<I>(*std::forward<Self>(self).__value));
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
                noexcept (requires{
                    {meta::get<I>(c)} noexcept -> meta::nothrow::convertible_to<type>;
                })
            {
                return meta::get<I>(c);
            }
        };
        using dispatch = impl::basic_vtable<fn, meta::tuple_size<C>>;
    };

    template <typename>
    struct _tuple_range : tuple_range_tag {
        using type = const NoneType&;
        static constexpr tuple_kind kind = tuple_kind::EMPTY;
    };
    template <typename T, typename... Ts>
    struct _tuple_range<meta::pack<T, Ts...>> : tuple_range_tag {
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
            noexcept (requires{{C(std::forward<A>(args)...)} noexcept;})
            requires (requires{{C(std::forward<A>(args)...)};})
        :
            __value{C(std::forward<A>(args)...)}
        {}

        constexpr void swap(tuple_range& other)
            noexcept (requires{{std::ranges::swap(__value, other.__value)} noexcept;})
            requires (requires{{std::ranges::swap(__value, other.__value)};})
        {
            std::ranges::swap(__value, other.__value);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept {
            return (*std::forward<Self>(self).__value);
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
                {meta::get<Is>(*__value)} noexcept -> meta::nothrow::convertible_to<ref>;
            } && ...))
        {
            return {meta::get<Is>(*__value)...};
        }

    public:
        [[nodiscard]] constexpr tuple_range() = default;

        template <typename... A> requires (sizeof...(A) > 0)
        [[nodiscard]] constexpr tuple_range(A&&... args)
            noexcept (requires{{impl::ref<C>{{std::forward<A>(args)...}}} noexcept;})
            requires (requires{{impl::ref<C>{{std::forward<A>(args)...}}};})
        :
            __value{{std::forward<A>(args)...}},
            elements(init(std::make_index_sequence<size()>{}))
        {}

        constexpr void swap(tuple_range& other)
            noexcept (requires{
                {std::ranges::swap(__value, other.__value)} noexcept;
                {std::ranges::swap(elements, other.elements)} noexcept;
            })
            requires (requires{
                {std::ranges::swap(__value, other.__value)};
                {std::ranges::swap(elements, other.elements)};
            })
        {
            std::ranges::swap(__value, other.__value);
            std::ranges::swap(elements, other.elements);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept {
            return (*std::forward<Self>(self).__value);
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{std::addressof(*__value)} noexcept;})
            requires (requires{{std::addressof(*__value)};})
        {
            return std::addressof(*__value);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{std::addressof(*__value)} noexcept;})
            requires (requires{{std::addressof(*__value)};})
        {
            return std::addressof(*__value);
        }

        template <size_t I, typename Self>
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{meta::get<I>(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::get<I>(*std::forward<Self>(self).__value)};})
        {
            return (meta::get<I>(*std::forward<Self>(self).__value));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, size_t i) noexcept {
            return (*std::forward<Self>(self).elements[i]);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self) noexcept {
            return (*std::forward<Self>(self).elements[0]);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) back(this Self&& self) noexcept {
            return (*std::forward<Self>(self).elements[size() - 1]);
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
            noexcept (requires{{impl::ref<C>{{std::forward<A>(args)...}}} noexcept;})
            requires (requires{{impl::ref<C>{{std::forward<A>(args)...}}};})
        :
            __value{{std::forward<A>(args)...}}
        {}

        constexpr void swap(tuple_range& other)
            noexcept (requires{{std::ranges::swap(__value, other.__value)} noexcept;})
            requires (requires{{std::ranges::swap(__value, other.__value)};})
        {
            std::ranges::swap(__value, other.__value);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept {
            return (*std::forward<Self>(self).__value);
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

        template <size_t I, typename Self>
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{meta::get<I>(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::get<I>(*std::forward<Self>(self).__value)};})
        {
            return (meta::get<I>(*std::forward<Self>(self).__value));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, size_t i)
            noexcept (requires{{dispatch<Self>{i}(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{dispatch<Self>{i}(*std::forward<Self>(self).__value)};})
        {
            return (dispatch<Self>{i}(*std::forward<Self>(self).__value));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self)
            noexcept (requires{{dispatch<Self>{0}(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{dispatch<Self>{0}(*std::forward<Self>(self).__value)};})
        {
            return (dispatch<Self>{0}(*std::forward<Self>(self).__value));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) back(this Self&& self)
            noexcept (requires{
                {dispatch<Self>{size() - 1}(*std::forward<Self>(self).__value)} noexcept;
            })
            requires (requires{{dispatch<Self>{size() - 1}(*std::forward<Self>(self).__value)};})
        {
            return (dispatch<Self>{size() - 1}(*std::forward<Self>(self).__value));
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
    available and integer-like.  Otherwise, if `start` or `stop` has an integer
    difference with respect to itself, then we use that type.  Lastly, we default to
    `std::ptrdiff_t` as a fallback. */
    template <typename Start, typename Stop>
    struct _iota_difference { using type = std::ptrdiff_t; };
    template <typename Start, typename Stop>
        requires (requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {stop - start} -> meta::signed_integer;
        })
    struct _iota_difference<Start, Stop> {
        using type = meta::unqualify<decltype(
            std::declval<meta::as_const_ref<Stop>>() -
            std::declval<meta::as_const_ref<Start>>()
        )>;
    };
    template <typename Start, meta::None Stop>
        requires (!requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {stop - start};
        } && requires(meta::as_const_ref<Start> start) {
            {start - start} -> meta::signed_integer;
        })
    struct _iota_difference<Start, Stop> {
        using type = meta::unqualify<decltype(
            std::declval<meta::as_const_ref<Start>>() -
            std::declval<meta::as_const_ref<Start>>()
        )>;
    };
    template <typename Start, meta::None Stop>
        requires (!requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {stop - start};
            {start - start} -> meta::signed_integer;
        } && requires(meta::as_const_ref<Stop> stop) {
            {stop - stop} -> meta::signed_integer;
        })
    struct _iota_difference<Start, Stop> {
        using type = meta::unqualify<decltype(
            std::declval<meta::as_const_ref<Stop>>() -
            std::declval<meta::as_const_ref<Stop>>()
        )>;
    };
    template <typename Start, typename Stop>
    using iota_difference = _iota_difference<Start, Stop>::type;

    template <typename T>
    concept iota_empty = meta::is<T, iota_tag>;

    template <typename Stop>
    concept iota_infinite = iota_empty<Stop>;

    template <typename Start, typename Stop, typename Step>
    concept iota_bounded =
        !iota_infinite<Stop> &&
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {start < stop} -> meta::convertible_to<bool>;
        } && (strictly_positive<Step> || requires(
            meta::as_const_ref<Start> start,
            meta::as_const_ref<Stop> stop
        ) {
            {start > stop} -> meta::convertible_to<bool>;
        });

    template <typename Start, typename Stop, typename Step>
    concept iota_conditional =
        !iota_infinite<Stop> &&
        !iota_bounded<Start, Stop, Step> &&
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {stop(start)} -> meta::explicitly_convertible_to<bool>;
        };

    template <typename Start, typename Step>
    concept iota_simple =
        iota_empty<Step> &&
        requires(meta::unqualify<Start>& start) {
            {++start};
        };

    template <typename Start, typename Step>
    concept iota_linear =
        !iota_simple<Start, Step> &&
        requires(meta::unqualify<Start>& start, meta::as_const_ref<Step> step) {
            {start += step};
        };

    template <typename Start, typename Step>
    concept iota_nonlinear =
        !iota_simple<Start, Step> &&
        !iota_linear<Start, Step> &&
        requires(meta::unqualify<Start>& start, meta::as_const_ref<Step> step) {
            {step(start)} -> meta::is_void;
        };

    constexpr AssertionError zero_step_error() noexcept {
        return AssertionError("step size cannot be zero");
    }

    /* A simple, half-open range from `[start, stop)` that increments by `step` on each
    iteration.

    The `start`, `stop`, and `step` indices must satisfy the following criteria:

        1.  `start` must be copyable, while `stop` and `step` must either be copyable
            or lvalue references (whose lifetimes will not be extended).
        2.  `stop` must be either empty (indicating an infinite range), or satisfy
            one of the following (in order of preference):
                a.  `start < stop`.  If `step` is not empty and `step < 0` is
                    well-formed, then `start > stop` must also be valid.
                b.  `stop(start) -> bool`, where `false` terminates the range.
        3.  If `step` is empty, then `++start` must be valid.  Otherwise, one of
            the following must be well-formed (in order of preference):
                a.  `start += step`.  If `step == 0` is well-formed and evaluates to
                    true during the constructor, then a debug assertion will be thrown.
                b.  `step(start) -> void`, which modifies `start` in-place.

    Ranges of this form expose `size()` and `ssize()` methods as long as
    `(stop - start) / step` is a valid expression whose result can be explicitly
    converted to `difference_type`, and will also support indexing via the subscript
    operator if possible.  The range always models `std::borrowed_range`, and if
    `start` is decrementable, then the iterators will model
    `std::bidirectional_iterator`.  If `start` supports random-access addition and
    subtraction with the step size, then the iterators will model
    `std::random_access_iterator` as well.  Since the `end()` iterator is an empty
    sentinel, the range will never model `std::common_range` (but the sentinel may
    model `std::sized_sentinel_for<Begin>` if `ssize()` is available).

    The indices are meant to reflect typical loop syntax in a variety of languages,
    and can effectively replace any C-style `for` or `while` loop with zero overhead.
    The begin iterator over the range effectively equates to an explicit copy of the
    `start` index, while the end iterator is a sentinel that triggers the `stop`
    condition.  Incrementing the iterator updates the copy in-place according to
    `step`.

    The only possible regressions over a hand-rolled loop occur when:

        1.  The iota supports subscripting, which equates to a `start + n * step`
            expression.  In that case, the dereference operator must convert the result
            to a common type in order to satisfy `std::random_access_iterator`.  This
            may cause an extra copy on each dereference, which is usually not a problem
            for simple arithmetic types, but could be for more complex user-defined
            types.  Breaking the random-access constraint (such as by using an unsized
            `stop` condition or non-empty `step`) avoids this issue.
        2.  `step` is signed and `stop` is an absolute bound, in which case an extra
            branch must be emitted to confirm the direction of comparison.  In a future
            revision, this branch could be elided by either determining the signedness
            of `step` at compile-time if it is a constant, or by JIT compiling the
            comparison function during construction.  For now, the only way to avoid
            this is to replace the bound with a conditional `stop` function.
    */
    template <meta::not_rvalue Start, meta::not_rvalue Stop, meta::not_rvalue Step>
        requires (
            meta::copyable<Start> &&
            (meta::lvalue<Stop> || meta::copyable<Stop>) &&
            (meta::lvalue<Step> || meta::copyable<Step>) &&
            (
                iota_infinite<Stop> ||
                iota_bounded<Start, Stop, Step> ||
                iota_conditional<Start, Stop, Step>
            ) && (
                iota_simple<Start, Step> ||
                iota_linear<Start, Step> ||
                iota_nonlinear<Start, Step>
            )
        )
    struct iota : iota_tag {
        using start_type = Start;
        using stop_type = Stop;
        using step_type = Step;
        using difference_type = iota_difference<Start, Stop>;
        using size_type = meta::as_unsigned<difference_type>;
        using value_type = meta::unqualify<Start>;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::address_type<reference>;
        using iterator_category = std::conditional_t<
            (iota_empty<Stop> && requires(
                meta::as_const_ref<Start> start,
                meta::as_const_ref<Stop> stop
            ) {
                {difference_type(start - stop)};
            }) || (!iota_empty<Stop> && requires(
                meta::as_const_ref<Start> start,
                meta::as_const_ref<Stop> stop,
                meta::as_const_ref<Step> step
            ) {
                {difference_type(math::div::ceil<
                    meta::unqualify<decltype(stop - start)>,
                    meta::unqualify<Step>
                >{}(stop - start, step))};
            }),
            std::conditional_t<
                (iota_empty<Step> && requires(meta::unqualify<Start>& start) {
                    {--start};
                }) || (!iota_empty<Step> && requires(
                    meta::unqualify<Start>& start,
                    meta::as_const_ref<Step> step
                ) {
                    {start -= step};
                }),
                std::conditional_t<
                    (iota_empty<Step> && requires(
                        meta::unqualify<Start>& start,
                        difference_type i
                    ) {
                        {start + i} -> meta::has_common_type<meta::as_lvalue<Start>>;
                        {start += i};
                        {start -= i};
                    }) || (!iota_empty<Step> && requires(
                        meta::unqualify<Start>& start,
                        meta::as_const_ref<Step> step,
                        difference_type i
                    ) {
                        {start + i * step} -> meta::has_common_type<meta::as_lvalue<Start>>;
                        {start += i * step};
                        {start -= i * step};
                    }),
                    std::random_access_iterator_tag,
                    std::bidirectional_iterator_tag
                >,
                std::forward_iterator_tag
            >,
            std::input_iterator_tag
        >;

    private:
        using copy = iota<meta::unqualify<Start>, Stop, Step>;

        [[no_unique_address]] impl::ref<Start> m_start {};
        [[no_unique_address]] impl::ref<Stop> m_stop {};
        [[no_unique_address]] impl::ref<Step> m_step {};

    public:
        [[nodiscard]] constexpr iota() = default;
        [[nodiscard]] constexpr iota(
            meta::forward<Start> start,
            meta::forward<Stop> stop,
            meta::forward<Step> step = {}
        )
            noexcept (requires{
                {impl::ref<Start>{std::forward<Start>(start)}} noexcept;
                {impl::ref<Stop>{std::forward<Stop>(stop)}} noexcept;
                {impl::ref<Step>{std::forward<Step>(step)}} noexcept;
            } && (
                !DEBUG ||
                !iota_linear<Start, Step> ||
                !requires{{*m_step == 0} -> meta::explicitly_convertible_to<bool>;}
            ))
            requires (requires{
                {impl::ref<Start>{std::forward<Start>(start)}};
                {impl::ref<Stop>{std::forward<Stop>(stop)}};
                {impl::ref<Step>{std::forward<Step>(step)}};
            })
        :
            m_start{std::forward<Start>(start)},
            m_stop{std::forward<Stop>(stop)},
            m_step{std::forward<Step>(step)}
        {
            if constexpr (
                DEBUG &&
                iota_linear<Start, Step> &&
                requires{{*m_step == 0} -> meta::explicitly_convertible_to<bool>;}
            ) {
                if (*m_step == 0) {
                    throw zero_step_error();
                }
            }
        }
        [[nodiscard]] constexpr iota(
            const meta::unqualify<Start>& start,
            const impl::ref<Stop>& stop,
            const impl::ref<Step>& step
        )
            noexcept (requires{
                {impl::ref<Start>{start}} noexcept;
                {impl::ref<Stop>{stop}} noexcept;
                {impl::ref<Step>{step}} noexcept;
            })
            requires (requires{
                {impl::ref<Start>{start}};
                {impl::ref<Stop>{stop}};
                {impl::ref<Step>{step}};
            })
        :
            m_start{start},
            m_stop{stop},
            m_step{step}
        {}
        [[nodiscard]] constexpr iota(
            meta::unqualify<Start>&& start,
            impl::ref<Stop>&& stop,
            impl::ref<Step>&& step
        )
            noexcept (requires{
                {impl::ref<Start>{std::move(start)}} noexcept;
                {impl::ref<Stop>{std::move(stop)}} noexcept;
                {impl::ref<Step>{std::move(step)}} noexcept;
            })
            requires (requires{
                {impl::ref<Start>{std::move(start)}};
                {impl::ref<Stop>{std::move(stop)}};
                {impl::ref<Step>{std::move(step)}};
            })
        :
            m_start{std::move(start)},
            m_stop{std::move(stop)},
            m_step{std::move(step)}
        {}

        constexpr void swap(iota& other)
            noexcept (requires{
                {std::ranges::swap(m_start, other.m_start)} noexcept;
                {std::ranges::swap(m_stop, other.m_stop)} noexcept;
                {std::ranges::swap(m_step, other.m_step)} noexcept;
            })
            requires (requires{
                {std::ranges::swap(m_start, other.m_start)};
                {std::ranges::swap(m_stop, other.m_stop)};
                {std::ranges::swap(m_step, other.m_step)};
            })
        {
            std::ranges::swap(m_start, other.m_start);
            std::ranges::swap(m_stop, other.m_stop);
            std::ranges::swap(m_step, other.m_step);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) start(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_start);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) stop(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_stop);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) step(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_step);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self) noexcept {
            return (std::forward<Self>(self).start());
        }

        [[nodiscard]] constexpr copy begin() const
            noexcept (requires{{copy{start(), m_stop, m_step}} noexcept;})
            requires (requires{{copy{start(), m_stop, m_step}};})
        {
            return copy{start(), m_stop, m_step};
        }

        [[nodiscard]] constexpr copy begin() &&
            noexcept (requires{
                {copy{std::move(start()), std::move(m_stop), std::move(m_step)}} noexcept;
            })
            requires (requires{
                {copy{std::move(start()), std::move(m_stop), std::move(m_step)}};
            })
        {
            return copy{std::move(start()), std::move(m_stop), std::move(m_step)};
        }

        [[nodiscard]] static constexpr NoneType end() noexcept {
            return {};
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator[](this Self&& self, difference_type i)
            noexcept (requires{{
                std::forward<Self>(self).start() + i
            } noexcept -> meta::nothrow::convertible_to<meta::common_type<
                meta::remove_rvalue<decltype((std::forward<Self>(self).start()))>,
                meta::remove_rvalue<decltype(
                    (std::forward<Self>(self).start() + i)
                )>
            >>;})
            -> meta::common_type<
                meta::remove_rvalue<decltype((std::forward<Self>(self).start()))>,
                meta::remove_rvalue<decltype(
                    (std::forward<Self>(self).start() + i)
                )>
            >
            requires (iota_empty<Step> && requires{
                {self.ssize()};
                {
                    std::forward<Self>(self).start() + i
                } -> meta::convertible_to<meta::common_type<
                    meta::remove_rvalue<decltype((std::forward<Self>(self).start()))>,
                    meta::remove_rvalue<decltype(
                        (std::forward<Self>(self).start() + i)
                    )>
                >>;
            })
        {
            return std::forward<Self>(self).start() + i;
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator[](this Self&& self, difference_type i)
            noexcept (requires{{
                std::forward<Self>(self).start() + i * std::forward<Self>(self).step()
            } noexcept -> meta::nothrow::convertible_to<meta::common_type<
                meta::remove_rvalue<decltype((std::forward<Self>(self).start()))>,
                meta::remove_rvalue<decltype(
                    (std::forward<Self>(self).start() + i * std::forward<Self>(self).step())
                )>
            >>;})
            -> meta::common_type<
                meta::remove_rvalue<decltype((std::forward<Self>(self).start()))>,
                meta::remove_rvalue<decltype(
                    (std::forward<Self>(self).start() + i * std::forward<Self>(self).step())
                )>
            >
            requires (!iota_empty<Step> && requires{
                {self.ssize()};
                {
                    std::forward<Self>(self).start() + i * std::forward<Self>(self).step()
                } -> meta::convertible_to<meta::common_type<
                    meta::remove_rvalue<decltype((std::forward<Self>(self).start()))>,
                    meta::remove_rvalue<decltype(
                        (std::forward<Self>(self).start() + i * std::forward<Self>(self).step())
                    )>
                >>;
            })
        {
            return std::forward<Self>(self).start() + i * std::forward<Self>(self).step();
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator*(this Self&& self)
            noexcept (requires(difference_type i) {{
                std::forward<Self>(self)[i]
            } noexcept -> meta::nothrow::convertible_to<
                meta::remove_rvalue<decltype((std::forward<Self>(self)[i]))>
            >;})
            -> meta::remove_rvalue<decltype(
                (std::forward<Self>(self)[std::declval<difference_type>()])
            )>
            requires (requires(difference_type i) {
                {std::forward<Self>(self)[i]};
                {std::forward<Self>(self).start()} -> meta::convertible_to<
                    meta::remove_rvalue<decltype((std::forward<Self>(self)[i]))>
                >;
            })
        {
            return std::forward<Self>(self).start();
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept
            requires (!requires(difference_type i) {{std::forward<Self>(self)[i]};})
        {
            return (std::forward<Self>(self).start());
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{meta::to_arrow(start())} noexcept;})
            requires (requires{{meta::to_arrow(start())};})
        {
            return meta::to_arrow(start());
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{meta::to_arrow(start())} noexcept;})
            requires (requires{{meta::to_arrow(start())};})
        {
            return meta::to_arrow(start());
        }

        [[nodiscard]] static constexpr bool empty() noexcept requires (iota_infinite<Stop>) {
            return false;
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {start() < stop()} noexcept -> meta::nothrow::convertible_to<bool>;
            } && (strictly_positive<Step> || requires{
                {step() < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {start() > stop()} noexcept -> meta::nothrow::convertible_to<bool>;
            }))
            requires (iota_bounded<Start, Stop, Step>)
        {
            if constexpr (strictly_positive<Step>) {
                return !bool(start() < stop());
            } else {
                if (step() < 0) {
                    return !bool(start() > stop());
                } else {
                    return !bool(start() < stop());
                }
            }
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {stop()(start())} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (iota_conditional<Start, Stop, Step>)
        {
            return !bool(stop()(start()));
        }

        [[nodiscard]] explicit constexpr operator bool() const
            noexcept (requires{{empty()} noexcept;})
            requires (requires{{empty()};})
        {
            return !empty();
        }

        [[nodiscard]] friend constexpr bool operator<(const iota& self, NoneType)
            noexcept (requires{{!self.empty()} noexcept;})
            requires (requires{{!self.empty()};})
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
            requires (requires{{self.empty()};})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator==(const iota& self, NoneType)
            noexcept (requires{{self.empty()} noexcept;})
            requires (requires{{self.empty()};})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator==(NoneType, const iota& self)
            noexcept (requires{{self.empty()} noexcept;})
            requires (requires{{self.empty()};})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator!=(const iota& self, NoneType)
            noexcept (requires{{!self.empty()} noexcept;})
            requires (requires{{!self.empty()};})
        {
            return !self.empty();
        }

        [[nodiscard]] friend constexpr bool operator!=(NoneType, const iota& self)
            noexcept (requires{{!self.empty()} noexcept;})
            requires (requires{{!self.empty()};})
        {
            return !self.empty();
        }

        [[nodiscard]] friend constexpr bool operator>=(const iota& self, NoneType)
            noexcept (requires{{self.empty()} noexcept;})
            requires (requires{{self.empty()};})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator>=(NoneType, const iota& self) noexcept {
            return true;
        }

        [[nodiscard]] friend constexpr bool operator>(const iota& self, NoneType) noexcept {
            return false;
        }

        [[nodiscard]] friend constexpr bool operator>(NoneType, const iota& self)
            noexcept (requires{{!self.empty()} noexcept;})
            requires (requires{{!self.empty()};})
        {
            return !self.empty();
        }

        [[nodiscard]] constexpr difference_type ssize() const
            noexcept (requires{{difference_type(stop() - start())} noexcept;})
            requires (iota_empty<Step> && requires{{difference_type(stop() - start())};})
        {
            return difference_type(stop() - start());
        }

        [[nodiscard]] constexpr difference_type ssize() const
            noexcept (requires{{
                difference_type(math::div::ceil<
                    meta::unqualify<decltype(stop() - start())>,
                    meta::unqualify<Step>
                >{}(stop() - start(), step()))
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (!iota_empty<Step> && requires{{
                difference_type(math::div::ceil<
                    meta::unqualify<decltype(stop() - start())>,
                    meta::unqualify<Step>
                >{}(stop() - start(), step()))
            };})
        {
            return difference_type(math::div::ceil<
                meta::unqualify<decltype(stop() - start())>,
                meta::unqualify<Step>
            >{}(stop() - start(), step()));
        }

        [[nodiscard]] constexpr size_type size() const
            noexcept (requires{{size_type(ssize())} noexcept;})
            requires (requires{{size_type(ssize())};})
        {
            return size_type(ssize());
        }

        [[nodiscard]] constexpr bool operator==(const iota& other) const
            noexcept (requires{{
                (other.stop() - other.start()) == (stop() - start())
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                (other.stop() - other.start()) == (stop() - start())
            } -> meta::convertible_to<bool>;})
        {
            return (other.stop() - other.start()) == (stop() - start());
        }

        [[nodiscard]] constexpr auto operator<=>(const iota& other) const
            noexcept (requires{{(other.stop() - other.start()) <=> (stop() - start())} noexcept;})
            requires (requires{{(other.stop() - other.start()) <=> (stop() - start())};})
        {
            return (other.stop() - other.start()) <=> (stop() - start());
        }

        [[nodiscard]] constexpr difference_type operator-(const iota& other) const
            noexcept (requires{{
                (other.stop() - other.start()) - (stop() - start())
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (iota_empty<Step> && requires{{
                (other.stop() - other.start()) - (stop() - start())
            } -> meta::convertible_to<difference_type>;})
        {
            return (other.stop() - other.start()) - (stop() - start());
        }

        [[nodiscard]] constexpr difference_type operator-(const iota& other) const
            noexcept (requires{{
                difference_type(math::div::ceil<
                    meta::unqualify<decltype(stop() - start())>,
                    meta::unqualify<Step>
                >{}((other.stop() - other.start()) - (stop() - start()), step()))
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (!iota_empty<Step> && requires{{
                difference_type(math::div::ceil<
                    meta::unqualify<decltype(stop() - start())>,
                    meta::unqualify<Step>
                >{}((other.stop() - other.start()) - (stop() - start()), step()))
            };})
        {
            return difference_type(math::div::ceil<
                meta::unqualify<decltype(stop() - start())>,
                meta::unqualify<Step>
            >{}((other.stop() - other.start()) - (stop() - start()), step()));
        }

        [[nodiscard]] friend constexpr difference_type operator-(const iota& self, NoneType)
            noexcept (requires{
                {-self.ssize()} noexcept -> meta::nothrow::convertible_to<difference_type>;
            })
            requires (requires{{-self.ssize()} -> meta::convertible_to<difference_type>;})
        {
            return -self.ssize();
        }

        [[nodiscard]] friend constexpr difference_type operator-(NoneType, const iota& self)
            noexcept (requires{{self.ssize()} noexcept;})
            requires (requires{{self.ssize()};})
        {
            return self.ssize();
        }

        constexpr void increment()
            noexcept (requires{{++start()} noexcept;})
            requires (iota_simple<Start, Step>)
        {
            ++start();
        }

        constexpr void increment()
            noexcept (requires{{start() += step()} noexcept;})
            requires (iota_linear<Start, Step>)
        {
            start() += step();
        }

        constexpr void increment()
            noexcept (requires{{step()(start())} noexcept;})
            requires (iota_nonlinear<Start, Step>)
        {
            step()(start());
        }

        constexpr iota& operator++()
            noexcept (requires{{increment()} noexcept;})
            requires (requires{{increment()};})
        {
            increment();
            return *this;
        }

        [[nodiscard]] constexpr iota operator++(int)
            noexcept (requires(copy tmp) {
                {begin()} noexcept;
                {tmp.increment()} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {tmp.increment()};
            })
        {
            copy tmp = begin();
            tmp.increment();
            return tmp;
        }

        constexpr void increment(difference_type n)
            noexcept (requires{{start() += n} noexcept;})
            requires (iota_empty<Step> && requires{{start() += n};})
        {
            start() += n;
        }

        constexpr void increment(difference_type n)
            noexcept (requires{{start() += n * step()} noexcept;})
            requires (!iota_empty<Step> && requires{{start() += n * step()};})
        {
            start() += n * step();
        }

        constexpr iota& operator+=(difference_type n)
            noexcept (requires{{increment(n)} noexcept;})
            requires (requires{{increment(n)};})
        {
            increment(n);
            return *this;
        }

        [[nodiscard]] friend constexpr copy operator+(const iota& self, difference_type n)
            noexcept (requires(copy tmp) {
                {self.begin()} noexcept;
                {tmp.increment(n)} noexcept;
            })
            requires (requires(copy tmp) {
                {self.begin()};
                {tmp.increment(n)};
            })
        {
            copy tmp = self.begin();
            tmp.increment(n);
            return tmp;
        }

        [[nodiscard]] friend constexpr copy operator+(difference_type n, const iota& self)
            noexcept (requires(copy tmp) {
                {self.begin()} noexcept;
                {tmp.increment(n)} noexcept;
            })
            requires (requires(copy tmp) {
                {self.begin()};
                {tmp.increment(n)};
            })
        {
            copy tmp = self.begin();
            tmp.increment(n);
            return tmp;
        }

        constexpr void decrement()
            noexcept (requires{{--start()} noexcept;})
            requires (iota_empty<Step> && requires{{--start()};})
        {
            --start();
        }

        constexpr void decrement()
            noexcept (requires{{start() -= step()} noexcept;})
            requires (!iota_empty<Step> && requires{{start() -= step()};})
        {
            start() -= step();
        }

        constexpr iota& operator--()
            noexcept (requires{{decrement()} noexcept;})
            requires (requires{{decrement()};})
        {
            decrement();
            return *this;
        }

        [[nodiscard]] constexpr iota operator--(int)
            noexcept (requires(copy tmp) {
                {begin()} noexcept;
                {tmp.decrement()} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {tmp.decrement()};
            })
        {
            copy tmp = begin();
            tmp.decrement();
            return tmp;
        }

        constexpr void decrement(difference_type n)
            noexcept (requires{{start() -= n} noexcept;})
            requires (iota_empty<Step> && requires{{start() -= n};})
        {
            start() -= n;
        }

        constexpr void decrement(difference_type n)
            noexcept (requires{{start() -= n * step()} noexcept;})
            requires (!iota_empty<Step> && requires{{start() -= n * step()};})
        {
            start() -= n * step();
        }

        constexpr iota& operator-=(difference_type n)
            noexcept (requires{{decrement(n)} noexcept;})
            requires (requires{{decrement(n)};})
        {
            decrement(n);
            return *this;
        }

        [[nodiscard]] constexpr copy operator-(difference_type n) const
            noexcept (requires(copy tmp) {
                {begin()} noexcept;
                {tmp.decrement(n)} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {tmp.decrement(n)};
            })
        {
            copy tmp = begin();
            tmp.decrement(n);
            return tmp;
        }
    };

    template <typename Start, typename Stop = iota_tag, typename Step = iota_tag>
    iota(Start&&, Stop&& stop, Step&& step = {}) -> iota<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >;

    template <typename Start, typename Stop>
    using subrange_difference = iota_difference<Start, Stop>;

    template <typename T>
    concept subrange_empty = meta::is<T, subrange_tag>;

    template <typename Stop>
    concept subrange_infinite = subrange_empty<Stop>;

    template <typename Start, typename Stop, typename Step>
    concept subrange_bounded =
        !subrange_infinite<Stop> &&
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {start < stop} -> meta::convertible_to<bool>;
        } && (strictly_positive<Step> || requires(
            meta::as_const_ref<Start> start,
            meta::as_const_ref<Stop> stop
        ) {
            {start > stop} -> meta::convertible_to<bool>;
        });

    template <typename Start, typename Stop, typename Step>
    concept subrange_equal =
        !subrange_infinite<Stop> &&
        !subrange_bounded<Start, Stop, Step> &&
        subrange_empty<Step> &&
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {start == stop} -> meta::convertible_to<bool>;
        };

    template <typename Start, typename Stop, typename Step>
    concept subrange_counted =
        !subrange_infinite<Stop> &&
        !subrange_bounded<Start, Stop, Step> &&
        !subrange_equal<Start, Stop, Step> &&
        meta::integer<Stop> &&
        requires(
            subrange_difference<Start, Stop> index,
            meta::as_const_ref<Stop> stop
        ) {
            {index >= subrange_difference<Start, Stop>(stop)} -> meta::convertible_to<bool>;
        };

    template <typename Start, typename Stop, typename Step>
    concept subrange_conditional =
        !subrange_infinite<Stop> &&
        !subrange_bounded<Start, Stop, Step> &&
        !subrange_equal<Start, Stop, Step> &&
        !subrange_counted<Start, Stop, Step> &&
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {stop(start)} -> meta::explicitly_convertible_to<bool>;
        };

    template <typename Start, typename Step>
    concept subrange_simple =
        subrange_empty<Step> &&
        requires(meta::unqualify<Start>& start) {
            {++start};
        };

    /// TODO: subrange_linear can only be used if the stop condition supports distance
    /// checks?

    template <typename Start, typename Stop, typename Step>
    concept subrange_linear =
        !subrange_simple<Start, Step> &&
        meta::convertible_to<Step, subrange_difference<Start, Stop>> &&
        requires(meta::unqualify<Start>& start, subrange_difference<Start, Stop> step) {
            {start += step};
        };

    template <typename Start, typename Stop, typename Step>
    concept subrange_loop =
        !subrange_simple<Start, Step> &&
        !subrange_linear<Start, Stop, Step> &&
        meta::convertible_to<Step, subrange_difference<Start, Stop>> &&
        meta::default_constructible<subrange_difference<Start, Stop>> &&
        requires(
            meta::unqualify<Start>& start,
            subrange_difference<Start, Stop> step,
            subrange_difference<Start, Stop> i
        ) {
            {i < step};
            {++i};
            {++start};
        } && (strictly_positive<Step> || requires(
            meta::unqualify<Start>& start,
            subrange_difference<Start, Stop> step,
            subrange_difference<Start, Stop> i
        ) {
            {i > step};
            {--i};
            {--start};
        });

    template <typename Start, typename Stop, typename Step>
    concept subrange_nonlinear =
        !subrange_simple<Start, Step> &&
        !subrange_linear<Start, Stop, Step> &&
        !subrange_loop<Start, Stop, Step> &&
        requires(meta::unqualify<Start>& start, meta::as_const_ref<Step> step) {
            {step(start)} -> meta::is_void;
        };

    constexpr AssertionError negative_count_error() noexcept {
        return AssertionError("count cannot be negative");
    }

    enum class subrange_check {
        NEVER,
        CONSTEVAL,
        ALWAYS
    };

    template <typename Start, typename Stop, typename Step>
    concept subrange_concept =
        meta::not_rvalue<Start> &&
        meta::not_rvalue<Stop> &&
        meta::not_rvalue<Step> &&
        meta::iterator<Start> &&
        meta::copyable<Start> &&
        (meta::lvalue<Stop> || meta::copyable<Stop>) &&
        (meta::lvalue<Step> || meta::copyable<Step>) &&
        (
            subrange_infinite<Stop> ||
            subrange_bounded<Start, Stop, Step> ||
            subrange_equal<Start, Stop, Step> ||
            subrange_counted<Start, Stop, Step> ||
            subrange_conditional<Start, Stop, Step>
        ) && (
            subrange_simple<Start, Step> ||
            subrange_linear<Start, Stop, Step> ||
            subrange_loop<Start, Stop, Step> ||
            subrange_nonlinear<Start, Stop, Step>
        );

    /* A simple subrange that yields successive values in the interval `[start, stop)`,
    incrementing by `step` on each iteration.

    This class behaves similarly to `iota` in most respects, but differs in the
    following:

        1.  `start` must be an iterator type, rather than a value type.
        2.  `stop` can be given as a positive integer count rather than an absolute
            bound or conditional function, which converts the subrange into a counted
            range.  `start == stop` may also be used instead of `start < stop` to
            detect the end of the range, since iterators are always incremented in
            discrete steps, and equality comparisons are therefore reliable.
        3.  The `step` size must be empty, a function object taking the range as an
            argument, or convertible to the subrange's difference type (a signed
            integer).  If `start += step` is not a valid expression, then `++start`
            and/or `--start` may be called in a loop depending on the sign of `step`.
        4.  Subrange iterators are always totally-ordered with respect to each other
            and the `end()` sentinel, even if the underlying iterator is not.
        5.  Extra bounds-checking may be performed to ensure that the `start` iterator
            does not exceed the `stop` bound during constant evaluation, and remains
            captured within the interval.  Additional tracking indices are used to
            ensure this, which may add a small amount of overhead to iteration.  The
            bounds checking may be elided in non-constant-evaluation contexts if
            comparisons against `stop` are either infinite, ordered, or counted.

    Ranges of this form expose `size()` and `ssize()` methods as long as
    `(stop - start) / step` is a valid expression, and will also support indexing via
    the subscript operator if the underlying iterator supports it.  The range always
    models `std::borrowed_range`, and if the underlying iterator is also bidirectional,
    then the range will model `std::bidirectional_range` as well.  If the iterator is
    random-access, then the range will model `std::random_access_range`, and if
    it is contiguous and no step size is given, then the range will model
    `std::contiguous_range` and provide a `data()` method as well.  Since the `end()`
    iterator is an empty sentinel, the range will never model `std::common_range`
    (but the sentinel may model `std::sized_sentinel_for<Begin>` if `ssize()` is
    available). */
    template <typename Start, typename Stop = subrange_tag, typename Step = subrange_tag>
        requires (subrange_concept<Start, Stop, Step>)
    struct subrange : subrange_tag {
        using difference_type = subrange_difference<Start, Stop>;
        using size_type = meta::as_unsigned<difference_type>;
        using value_type = meta::iterator_value<Start>;
        using reference = meta::iterator_reference<Start>;
        using pointer = meta::iterator_pointer<Start>;
        using iterator_category = std::conditional_t<
            !subrange_nonlinear<Start, Stop, Step> && meta::bidirectional_iterator<Start>,
            std::conditional_t<
                meta::random_access_iterator<Start>,
                std::conditional_t<
                    meta::contiguous_iterator<Start> && subrange_empty<Step> && (
                        subrange_infinite<Stop> ||
                        (subrange_counted<Start, Stop, Step> && requires(
                            meta::as_const_ref<Stop> stop,
                            difference_type index
                        ) {{
                            difference_type(stop) - index
                        } -> meta::convertible_to<difference_type>;}) ||
                        (!subrange_counted<Start, Stop, Step> && requires(
                            meta::as_const_ref<Stop> stop,
                            meta::as_const_ref<Start> start
                        ) {{stop - start} -> meta::convertible_to<difference_type>;})
                    ),
                    std::contiguous_iterator_tag,
                    std::random_access_iterator_tag
                >,
                std::bidirectional_iterator_tag
            >,
            std::forward_iterator_tag
        >;
        using start_type = Start;
        using stop_type = Stop;
        using step_type = std::conditional_t<
            subrange_linear<Start, Stop, Step> || subrange_loop<Start, Stop, Step>,
            difference_type,
            Step
        >;

    private:
        using copy = subrange<meta::unqualify<start_type>, stop_type, step_type>;

        static constexpr subrange_check check =
            (subrange_infinite<Stop> || subrange_simple<Start, Step>) ?
                subrange_check::NEVER :
                (subrange_bounded<Start, Stop, Step> || subrange_counted<Start, Stop, Step>) &&
                (subrange_linear<Start, Stop, Step> || subrange_loop<Start, Stop, Step>) ?
                    subrange_check::CONSTEVAL :
                    subrange_check::ALWAYS;

        using overflow_type = std::conditional_t<
            check == subrange_check::NEVER,
            subrange_tag,
            difference_type
        >;

        [[no_unique_address]] impl::ref<start_type> m_start {};
        [[no_unique_address]] impl::ref<stop_type> m_stop {};
        [[no_unique_address]] impl::ref<step_type> m_step {};
        [[no_unique_address]] difference_type m_index {};
        [[no_unique_address]] overflow_type m_overflow {};

    public:
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) start(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_start);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) stop(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_stop);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) step(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_step);
        }

        [[nodiscard]] constexpr difference_type& index() noexcept
            requires (check == subrange_check::NEVER)
        {
            return m_index;
        }

        [[nodiscard]] constexpr const difference_type& index() const noexcept
            requires (check == subrange_check::NEVER)
        {
            return m_index;
        }

        [[nodiscard]] constexpr difference_type index() const noexcept
            requires (check != subrange_check::NEVER)
        {
            if constexpr (check == subrange_check::ALWAYS) {
                return m_index + m_overflow;
            } else {
                if consteval {
                    return m_index + m_overflow;
                } else {
                    return m_index;
                }
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).start()} noexcept;})
            requires (requires{{*std::forward<Self>(self).start()};})
        {
            return (*std::forward<Self>(self).start());
        }

        [[nodiscard]] constexpr auto data()
            noexcept (requires{{std::addressof(front())} noexcept;})
            requires (meta::is<iterator_category, std::contiguous_iterator_tag> && requires{
                {std::addressof(front())};
            })
        {
            return std::addressof(front());
        }

        [[nodiscard]] constexpr auto data() const
            noexcept (requires{{std::addressof(front())} noexcept;})
            requires (meta::is<iterator_category, std::contiguous_iterator_tag> && requires{
                {std::addressof(front())};
            })
        {
            return std::addressof(front());
        }

        [[nodiscard]] constexpr subrange() = default;
        [[nodiscard]] constexpr subrange(
            meta::forward<Start> start,
            meta::forward<Stop> stop,
            meta::forward<Step> step = {}
        )
            noexcept (requires{
                {impl::ref<start_type>{std::forward<Start>(start)}} noexcept;
                {impl::ref<stop_type>{std::forward<Stop>(stop)}} noexcept;
                {impl::ref<step_type>{std::forward<Step>(step)}} noexcept;
            } && (
                !DEBUG ||
                (!subrange_linear<Start, Stop, Step> && !subrange_loop<Start, Stop, Step>) ||
                !requires{{this->step() == 0} -> meta::explicitly_convertible_to<bool>;}
            ) && (
                !DEBUG ||
                !subrange_counted<Start, Stop, Step> ||
                !requires{{this->stop() < 0} -> meta::explicitly_convertible_to<bool>;}
            ))
            requires (requires{
                {impl::ref<start_type>{std::forward<Start>(start)}};
                {impl::ref<stop_type>{std::forward<Stop>(stop)}};
                {impl::ref<step_type>{std::forward<Step>(step)}};
            })
        :
            m_start{std::forward<Start>(start)},
            m_stop{std::forward<Stop>(stop)},
            m_step{std::forward<Step>(step)}
        {
            if constexpr (DEBUG) {
                if constexpr (
                    (subrange_linear<Start, Stop, Step> || subrange_loop<Start, Stop, Step>) &&
                    requires{{this->step() == 0} -> meta::explicitly_convertible_to<bool>;}
                ) {
                    if (this->step() == 0) {
                        throw zero_step_error();
                    }
                }
                if constexpr (
                    subrange_counted<Start, Stop, Step> &&
                    requires{{this->stop() < 0} -> meta::explicitly_convertible_to<bool>;}
                ) {
                    if (this->stop() < 0) {
                        throw negative_count_error();
                    }
                }
            }
        }
        [[nodiscard]] constexpr subrange(
            const meta::unqualify<start_type>& start,
            const impl::ref<stop_type>& stop,
            const impl::ref<step_type>& step,
            const difference_type& index,
            const overflow_type& overflow
        )
            noexcept (requires{
                {impl::ref<start_type>{start}} noexcept;
                {impl::ref<stop_type>{stop}} noexcept;
                {impl::ref<step_type>{step}} noexcept;
                {difference_type(index)} noexcept;
                {overflow_type(overflow)} noexcept;
            })
            requires (requires{
                {impl::ref<start_type>{start}};
                {impl::ref<stop_type>{stop}};
                {impl::ref<step_type>{step}};
                {difference_type(index)};
                {overflow_type(overflow)};
            })
        :
            m_start{start},
            m_stop{stop},
            m_step{step},
            m_index(index),
            m_overflow(overflow)
        {}
        [[nodiscard]] constexpr subrange(
            meta::unqualify<start_type>&& start,
            impl::ref<stop_type>&& stop,
            impl::ref<step_type>&& step,
            difference_type&& index,
            overflow_type&& overflow
        )
            noexcept (requires{
                {impl::ref<start_type>{std::move(start)}} noexcept;
                {impl::ref<stop_type>{std::move(stop)}} noexcept;
                {impl::ref<step_type>{std::move(step)}} noexcept;
                {difference_type(std::move(index))} noexcept;
                {overflow_type(std::move(overflow))} noexcept;
            })
            requires (requires{
                {impl::ref<start_type>{std::move(start)}};
                {impl::ref<stop_type>{std::move(stop)}};
                {impl::ref<step_type>{std::move(step)}};
                {difference_type(std::move(index))};
                {overflow_type(std::move(overflow))};
            })
        :
            m_start{std::move(start)},
            m_stop{std::move(stop)},
            m_step{std::move(step)},
            m_index(std::move(index)),
            m_overflow(std::move(overflow))
        {}

        constexpr void swap(subrange& other)
            noexcept (requires{
                {std::ranges::swap(m_start, other.m_start)} noexcept;
                {std::ranges::swap(m_stop, other.m_stop)} noexcept;
                {std::ranges::swap(m_step, other.m_step)} noexcept;
                {std::ranges::swap(m_index, other.m_index)} noexcept;
                {std::ranges::swap(m_overflow, other.m_overflow)} noexcept;
            })
            requires (requires{
                {std::ranges::swap(m_start, other.m_start)};
                {std::ranges::swap(m_stop, other.m_stop)};
                {std::ranges::swap(m_step, other.m_step)};
                {std::ranges::swap(m_index, other.m_index)};
                {std::ranges::swap(m_overflow, other.m_overflow)};
            })
        {
            std::ranges::swap(m_start, other.m_start);
            std::ranges::swap(m_stop, other.m_stop);
            std::ranges::swap(m_step, other.m_step);
            std::ranges::swap(m_index, other.m_index);
            std::ranges::swap(m_overflow, other.m_overflow);
        }

        [[nodiscard]] constexpr copy begin() const
            noexcept (requires{{copy{start(), m_stop, m_step, m_index, m_overflow}} noexcept;})
            requires (requires{{copy{start(), m_stop, m_step, m_index, m_overflow}};})
        {
            return copy{start(), m_stop, m_step, m_index, m_overflow};
        }

        [[nodiscard]] constexpr copy begin() &&
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

        [[nodiscard]] static constexpr NoneType end() noexcept {
            return {};
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, difference_type n)
            noexcept (requires{{std::forward<Self>(self).start()[n]} noexcept;})
            requires (subrange_empty<Step> && requires{{std::forward<Self>(self).start()[n]};})
        {
            return (std::forward<Self>(self).start()[n]);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, difference_type n)
            noexcept (requires{
                {std::forward<Self>(self).start()[n + std::forward<Self>(self).step()]} noexcept;
            })
            requires (!subrange_empty<Step> && requires{
                {std::forward<Self>(self).start()[n * std::forward<Self>(self).step()]};
            })
        {
            return (std::forward<Self>(self).start()[n * std::forward<Self>(self).step()]);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).front()} noexcept;})
            requires (requires{{std::forward<Self>(self).front()};})
        {
            return (std::forward<Self>(self).front());
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{impl::arrow{*std::forward<Self>(self)}} noexcept;})
            requires (requires{{impl::arrow{*std::forward<Self>(self)}};})
        {
            return impl::arrow{*std::forward<Self>(self)};
        }

        [[nodiscard]] static constexpr bool empty() noexcept requires (subrange_infinite<Stop>) {
            return false;
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {start() < stop()} noexcept -> meta::nothrow::convertible_to<bool>;
            } && (strictly_positive<Step> || requires{
                {step() < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {start() > stop()} noexcept -> meta::nothrow::convertible_to<bool>;
            }))
            requires (subrange_bounded<Start, Stop, Step>)
        {
            if constexpr (strictly_positive<Step>) {
                return !bool(start() < stop());
            } else {
                if (step() < 0) {
                    return !bool(start() > stop());
                } else {
                    return !bool(start() < stop());
                }
            }
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {start() == stop()} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (subrange_equal<Start, Stop, Step>)
        {
            return start() == stop();
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {index() >= difference_type(stop())} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (subrange_counted<Start, Stop, Step>)
        {
            return index() >= difference_type(stop());
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {stop()(start())} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (subrange_conditional<Start, Stop, Step>)
        {
            return !bool(stop()(start()));
        }

        [[nodiscard]] explicit constexpr operator bool() const
            noexcept (requires{{!empty()} noexcept;})
            requires (requires{{!empty()};})
        {
            return !empty();
        }

        [[nodiscard]] friend constexpr bool operator<(const subrange& self, NoneType)
            noexcept (requires{{!self.empty()} noexcept;})
            requires (requires{{!self.empty()};})
        {
            return !self.empty();
        }

        [[nodiscard]] friend constexpr bool operator<(NoneType, const subrange& self) noexcept {
            return false;
        }

        [[nodiscard]] friend constexpr bool operator<=(const subrange& self, NoneType) noexcept {
            return true;
        }

        [[nodiscard]] friend constexpr bool operator<=(NoneType, const subrange& self)
            noexcept (requires{{self.empty()} noexcept;})
            requires (requires{{self.empty()};})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator==(const subrange& self, NoneType)
            noexcept (requires{{self.empty()} noexcept;})
            requires (requires{{self.empty()};})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator==(NoneType, const subrange& self)
            noexcept (requires{{self.empty()} noexcept;})
            requires (requires{{self.empty()};})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator!=(const subrange& self, NoneType)
            noexcept (requires{{!self.empty()} noexcept;})
            requires (requires{{!self.empty()};})
        {
            return !self.empty();
        }

        [[nodiscard]] friend constexpr bool operator!=(NoneType, const subrange& self)
            noexcept (requires{{!self.empty()} noexcept;})
            requires (requires{{!self.empty()};})
        {
            return !self.empty();
        }

        [[nodiscard]] friend constexpr bool operator>=(const subrange& self, NoneType)
            noexcept (requires{{self.empty()} noexcept;})
            requires (requires{{self.empty()};})
        {
            return self.empty();
        }

        [[nodiscard]] friend constexpr bool operator>=(NoneType, const subrange& self) noexcept {
            return true;
        }

        [[nodiscard]] friend constexpr bool operator>(const subrange& self, NoneType) noexcept {
            return false;
        }

        [[nodiscard]] friend constexpr bool operator>(NoneType, const subrange& self)
            noexcept (requires{{!self.empty()} noexcept;})
            requires (requires{{!self.empty()};})
        {
            return !self.empty();
        }

        [[nodiscard]] constexpr bool operator==(const subrange& other) const
            noexcept (requires{
                {index() == other.index()} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {index() == other.index()} -> meta::convertible_to<bool>;
            })
        {
            return index() == other.index();
        }

        [[nodiscard]] constexpr auto operator<=>(const subrange& other) const
            noexcept (requires{{index() <=> other.index()} noexcept;})
            requires (requires{{index() <=> other.index()};})
        {
            return index() <=> other.index();
        }

        [[nodiscard]] constexpr difference_type operator-(const subrange& other) const
            noexcept (requires{
                {index() - other.index()} noexcept -> meta::nothrow::convertible_to<difference_type>;
            })
            requires (subrange_empty<Step> && requires{
                {index() - other.index()} -> meta::convertible_to<difference_type>;
            })
        {
            return index() - other.index();
        }

        [[nodiscard]] constexpr difference_type operator-(const subrange& other) const
            noexcept (requires{{
                (index() - other.index()) / step()
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (!subrange_empty<Step> && requires{{
                (index() - other.index()) / step()
            } -> meta::convertible_to<difference_type>;})
        {
            return (index() - other.index()) / step();
        }

    private:
        constexpr difference_type remaining() const
            noexcept (requires{{
                difference_type(stop()) - index()
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (subrange_counted<Start, Stop, Step> && requires{{
                difference_type(stop()) - index()
            } -> meta::convertible_to<difference_type>;})
        {
            return difference_type(stop()) - index();
        }

        constexpr difference_type remaining() const
            noexcept (requires{
                {stop() - start()} noexcept -> meta::nothrow::convertible_to<difference_type>;
            })
            requires (!subrange_counted<Start, Stop, Step> && requires{
                {stop() - start()} -> meta::convertible_to<difference_type>;
            })
        {
            return stop() - start();
        }

        constexpr void unsafe_increment()
            noexcept (requires{{++start()} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{++start()};})
        {
            ++start();
            ++m_index;
        }

        constexpr void safe_increment()
            noexcept (requires{
                {empty()} noexcept;
                {++start()} noexcept;
            })
            requires (check != subrange_check::NEVER && requires{
                {empty()};
                {++start()};
            })
        {
            if (empty() || m_overflow < 0) {
                ++m_overflow;
            } else {
                ++start();
                ++m_index;
            }
        }

        constexpr void unsafe_decrement()
            noexcept (requires{{--start()} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{--start()};})
        {
            --start();
            --m_index;
        }

        constexpr void safe_decrement()
            noexcept (requires{{--start()} noexcept;})
            requires (check != subrange_check::NEVER && requires{{--start()};})
        {
            if (m_index == 0 || m_overflow > 0) {
                --m_overflow;
            } else {
                --start();
                --m_index;
            }
        }

        constexpr void unsafe_increment_by(difference_type n)
            noexcept (requires{{start() += n} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{start() += n};})
        {
            start() += n;
            m_index += n;
        }

        constexpr void safe_increment_by(difference_type n)
            noexcept (requires(difference_type delta) {
                {start() -= m_index} noexcept;
                {start() += n} noexcept;
                {empty()} noexcept;
                {remaining()} noexcept;
            })
            requires (check != subrange_check::NEVER && requires{
                {start() -= m_index};
                {start() += n};
                {empty()};
                {remaining()};
            })
        {
            if (n < 0) {
                if (m_index == 0) {
                    m_overflow += n;
                    return;
                }
                if (m_overflow > 0) {
                    m_overflow += n;
                    if (m_overflow > 0) {
                        return;
                    }
                    n = m_overflow;
                    m_overflow = 0;
                }
                if (-n > m_index) {
                    start() -= m_index;
                    m_overflow += m_index + n;
                    m_index = 0;
                } else {
                    start() += n;
                    m_index += n;
                }
            } else {
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
                    m_overflow += n - delta;
                    m_index += delta;
                } else {
                    start() += n;
                    m_index += n;
                }
            }
        }

        constexpr void increment_by(difference_type n)
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_increment_by(n)} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_increment_by(n)} noexcept;}
            ))
            requires ((
                check == subrange_check::NEVER ||
                requires{{safe_increment_by(n)};}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_increment_by(n)};}
            ))
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_increment_by(n);
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_increment_by(n);
            } else {
                if consteval {
                    safe_increment_by(n);
                } else {
                    unsafe_increment_by(n);
                }
            }
        }

        constexpr void unsafe_decrement_by(difference_type n)
            noexcept (requires{{start() -= n} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{start() -= n};})
        {
            start() -= n;
            m_index -= n;
        }

        constexpr void safe_decrement_by(difference_type n)
            noexcept (requires(difference_type delta) {
                {empty()} noexcept;
                {remaining()} noexcept;
                {start() += delta} noexcept;
                {start() -= n} noexcept;
            })
            requires (check != subrange_check::NEVER && requires(difference_type delta) {
                {empty()};
                {remaining()};
                {start() += delta};
                {start() -= n};
            })
        {
            if (n < 0) {
                if (empty()) {
                    m_overflow -= n;
                    return;
                }
                if (m_overflow < 0) {
                    m_overflow -= n;
                    if (m_overflow < 0) {
                        return;
                    }
                    n = m_overflow;
                    m_overflow = 0;
                }
                difference_type delta = remaining();
                if (-n > delta) {
                    start() += delta;
                    m_overflow -= n + delta;
                    m_index += delta;
                } else {
                    start() -= n;
                    m_index -= n;
                }
            } else {
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
                    m_overflow += m_index - n;
                    m_index = 0;
                } else {
                    start() -= n;
                    m_index -= n;
                }
            }
        }

        constexpr void decrement_by(difference_type n)
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_decrement_by(n)} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_decrement_by(n)} noexcept;}
            ))
            requires ((
                check == subrange_check::NEVER ||
                requires{{safe_decrement_by(n)};}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_decrement_by(n)};}
            ))
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_decrement_by(n);
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_decrement_by(n);
            } else {
                if consteval {
                    safe_decrement_by(n);
                } else {
                    unsafe_decrement_by(n);
                }
            }
        }

        constexpr void unsafe_increment_for_positive(difference_type n)
            noexcept (requires{{++start()} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{++start()};})
        {
            for (difference_type i {}; i < n; ++i) ++start();
            m_index += n;
        }

        constexpr void safe_increment_for_positive(difference_type n)
            noexcept (requires{
                {empty()} noexcept;
                {++start()} noexcept;
            })
            requires (requires{
                {empty()};
                {++start()};
            })
        {
            for (difference_type i {}; i < n; ++i) {
                if (empty() || m_overflow < 0) {
                    ++m_overflow;
                } else {
                    ++m_index;
                    ++start();
                }
            }
        }

        constexpr void increment_for_positive(difference_type n)
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_increment_for_positive(n)} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_increment_for_positive(n)} noexcept;}
            ))
            requires ((
                check == subrange_check::NEVER ||
                requires{{safe_increment_for_positive(n)};}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_increment_for_positive(n)};}
            ))
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_increment_for_positive(n);
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_increment_for_positive(n);
            } else {
                if consteval {
                    safe_increment_for_positive(n);
                } else {
                    unsafe_increment_for_positive(n);
                }
            }
        }

        constexpr void unsafe_increment_for_negative(difference_type n)
            noexcept (requires{{--start()} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{--start()};})
        {
            for (difference_type i {}; i > n; --i) --start();
            m_index -= n;
        }

        constexpr void safe_increment_for_negative(difference_type n)
            noexcept (requires{
                {empty()} noexcept;
                {--start()} noexcept;
            })
            requires (check != subrange_check::NEVER && requires{
                {empty()};
                {--start()};
            })
        {
            for (difference_type i {}; i > n; --i) {
                if (empty() || m_overflow < 0) {
                    ++m_overflow;
                } else {
                    ++m_index;
                    --start();
                }
            }
        }

        constexpr void increment_for_negative(difference_type n)
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_increment_for_negative(n)} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_increment_for_negative(n)} noexcept;}
            ))
            requires ((
                check == subrange_check::NEVER ||
                requires{{safe_increment_for_negative(n)};}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_increment_for_negative(n)};}
            ))
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_increment_for_negative(n);
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_increment_for_negative(n);
            } else {
                if consteval {
                    safe_increment_for_negative(n);
                } else {
                    unsafe_increment_for_negative(n);
                }
            }
        }

        constexpr void unsafe_decrement_for_positive(difference_type n)
            noexcept (requires{{--start()} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{--start()};})
        {
            for (difference_type i {}; i < n; ++i) --start();
            m_index -= n;
        }

        constexpr void safe_decrement_for_positive(difference_type n)
            noexcept (requires{{--start()} noexcept;})
            requires (check != subrange_check::NEVER && requires{{--start()};})
        {
            for (difference_type i {}; i < n; ++i) {
                if (m_index == 0 || m_overflow > 0) {
                    --m_overflow;
                } else {
                    --m_index;
                    --start();
                }
            }
        }

        constexpr void decrement_for_positive(difference_type n)
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_decrement_for_positive(n)} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_decrement_for_positive(n)} noexcept;}
            ))
            requires ((
                check == subrange_check::NEVER ||
                requires{{safe_decrement_for_positive(n)};}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_decrement_for_positive(n)};}
            ))
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_decrement_for_positive(n);
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_decrement_for_positive(n);
            } else {
                if consteval {
                    safe_decrement_for_positive(n);
                } else {
                    unsafe_decrement_for_positive(n);
                }
            }
        }

        constexpr void unsafe_decrement_for_negative(difference_type n)
            noexcept (requires{{++start()} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{++start()};})
        {
            for (difference_type i {}; i > n; --i) ++start();
            m_index += n;
        }

        constexpr void safe_decrement_for_negative(difference_type n)
            noexcept (requires{{++start()} noexcept;})
            requires (check != subrange_check::NEVER && requires{{++start()};})
        {
            for (difference_type i {}; i > n; --i) {
                if (m_index == 0 || m_overflow > 0) {
                    --m_overflow;
                } else {
                    --m_index;
                    ++start();
                }
            }
        }

        constexpr void decrement_for_negative(difference_type n)
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_decrement_for_negative(n)} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_decrement_for_negative(n)} noexcept;}
            ))
            requires ((
                check == subrange_check::NEVER ||
                requires{{safe_decrement_for_negative(n)};}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_decrement_for_negative(n)};}
            ))
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_decrement_for_negative(n);
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_decrement_for_negative(n);
            } else {
                if consteval {
                    safe_decrement_for_negative(n);
                } else {
                    unsafe_decrement_for_negative(n);
                }
            }
        }

        constexpr void unsafe_increment_func()
            noexcept (requires{{step()(start())} noexcept;})
            requires (check != subrange_check::ALWAYS && requires{{step()(start())};})
        {
            step()(start());
            ++m_index;
        }

        constexpr void safe_increment_func()
            noexcept (requires{
                {empty()} noexcept;
                {step()(start())} noexcept;
            })
            requires (check != subrange_check::NEVER && requires{
                {empty()};
                {step()(start())};
            })
        {
            if (empty() || m_overflow < 0) {
                ++m_overflow;
            } else {
                step()(start());
                ++m_index;
            }
        }

    public:
        [[nodiscard]] constexpr difference_type ssize() const
            noexcept (requires{{remaining()} noexcept;})
            requires (subrange_empty<Step> && requires{{remaining()};})
        {
            return remaining();
        }

        [[nodiscard]] constexpr difference_type ssize() const
            noexcept (requires{{
                difference_type(math::div::ceil<
                    difference_type,
                    meta::unqualify<step_type>
                >{}(remaining(), step()))
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (!subrange_empty<Step> && requires{{
                difference_type(math::div::ceil<
                    difference_type,
                    meta::unqualify<step_type>
                >{}(remaining(), step()))
            };})
        {
            return difference_type(math::div::ceil<
                difference_type,
                meta::unqualify<step_type>
            >{}(remaining(), step()));
        }

        [[nodiscard]] constexpr size_type size() const
            noexcept (requires{{size_type(ssize())} noexcept;})
            requires (requires{{size_type(ssize())};})
        {
            return size_type(ssize());
        }

        constexpr void increment()
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_increment()} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_increment()} noexcept;}
            ))
            requires (subrange_simple<Start, Step>)
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_increment();
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_increment();
            } else {
                if consteval {
                    safe_increment();
                } else {
                    unsafe_increment();
                }
            }
        }

        constexpr void increment()
            noexcept (requires{{increment_by(step())} noexcept;})
            requires (subrange_linear<Start, Stop, Step>)
        {
            increment_by(step());
        }

        constexpr void increment()
            noexcept (requires{{increment_for_positive(step())} noexcept;} && (
                strictly_positive<Step> ||
                requires{{increment_for_negative(step())} noexcept;}
            ))
            requires (subrange_loop<Start, Stop, Step>)
        {
            if constexpr (strictly_positive<Step>) {
                increment_for_positive(step());
            } else {
                if (step() < 0) {
                    increment_for_negative(step());
                } else {
                    increment_for_positive(step());
                }
            }
        }

        constexpr void increment()
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_increment_func()} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_increment_func()} noexcept;}
            ))
            requires (subrange_nonlinear<Start, Stop, Step>)
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_increment_func();
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_increment_func();
            } else {
                if consteval {
                    safe_increment_func();
                } else {
                    unsafe_increment_func();
                }
            }
        }

        constexpr subrange& operator++()
            noexcept (requires{{increment()} noexcept;})
            requires (requires{{increment()};})
        {
            increment();
            return *this;
        }

        [[nodiscard]] constexpr copy operator++(int)
            noexcept (requires(copy tmp) {
                {begin()} noexcept;
                {tmp.increment()} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {tmp.increment()};
            })
        {
            copy tmp = begin();
            tmp.increment();
            return tmp;
        }

        constexpr subrange& operator+=(difference_type n)
            noexcept (requires{{increment_by(n)} noexcept;})
            requires (subrange_empty<Step> && requires{{increment_by(n)};})
        {
            increment_by(n);
            return *this;
        }

        constexpr subrange& operator+=(difference_type n)
            noexcept (requires{{increment_by(n * step())} noexcept;})
            requires (!subrange_empty<Step> && requires{{increment_by(n * step())};})
        {
            increment_by(n * step());
            return *this;
        }

        [[nodiscard]] friend constexpr copy operator+(const subrange& self, difference_type n)
            noexcept (requires(copy tmp) {
                {self.begin()} noexcept;
                {tmp += n} noexcept;
            })
            requires (requires(copy tmp) {
                {self.begin()};
                {tmp += n};
            })
        {
            copy tmp = self.begin();
            tmp += n;
            return tmp;
        }

        [[nodiscard]] friend constexpr copy operator+(difference_type n, const subrange& self)
            noexcept (requires(copy tmp) {
                {self.begin()} noexcept;
                {tmp += n} noexcept;
            })
            requires (requires(copy tmp) {
                {self.begin()};
                {tmp += n};
            })
        {
            copy tmp = self.begin();
            tmp += n;
            return tmp;
        }

        constexpr void increment(difference_type n)
            noexcept (requires{{*this += n} noexcept;})
            requires (requires{{*this += n};})
        {
            *this += n;
        }

        constexpr void increment(difference_type n)
            noexcept (requires{
                {increment_for_negative(n)} noexcept;
                {increment_for_positive(n)} noexcept;
            })
            requires (subrange_empty<Step> && !requires{{*this += n};} && requires{
                {increment_for_negative(n)};
                {increment_for_positive(n)};
            })
        {
            if (n < 0) {
                increment_for_negative(n);
            } else {
                increment_for_positive(n);
            }
        }

        constexpr void increment(difference_type n)
            noexcept (requires{
                {increment_for_negative(n * step())} noexcept;
                {increment_for_positive(n * step())} noexcept;
            })
            requires (!subrange_empty<Step> && !requires{{*this += n};} && requires{
                {increment_for_negative(n * step())};
                {increment_for_positive(n * step())};
            })
        {
            n *= step();
            if (n < 0) {
                increment_for_negative(n);
            } else {
                increment_for_positive(n);
            }
        }

        constexpr void decrement()
            noexcept ((
                check == subrange_check::NEVER ||
                requires{{safe_decrement()} noexcept;}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_decrement()} noexcept;}
            ))
            requires (subrange_empty<Step> && (
                check == subrange_check::NEVER ||
                requires{{safe_decrement()};}
            ) && (
                check == subrange_check::ALWAYS ||
                requires{{unsafe_decrement()};}
            ))
        {
            if constexpr (check == subrange_check::NEVER) {
                unsafe_decrement();
            } else if constexpr (check == subrange_check::ALWAYS) {
                safe_decrement();
            } else {
                if consteval {
                    safe_decrement();
                } else {
                    unsafe_decrement();
                }
            }
        }

        constexpr void decrement()
            noexcept (requires{{decrement_by(step())} noexcept;})
            requires (!subrange_empty<Step> && requires{{decrement_by(step())};})
        {
            decrement_by(step());
        }

        constexpr void decrement()
            noexcept (requires{{decrement_for_positive(step())};} && (
                strictly_positive<Step> ||
                requires{{decrement_for_negative(step())};
            }))
            requires (!subrange_empty<Step> && !requires{{decrement_by(step())};} && requires{
                {decrement_for_positive(step())};
            } && (
                strictly_positive<Step> ||
                requires{{decrement_for_negative(step())};
            }))
        {
            if constexpr (strictly_positive<Step>) {
                decrement_for_positive(step());
            } else {
                if (step() < 0) {
                    decrement_for_negative(step());
                } else {
                    decrement_for_positive(step());
                }
            }
        }

        constexpr subrange& operator--()
            noexcept (requires{{decrement()} noexcept;})
            requires (requires{{decrement()};})
        {
            decrement();
            return *this;
        }

        [[nodiscard]] constexpr copy operator--(int)
            noexcept (requires(copy tmp) {
                {begin()} noexcept;
                {tmp.decrement()} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {tmp.decrement()};
            })
        {
            copy tmp = begin();
            tmp.decrement();
            return tmp;
        }

        constexpr subrange& operator-=(difference_type n)
            noexcept (requires{{decrement_by(n)} noexcept;})
            requires (subrange_empty<Step> && requires{{decrement_by(n)};})
        {
            decrement_by(n);
            return *this;
        }

        constexpr subrange& operator-=(difference_type n)
            noexcept (requires{{decrement_by(n * step())} noexcept;})
            requires (!subrange_empty<Step> && requires{{decrement_by(n * step())};})
        {
            decrement_by(n * step());
            return *this;
        }

        [[nodiscard]] constexpr copy operator-(difference_type n) const
            noexcept (requires(copy tmp) {
                {begin()} noexcept;
                {tmp -= n} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {tmp -= n};
            })
        {
            copy tmp = begin();
            tmp -= n;
            return tmp;
        }

        constexpr void decrement(difference_type n)
            noexcept (requires{{*this -= n} noexcept;})
            requires (requires{{*this -= n};})
        {
            *this -= n;
        }

        constexpr void decrement(difference_type n)
            noexcept (requires{
                {decrement_for_negative(n)} noexcept;
                {decrement_for_positive(n)} noexcept;
            })
            requires (subrange_empty<Step> && !requires{{*this -= n};} && requires{
                {decrement_for_negative(n)};
                {decrement_for_positive(n)};
            })
        {
            if (n < 0) {
                decrement_for_negative(n);
            } else {
                decrement_for_positive(n);
            }
        }

        constexpr void decrement(difference_type n)
            noexcept (requires{
                {decrement_for_negative(n * step())} noexcept;
                {decrement_for_positive(n * step())} noexcept;
            })
            requires (!subrange_empty<Step> && !requires{{*this -= n};} && requires{
                {decrement_for_negative(n * step())};
                {decrement_for_positive(n * step())};
            })
        {
            n *= step();
            if (n < 0) {
                decrement_for_negative(n);
            } else {
                decrement_for_positive(n);
            }
        }
    };

    template <typename Start, typename Stop = subrange_tag, typename Step = subrange_tag>
    subrange(Start&&, Stop&& stop, Step&& step = {}) -> subrange<
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
        requires (!meta::iterator<Start>)
    range(Start&&, Stop&&, Step&& = {}) -> range<impl::iota<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >>;

    template <typename Start, typename Stop = impl::subrange_tag, typename Step = impl::subrange_tag>
        requires (meta::iterator<Start>)
    range(Start&&, Stop&&, Step&& = {}) -> range<impl::subrange<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >>;

    template <meta::not_rvalue C = impl::empty_range>
    struct unpack;

    template <typename C>
    unpack(unpack<C>&) -> unpack<unpack<C>&>;

    template <typename C>
    unpack(const unpack<C>&) -> unpack<const unpack<C>&>;

    template <typename C>
    unpack(unpack<C>&&) -> unpack<unpack<C>>;

    template <typename C>
    unpack(C&&) -> unpack<meta::remove_rvalue<C>>;

    template <typename Start, typename Stop = impl::iota_tag, typename Step = impl::iota_tag>
        requires (!meta::iterator<Start>)
    unpack(Start&&, Stop&&, Step&& = {}) -> unpack<impl::iota<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >>;

    template <typename Start, typename Stop = impl::subrange_tag, typename Step = impl::subrange_tag>
        requires (meta::iterator<Start>)
    unpack(Start&&, Stop&&, Step&& = {}) -> unpack<impl::subrange<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >>;

}


namespace impl {

    template <meta::not_rvalue T>
    struct range_iterator;

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
        using difference_type = meta::iterator_difference<T>;
        using value_type = meta::iterator_value<T>;
        using reference = meta::iterator_reference<T>;
        using pointer = meta::iterator_pointer<T>;
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

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{iter::range(*std::forward<Self>(self).iter)} noexcept;})
            requires (
                is_range_iterator<T> &&
                requires{{iter::range(*std::forward<Self>(self).iter)};}
            )
        {
            return (iter::range(*std::forward<Self>(self).iter));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).iter} noexcept;})
            requires (
                !is_range_iterator<T> &&
                requires{{*std::forward<Self>(self).iter};}
            )
        {
            return (*std::forward<Self>(self).iter);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{impl::arrow{*std::forward<Self>(self).iter}} noexcept;})
            requires (requires{{impl::arrow{*std::forward<Self>(self).iter}};})
        {
            return impl::arrow{*std::forward<Self>(self).iter};
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, difference_type n)
            noexcept (requires{{iter::unpack(std::forward<Self>(self).iter[n])} noexcept;})
            requires (
                is_range_iterator<T> &&
                requires{{iter::unpack(std::forward<Self>(self).iter[n])};}
            )
        {
            return (iter::unpack(std::forward<Self>(self).iter[n]));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, difference_type n)
            noexcept (requires{{std::forward<Self>(self).iter[n]} noexcept;})
            requires (
                !is_range_iterator<T> &&
                requires{{std::forward<Self>(self).iter[n]};}
            )
        {
            return (std::forward<Self>(self).iter[n]);
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

    template <typename T>
    range_iterator(T&&) -> range_iterator<meta::remove_rvalue<T>>;
    template <typename T>
    range_iterator(const range_iterator<T>&) -> range_iterator<range_iterator<T>>;
    template <typename T>
    range_iterator(range_iterator<T>&&) -> range_iterator<range_iterator<T>>;

    /* A wrapper for an iterator over an `iter::unpack()` object.  This acts as a
    transparent adaptor for the underlying iterator type, and does not change its
    behavior in any way.  The only caveat is for nested ranges, whereby the wrapped
    iterator will be another instance of this class, and the dereference type will be
    promoted to an `unpack` range. */
    template <meta::not_rvalue T>
    struct unpack_iterator;

    template <typename T>
    constexpr bool _is_unpack_iterator = false;
    template <typename T>
    constexpr bool _is_unpack_iterator<unpack_iterator<T>> = true;
    template <typename T>
    concept is_unpack_iterator = _is_unpack_iterator<meta::unqualify<T>>;

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

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{iter::unpack(*std::forward<Self>(self).iter)} noexcept;})
            requires (
                is_unpack_iterator<T> &&
                requires{{iter::unpack(*std::forward<Self>(self).iter)};}
            )
        {
            return (iter::unpack(*std::forward<Self>(self).iter));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).iter} noexcept;})
            requires (
                !is_unpack_iterator<T> &&
                requires{{*std::forward<Self>(self).iter};}
            )
        {
            return (*std::forward<Self>(self).iter);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{impl::arrow(*std::forward<Self>(self).iter)} noexcept;})
            requires (requires{{impl::arrow(*std::forward<Self>(self).iter)};})
        {
            return impl::arrow(*std::forward<Self>(self).iter);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, difference_type n)
            noexcept (requires{{iter::unpack(std::forward<Self>(self).iter[n])} noexcept;})
            requires (
                is_unpack_iterator<T> &&
                requires{{iter::unpack(std::forward<Self>(self).iter[n])};}
            )
        {
            return (iter::unpack(std::forward<Self>(self).iter[n]));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, difference_type n)
            noexcept (requires{{std::forward<Self>(self).iter[n]} noexcept;})
            requires (
                !is_unpack_iterator<T> &&
                requires{{std::forward<Self>(self).iter[n]};}
            )
        {
            return (std::forward<Self>(self).iter[n]);
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

    template <typename T>
    unpack_iterator(T&&) -> unpack_iterator<meta::remove_rvalue<T>>;
    template <typename T>
    unpack_iterator(const unpack_iterator<T>&) -> unpack_iterator<unpack_iterator<T>>;
    template <typename T>
    unpack_iterator(unpack_iterator<T>&&) -> unpack_iterator<unpack_iterator<T>>;

    template <typename... Ts>
    concept multidimensional_index_runtime = (meta::type_identity<Ts> && ...);

    template <typename... Ts>
    concept multidimensional_index_comptime = (!meta::type_identity<Ts> && ...);

    template <typename... Ts>
    concept multidimensional_index =
        multidimensional_index_runtime<Ts...> ||
        multidimensional_index_comptime<Ts...>;

    template <meta::integer A, meta::iterable C>
    constexpr decltype(auto) at_offset(A&& a, C&& c)
        noexcept (requires{{std::forward<C>(c).begin()[std::forward<A>(a)]} noexcept;})
        requires (requires{{std::forward<C>(c).begin()[std::forward<A>(a)]};})
    {
        return (std::forward<C>(c).begin()[std::forward<A>(a)]);
    }

    template <meta::integer A, meta::iterable C>
    constexpr decltype(auto) at_offset(A&& a, C&& c)
        noexcept (requires(meta::begin_type<C> it) {
            {std::forward<C>(c).begin()} noexcept;
            {it += std::forward<A>(a)} noexcept;
            {*it} noexcept;
        })
        requires (
            !requires{{std::forward<C>(c).begin()[std::forward<A>(a)]};} &&
            requires(meta::begin_type<C> it) {
                {std::forward<C>(c).begin()};
                {it += std::forward<A>(a)};
                {*it};
            }
        )
    {
        auto it = std::forward<C>(c).begin();
        it += std::forward<A>(a);
        return (*it);
    }

    template <meta::integer A, meta::iterable C>
    constexpr decltype(auto) at_offset(A&& a, C&& c)
        noexcept (requires(meta::begin_type<C> it) {
            {std::forward<C>(c).begin()} noexcept;
            {a > 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
            {++it} noexcept;
            {--a} noexcept;
            {a < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
            {--it} noexcept;
            {++a} noexcept;
            {*it} noexcept;
        })
        requires (
            !requires{{std::forward<C>(c).begin()[std::forward<A>(a)]};} &&
            !requires(meta::begin_type<C> it) {
                {std::forward<C>(c).begin()};
                {it += std::forward<A>(a)};
                {*it};
            } &&
            requires(meta::begin_type<C> it) {
                {std::forward<C>(c).begin()};
                {a > 0} -> meta::explicitly_convertible_to<bool>;
                {++it};
                {--a};
                {a < 0} -> meta::explicitly_convertible_to<bool>;
                {--it};
                {++a};
                {*it};
            }
        )
    {
        auto it = std::forward<C>(c).begin();
        while (a > 0) {
            ++it;
            --a;
        }
        while (a < 0) {
            --it;
            ++a;
        }
        return (*it);
    }

    template <meta::integer A, meta::iterable C>
    constexpr decltype(auto) at_offset(A&& a, C&& c)
        noexcept (requires(meta::begin_type<C> it) {
            {std::forward<C>(c).begin()} noexcept;
            {a > 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
            {++it} noexcept;
            {--a} noexcept;
            {*it} noexcept;
        })
        requires (
            !requires{{std::forward<C>(c).begin()[std::forward<A>(a)]};} &&
            !requires(meta::begin_type<C> it) {
                {std::forward<C>(c).begin()};
                {it += std::forward<A>(a)};
                {*it};
            } &&
            !requires(meta::begin_type<C> it) {
                {std::forward<C>(c).begin()};
                {a > 0} -> meta::explicitly_convertible_to<bool>;
                {++it};
                {--a};
                {a < 0} -> meta::explicitly_convertible_to<bool>;
                {--it};
                {++a};
                {*it};
            } &&
            requires(meta::begin_type<C> it) {
                {std::forward<C>(c).begin()};
                {a > 0} -> meta::explicitly_convertible_to<bool>;
                {++it};
                {--a};
                {*it};
            }
        )
    {
        auto it = std::forward<C>(c).begin();
        while (a > 0) {
            ++it;
            --a;
        }
        return (*it);
    }

    template <typename T>
    concept range_forward = meta::range<T> || impl::range_transparent<T>;

    template <typename T> requires (!range_forward<T>)
    constexpr decltype(auto) range_value(T&& t) noexcept {
        return (std::forward<T>(t));
    }

    template <range_forward T>
    constexpr decltype(auto) range_value(T&& t) noexcept {
        if constexpr (requires{{*std::forward<T>(t)} -> range_forward;}) {
            return (range_value(*std::forward<T>(t)));
        } else {
            return (*std::forward<T>(t));
        }
    }

    inline constexpr AssertionError range_front_error =
        AssertionError("Attempted to access the front of an empty range");

    inline constexpr AssertionError range_back_error =
        AssertionError("Attempted to access the back of an empty range");

    template <typename Self, typename to>
    concept range_direct_conversion = requires(Self self) {
        {impl::range_value(std::forward<Self>(self))} -> meta::convertible_to<to>;
    };

    template <typename Self, typename to>
    concept range_conversion = requires(Self self) {
        {to(std::from_range, std::forward<Self>(self))};
    };

    template <typename Self, typename to>
    concept range_traversal = requires(Self self) {
        {to(self.begin(), self.end())};
    };

    template <meta::tuple_like to, meta::range R, size_t... Is>
        requires (
            meta::tuple_like<R> &&
            meta::tuple_size<R> == meta::tuple_size<to> &&
            sizeof...(Is) == meta::tuple_size<to>
        )
    constexpr to range_convert_tuple_to_tuple(R&& r, std::index_sequence<Is...>)
        noexcept (requires{{to{std::forward<R>(r).template get<Is>()...}} noexcept;})
        requires (requires{{to{std::forward<R>(r).template get<Is>()...}};})
    {
        return to{std::forward<R>(r).template get<Is>()...};
    }

    template <typename Self, typename to>
    concept range_tuple_to_tuple =
        meta::tuple_like<Self> &&
        meta::tuple_size<to> == meta::tuple_size<Self> &&
        requires(Self self) {
            {impl::range_convert_tuple_to_tuple<to>(
                std::forward<Self>(self),
                std::make_index_sequence<meta::tuple_size<to>>{}
            )};
        };

    /// TODO: forward-declare these errors and backfill them when I've defined `repr()`
    /// and integer <-> string conversions.

    template <size_t Expected>
    constexpr TypeError range_convert_iter_to_tuple_size_error(size_t actual) noexcept {
        return TypeError(
            "Cannot convert range of size " + std::to_string(actual) +
            " to tuple of size " + std::to_string(Expected)
        );
    }

    template <size_t I, size_t N>
    constexpr TypeError range_convert_iter_to_tuple_too_small() noexcept {
        return TypeError(
            "Cannot convert range of size " + std::to_string(I) +
            " to tuple of size " + std::to_string(N)
        );
    }

    template <size_t N>
    constexpr TypeError range_convert_iter_to_tuple_too_big() noexcept {
        return TypeError(
            "Cannot convert range of more than " + std::to_string(N) +
            " elements to tuple of size " + std::to_string(N)
        );
    }

    template <meta::iterator Iter>
    constexpr decltype(auto) _range_convert_iter_to_tuple(Iter& it)
        requires (requires{
            {*it};
            {++it};
        })
    {
        decltype(auto) val = *it;
        ++it;
        return val;
    }

    template <size_t I, size_t N, meta::iterator Iter, meta::sentinel_for<Iter> End>
    constexpr decltype(auto) _range_convert_iter_to_tuple(Iter& it, End& end)
        requires (requires{
            {it == end} -> meta::explicitly_convertible_to<bool>;
            {*it};
            {++it};
        })
    {
        if (it == end) {
            throw range_convert_iter_to_tuple_too_small<I, N>();
        }
        decltype(auto) val = *it;
        ++it;
        return val;
    }

    template <meta::tuple_like to, meta::range Self, size_t... Is>
        requires (!meta::tuple_like<Self> && sizeof...(Is) == meta::tuple_size<to>)
    constexpr to range_convert_iter_to_tuple(Self& self, std::index_sequence<Is...>)
        requires (requires(decltype(self.begin()) it, decltype(self.end()) end) {
            {self.size() != sizeof...(Is)} -> meta::explicitly_convertible_to<bool>;
            {self.begin()};
            {to{(void(Is), _range_convert_iter_to_tuple(it, end))...}};
        })
    {
        if (size_t size = self.size(); size != sizeof...(Is)) {
            throw range_convert_iter_to_tuple_size_error<sizeof...(Is)>(size);
        }
        auto it = self.begin();
        return to{(void(Is), _range_convert_iter_to_tuple(it))...};
    }

    template <meta::tuple_like to, meta::range Self, size_t... Is>
        requires (!meta::tuple_like<Self> && sizeof...(Is) == meta::tuple_size<to>)
    constexpr to range_convert_iter_to_tuple(Self& self, std::index_sequence<Is...>)
        requires (
            !requires{{self.size()};} &&
            requires(decltype(self.begin()) it, decltype(self.end()) end) {
                {self.begin()};
                {self.end()};
                {to{_range_convert_iter_to_tuple<Is, sizeof...(Is)>(it, end)...}};
            }
        )
    {
        auto it = self.begin();
        auto end = self.end();
        to result {_range_convert_iter_to_tuple<Is, sizeof...(Is)>(it, end)...};
        if (!bool(it == end)) {
            throw range_convert_iter_to_tuple_too_big<sizeof...(Is)>();
        }
        return result;
    }

    template <typename Self, typename to>
    concept range_iter_to_tuple =
        !meta::tuple_like<Self> &&
        meta::tuple_like<to> &&
        requires(Self self) {
            {impl::range_convert_iter_to_tuple<to>(
                self,
                std::make_index_sequence<meta::tuple_size<to>>{}
            )};
        };

    template <typename Self, typename T>
    concept range_direct_assignment = requires(Self self, T c) {
        {impl::range_value(self) = impl::range_value(std::forward<T>(c))};
    };

    template <typename Self, typename T>
    concept range_from_scalar =
        meta::iterable<Self> &&
        requires(meta::yield_type<Self> x, const T& v) {
            {std::forward<decltype(x)>(x) = v};
        };

    template <typename Self, typename T>
    concept range_iter_from_iter = requires(
        Self self,
        T r,
        decltype(self.begin()) s_it,
        decltype(self.end()) s_end,
        decltype(r.begin()) r_it,
        decltype(r.end()) r_end
    ) {
        {self.begin()};
        {self.end()};
        {r.begin()};
        {r.end()};
        {s_it != s_end} -> meta::explicitly_convertible_to<bool>;
        {r_it != r_end} -> meta::explicitly_convertible_to<bool>;
        {*s_it = *r_it};
        {++s_it};
        {++r_it};
    } && (
        !meta::tuple_like<Self> ||
        !meta::tuple_like<T> ||
        meta::tuple_size<Self> == meta::tuple_size<T>
    );

    template <meta::range Self, meta::range R, size_t... Is>
    constexpr void range_assign_tuple_from_tuple(Self& self, R&& r, std::index_sequence<Is...>)
        noexcept (requires{
            {((self.template get<Is>() = std::forward<R>(r).template get<Is>()), ...)} noexcept;
        })
        requires (
            meta::tuple_like<Self> &&
            meta::tuple_like<R> &&
            meta::tuple_size<Self> == meta::tuple_size<R> &&
            sizeof...(Is) == meta::tuple_size<R> &&
            requires{{((self.template get<Is>() = std::forward<R>(r).template get<Is>()), ...)};}
        )
    {
        ((self.template get<Is>() = std::forward<R>(r).template get<Is>()), ...);
    }

    template <typename Self, typename T>
    concept range_tuple_from_tuple =
        meta::tuple_like<Self> &&
        meta::tuple_like<T> &&
        meta::tuple_size<Self> == meta::tuple_size<T> &&
        requires(Self self, T r) {
            {impl::range_assign_tuple_from_tuple(
                self,
                std::forward<T>(r),
                std::make_index_sequence<meta::tuple_size<Self>>{}
            )};
        };

    /// TODO: error messages can be improved after `repr()` and integer <-> string
    /// conversions are defined.

    inline constexpr TypeError range_assign_size_error(size_t expected, size_t actual) noexcept {
        return TypeError(
            "Cannot assign range of size " + std::to_string(actual) +
            " to a range with " + std::to_string(expected) + " elements"
        );
    }

    inline constexpr TypeError range_assign_iter_too_small(size_t n) noexcept {
        return TypeError(
            "Cannot assign range of size " + std::to_string(n) +
            " to a range with >" + std::to_string(n) + " elements"
        );
    }

    inline constexpr TypeError range_assign_iter_too_big(size_t n) noexcept {
        return TypeError(
            "Cannot assign range of >" + std::to_string(n) +
            " elements to a range of size " + std::to_string(n)
        );
    }

    template <size_t I, meta::range R, meta::iterator Iter>
    constexpr void _range_assign_iter_from_tuple(R&& r, Iter& it)
        requires (requires{
            {*it = std::forward<R>(r).template get<I>()};
            {++it};
        })
    {
        *it = std::forward<R>(r).template get<I>();
        ++it;
    }

    template <size_t I, meta::range R, meta::iterator Iter, meta::sentinel_for<Iter> End>
    constexpr void _range_assign_iter_from_tuple(R&& r, Iter& it, End& end)
        requires (requires{
            {it == end} -> meta::explicitly_convertible_to<bool>;
            {*it = std::forward<R>(r).template get<I>()};
            {++it};
        })
    {
        if (it == end) {
            throw range_assign_iter_too_big(I);
        }
        *it = std::forward<R>(r).template get<I>();
        ++it;
    }

    template <size_t I, meta::range Self, meta::iterator Iter>
    constexpr void _range_assign_tuple_from_iter(Self& s, Iter& it)
        requires (requires{
            {s.template get<I>() = *it};
            {++it};
        })
    {
        s.template get<I>() = *it;
        ++it;
    }

    template <size_t I, meta::range Self, meta::iterator Iter, meta::sentinel_for<Iter> End>
    constexpr void _range_assign_tuple_from_iter(Self& s, Iter& it, End& end)
        requires (requires{
            {it == end} -> meta::explicitly_convertible_to<bool>;
            {s.template get<I>() = *it};
            {++it};
        })
    {
        if (it == end) {
            throw range_assign_iter_too_small(I);
        }
        s.template get<I>() = *it;
        ++it;
    }

    template <meta::range Self, meta::range R, size_t... Is>
    constexpr void range_assign_iter_from_tuple(Self& self, R&& r, std::index_sequence<Is...>)
        requires (
            !meta::tuple_like<Self> &&
            meta::tuple_like<R> &&
            sizeof...(Is) == meta::tuple_size<R> &&
            requires(decltype(self.begin()) it) {
                {self.size() != sizeof...(Is)} -> meta::explicitly_convertible_to<bool>;
                {self.begin()};
                {(range_assign_iter_from_tuple<Is>(std::forward<R>(r), it), ...)};
            }
        )
    {
        if (self.size() != sizeof...(Is)) {
            throw range_assign_size_error(self.size(), sizeof...(Is));
        }
        auto it = self.begin();
        (range_assign_iter_from_tuple<Is>(std::forward<R>(r), it), ...);
    }

    template <meta::range Self, meta::range R, size_t... Is>
    constexpr void range_assign_iter_from_tuple(Self& self, R&& r, std::index_sequence<Is...>)
        requires (
            !meta::tuple_like<Self> &&
            meta::tuple_like<R> &&
            sizeof...(Is) == meta::tuple_size<R> &&
            !requires{{self.size()};} &&
            requires(decltype(self.begin()) it, decltype(self.end()) end) {
                {self.begin()};
                {self.end()};
                {(range_assign_iter_from_tuple<Is>(std::forward<R>(r), it, end), ...)};
            }
        )
    {
        auto it = self.begin();
        auto end = self.end();
        (range_assign_iter_from_tuple<Is>(std::forward<R>(r), it, end), ...);
        if (!bool(it == end)) {
            throw range_assign_iter_too_small(sizeof...(Is));
        }
    }

    template <meta::range Self, meta::range R, size_t... Is>
    constexpr void range_assign_tuple_from_iter(Self& self, R&& r, std::index_sequence<Is...>)
        requires (
            meta::tuple_like<Self> &&
            !meta::tuple_like<R> &&
            sizeof...(Is) == meta::tuple_size<Self> &&
            requires(decltype(r.begin()) it) {
                {sizeof...(Is) != r.size()} -> meta::explicitly_convertible_to<bool>;
                {r.begin()};
                {(_range_assign_tuple_from_iter<Is>(self, it), ...)};
            }
        )
    {
        if (sizeof...(Is) != r.size()) {
            throw range_assign_size_error(sizeof...(Is), r.size());
        }
        auto it = r.begin();
        (_range_assign_tuple_from_iter<Is>(self, it), ...);
    }

    template <meta::range Self, meta::range R, size_t... Is>
    constexpr void range_assign_tuple_from_iter(Self& self, R&& r, std::index_sequence<Is...>)
        requires (
            meta::tuple_like<Self> &&
            !meta::tuple_like<R> &&
            sizeof...(Is) == meta::tuple_size<Self> &&
            !requires{{r.size()};} &&
            requires(decltype(r.begin()) it, decltype(r.end()) end) {
                {r.begin()};
                {r.end()};
                {(_range_assign_tuple_from_iter<Is>(self, it, end), ...)};
            }
        )
    {
        auto it = r.begin();
        auto end = r.end();
        (_range_assign_tuple_from_iter<Is>(self, it, end), ...);
        if (!bool(it == end)) {
            throw range_assign_iter_too_big(sizeof...(Is));
        }
    }

    template <typename Self, typename T>
    concept range_iter_from_tuple =
        !meta::tuple_like<Self> &&
        meta::tuple_like<T> &&
        requires(Self self, T r) {
            {impl::range_assign_iter_from_tuple(
                self,
                std::forward<T>(r),
                std::make_index_sequence<meta::tuple_size<T>>{}
            )};
        };

    template <typename Self, typename T>
    concept range_tuple_from_iter =
        meta::tuple_like<Self> &&
        !meta::tuple_like<T> &&
        requires(Self self, T r) {
            {impl::range_assign_tuple_from_iter(
                self,
                std::forward<T>(r),
                std::make_index_sequence<meta::tuple_size<Self>>{}
            )};
        };

}


namespace iter {

    /* Range-based logical disjunction operator.  Accepts any number of arguments that
    are explicitly convertible to `bool` and returns true if at least one evaluates to
    true.  If a range is supplied, then the conversion will be broadcasted over all its
    elements before advancing to the next argument.  A custom function predicate may be
    supplied as an initializer, which will be applied to each value. */
    template <meta::not_rvalue F = impl::ExplicitConvertTo<bool>>
    struct any {
    private:
        template <typename T>
        struct call {
            static constexpr bool operator()(meta::as_const_ref<F> func, meta::forward<T> v)
                noexcept (requires{
                    {
                        func(std::forward<T>(v))
                    } noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                })
                requires (requires{
                    {func(std::forward<T>(v))} -> meta::explicitly_convertible_to<bool>;
                })
            {
                return bool(func(std::forward<T>(v)));
            }
        };

        template <meta::range T>
        struct call<T> {
            static constexpr bool operator()(meta::as_const_ref<F> func, meta::forward<T> v)
                noexcept (meta::nothrow::iterable<T> && requires(meta::yield_type<T> x) {
                    {
                        call<decltype(x)>{}(func, std::forward<decltype(x)>(x))
                    } noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                })
                requires (requires(meta::yield_type<T> x) {
                    {
                        call<decltype(x)>{}(func, std::forward<decltype(x)>(x))
                    } -> meta::explicitly_convertible_to<bool>;
                })
            {
                for (auto&& x : v) {
                    if (call<decltype(x)>{}(std::forward<decltype(x)>(x))) {
                        return true;
                    }
                }
                return false;
            }
        };

    public:
        [[no_unique_address]] F func;

        template <typename... A>
        [[nodiscard]] constexpr bool operator()(A&&... a) const
            noexcept (requires{{(call<A>{}(func, std::forward<A>(a)) || ...)} noexcept;})
            requires (requires{{(call<A>{}(func, std::forward<A>(a)) || ...)};})
        {
            return (call<A>{}(func, std::forward<A>(a)) || ...);
        }
    };

    template <typename F>
    any(F&&) -> any<meta::remove_rvalue<F>>;

    /* Range-based logical conjunction operator.  Accepts any number of arguments that
    are explicitly convertible to `bool` and returns true if all of them evaluate to
    true.  If a range is supplied, then the conversion will be broadcasted over all its
    elements before advancing to the next argument.  A custom function predicate may be
    supplied as an initializer, which will be applied to each value. */
    template <meta::not_rvalue F = impl::ExplicitConvertTo<bool>>
    struct all {
    private:
        template <typename T>
        struct call {
            static constexpr bool operator()(meta::as_const_ref<F> func, meta::forward<T> v)
                noexcept (requires{
                    {
                        func(std::forward<T>(v))
                    } noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                })
                requires (requires{
                    {func(std::forward<T>(v))} -> meta::explicitly_convertible_to<bool>;
                })
            {
                return bool(func(std::forward<T>(v)));
            }
        };

        template <meta::range T>
        struct call<T> {
            static constexpr bool operator()(meta::as_const_ref<F> func, meta::forward<T> v)
                noexcept (meta::nothrow::iterable<T> && requires(meta::yield_type<T> x) {
                    {
                        !call<decltype(x)>{}(func, std::forward<decltype(x)>(x))
                    } noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                })
                requires (requires(meta::yield_type<T> x) {
                    {
                        !call<decltype(x)>{}(func, std::forward<decltype(x)>(x))
                    } -> meta::explicitly_convertible_to<bool>;
                })
            {
                for (auto&& x : v) {
                    if (!call<decltype(x)>{}(std::forward<decltype(x)>(x))) {
                        return true;
                    }
                }
                return false;
            }
        };

    public:
        [[no_unique_address]] F func;

        template <typename... A>
        [[nodiscard]] constexpr bool operator()(A&&... a) const
            noexcept (requires{{(call<A>{}(func, std::forward<A>(a)) && ...)} noexcept;})
            requires (requires{{(call<A>{}(func, std::forward<A>(a)) && ...)};})
        {
            return (call<A>{}(func, std::forward<A>(a)) && ...);
        }
    };

    template <typename F>
    all(F&&) -> all<meta::remove_rvalue<F>>;

    /* Check to see whether a particular value or consecutive subsequence is present
    in the arguments.  Multiple arguments may be supplied, in which case the search
    will proceed from left to right, stopping as soon as a match is found.

    The initializer may be any of the following (in order of precedence):

        1.  A single value for which `value == arg` is well-formed.
        2.  A function object for which `value(arg)` is well-formed and returns a type
            explicitly convertible to `bool`, where `true` terminates the search.
        3.  A valid input to an `arg.contains()` method, if one exists.
        4.  A linear search through the top-level elements of a tuple or iterable type,
            applying (1) and (2) to each result.  If the comparison value is a range,
            then it will be interpreted as a subsequence, and will only return true if
            all of its elements are found in the proper order.
    */
    template <meta::not_rvalue T>
    struct contains {
        [[no_unique_address]] T k;

    private:
        template <typename K, typename A>
        static constexpr bool equality =
            !meta::range<K> && !meta::range<A> && requires(const K& k, const A& a) {
                {k == a} -> meta::explicitly_convertible_to<bool>;
            };

        template <typename K, typename A>
        static constexpr bool predicate =
            !meta::range<K> && requires(const K& k, const A& a) {
                {k(a)} -> meta::explicitly_convertible_to<bool>;
            };

        // 1a) prefer `arg == value` if it is well-formed, and neither argument is a
        //     range
        template <typename K, typename A>
        static constexpr bool scalar(const K& k, const A& a)
            noexcept (requires{
                {k == a} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
            })
            requires (equality<K, A>)
        {
            return bool(k == a);
        }

        // 1b) allow `value(arg)` if it is well-formed, returns a boolean, and `value`
        //     is not a range
        template <typename K, typename A>
        static constexpr bool scalar(const K& k, const A& a)
            noexcept (requires{
                {k(a)} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
            })
            requires (!equality<K, A> && predicate<K, A>)
        {
            return bool(k(a));
        }

        // 1) dispatch to the appropriate scalar comparison
        template <typename K, typename A>
        static constexpr bool call(const K& k, const A& a)
            noexcept (requires{{scalar(k, a)} noexcept;})
            requires (equality<K, A> || predicate<K, A>)
        {
            return scalar(k, a);
        }

        template <typename K, typename A>
        static constexpr bool member = false;
        template <typename K, typename A> requires (!meta::range<A>)
        static constexpr bool member<K, A> = requires(const K& k, const A& a) {
            {a.contains(k)} -> meta::convertible_to<bool>;
        };

        // 2) prefer `arg.contains(value)` if it exists and `arg` is not a range
        template <typename K, typename A>
        static constexpr bool call(const K& k, const A& a)
            noexcept (requires{
                {a.contains(k)} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (
                !equality<K, A> &&
                !predicate<K, A> &&
                member<K, A>
            )
        {
            return a.contains(k);
        }

        template <typename K, typename A>
        static constexpr bool search = false;
        template <typename K, typename A> requires (!meta::range<K> && meta::iterable<const A&>)
        static constexpr bool search<K, A> = requires(
            const K& k,
            meta::as_const_ref<meta::yield_type<const A&>> x
        ) {
            {scalar(k, x)};
        };

        template <typename K, typename A>
        static constexpr bool subsequence = false;
        template <typename K, typename A> requires (meta::range<K> && meta::iterable<const A&>)
        static constexpr bool subsequence<K, A> = requires(
            const K& k,
            const A& arg,
            meta::begin_type<const K&> k_begin,
            meta::begin_type<const K&> k_it,
            meta::end_type<const K&> k_end,
            meta::begin_type<const A&> a_it,
            meta::end_type<const A&> a_end
        ) {
            {k.begin()} -> meta::copyable;
            {k.end()};
            {k_it == k_end} -> meta::explicitly_convertible_to<bool>;
            {std::ranges::begin(arg)};
            {std::ranges::end(arg)};
            {a_it != a_end} -> meta::explicitly_convertible_to<bool>;
            {scalar(*k_it, *a_it)};
            {++k_it};
            {k_it = k_begin};
            {++a_it};
        };

        template <typename K, typename A>
        static constexpr bool vector(const K& k, const A& a)
            noexcept (requires(
                meta::begin_type<const K&> k_begin,
                meta::begin_type<const K&> k_it,
                meta::end_type<const K&> k_end,
                meta::begin_type<const A&> a_it,
                meta::end_type<const A&> a_end
            ) {
                {k.begin()} noexcept -> meta::nothrow::copyable;
                {k.end()} noexcept;
                {k_it == k_end} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {std::ranges::begin(a)} noexcept;
                {std::ranges::end(a)} noexcept;
                {a_it != a_end} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {scalar(*k_it, *a_it)} noexcept;
                {++k_it} noexcept;
                {k_it = k_begin} noexcept;
                {++a_it} noexcept;
            })
            requires (subsequence<K, A>)
        {
            auto k_begin = k.begin();
            auto k_it = k_begin;
            auto k_end = k.end();
            if (k_it == k_end) {
                return true;  // empty range
            }
            auto a_it = std::ranges::begin(a);
            auto a_end = std::ranges::end(a);
            while (a_it != a_end) {
                if (scalar(*k_it, *a_it)) {
                    ++k_it;
                    if (k_it == k_end) {
                        return true;
                    }
                } else {
                    k_it = k_begin;
                }
                ++a_it;
            }
            return false;
        }

        // 3a) Do a linear search for a scalar value within an iterable argument
        template <typename K, typename A>
        static constexpr bool call(const K& k, const A& arg)
            noexcept (meta::nothrow::iterable<const A&> && requires(
                meta::as_const_ref<meta::yield_type<const A&>> x
            ) {
                {scalar(k, x)} noexcept;
            })
            requires (
                !equality<K, A> &&
                !predicate<K, A> &&
                !member<K, A> &&
                search<K, A>
            )
        {
            for (const auto& x : arg) {
                if (scalar(k, x)) {
                    return true;
                }
            }
            return false;
        }

        // 3b) Do a linear search for a range subsequence within an iterable argument
        template <typename K, typename A>
        static constexpr bool call(const K& k, const A& a)
            noexcept (requires{{vector(k, a)} noexcept;})
            requires (
                !equality<K, A> &&
                !predicate<K, A> &&
                !member<K, A> &&
                !search<K, A> &&
                subsequence<K, A>
            )
        {
            return vector(k, a);
        }

        template <typename K, typename A, size_t... Is>
        static constexpr bool destructure(const K& k, const A& a, std::index_sequence<Is...>)
            noexcept (requires{{(scalar(k, meta::get<Is>(a)) || ...)} noexcept;})
            requires (requires{{(scalar(k, meta::get<Is>(a)) || ...)};})
        {
            return (scalar(k, meta::get<Is>(a)) || ...);
        }

        template <typename K, typename A>
        static constexpr bool tuple =
            !meta::range<K> && meta::tuple_like<A> && requires(const K& k, const A& a) {
                {destructure(k, a, std::make_index_sequence<meta::tuple_size<A>>{})};
            };

        template <typename K, typename A>
        static constexpr bool tuple_subsequence = false;
        template <typename K, typename A> requires (meta::range<K> && meta::tuple_like<A>)
        static constexpr bool tuple_subsequence<K, A> = subsequence<K, impl::tuple_range<const A&>>;

        // 4a) destructure tuple-like arguments and do a scalar comparison against each
        //    element
        template <typename K, typename A>
        static constexpr bool call(const K& k, const A& a)
            noexcept (requires{
                {destructure(k, a, std::make_index_sequence<meta::tuple_size<A>>{})} noexcept;
            })
            requires (
                !equality<K, A> &&
                !predicate<K, A> &&
                !member<K, A> &&
                !search<K, A> &&
                !subsequence<K, A> &&
                !tuple_subsequence<K, A> &&
                tuple<K, A>
            )
        {
            return destructure(k, a, std::make_index_sequence<meta::tuple_size<A>>{});
        }

        // 4b) Do a linear search for a range subsequence within a tuple-like argument
        //     that is not otherwise iterable
        template <typename K, typename A>
        static constexpr bool call(const K& k, const A& a)
            noexcept (requires{{vector(k, impl::tuple_range{a})} noexcept;})
            requires (
                !equality<K, A> &&
                !predicate<K, A> &&
                !member<K, A> &&
                !search<K, A> &&
                !subsequence<K, A> &&
                tuple_subsequence<K, A>
            )
        {
            return vector(k, impl::tuple_range{a});
        }

    public:
        template <typename... A>
        [[nodiscard]] constexpr bool operator()(const A&... a) const
            noexcept (requires{{(call(k, a) || ...)} noexcept;})
            requires (requires{{(call(k, a) || ...)};})
        {
            return (call(k, a) || ...);
        }
    };

    template <typename T>
    contains(T&&) -> contains<meta::remove_rvalue<T>>;

    /* Range-based multidimensional indexing operator.  Accepts any number of values,
    which will be used to index into a container when called.  If more than one index
    is given, then they will be applied sequentially, effectively equating to a chain
    of `container[i][j][k]...` calls behind the scenes.

    The precise behavior for each index is as follows (in order of preference):

        1.  Direct indexing via `container[index]` or `get<index>(container)` (or an
            equivalent `container.get<index>()` member method).  The former is
            preferred for runtime indices, while the latter is preferred at compile
            time.
        2.  Integer values that do not satisfy (1).  These will be interpreted as
            offsets from the `begin()` iterator of the container, and will be applied
            using iterator arithmetic, including a linear search if necessary.
        3.  Predicate functions that take the container as an argument and return an
            arbitrary value.  This includes range adaptors such as `iter::slice{}`,
            `iter::find{}`, etc.

    The result of each indexing operation will be passed as the container to the next
    index, with the final result being returned to the caller.

    This specialization accepts the indices as non-type template parameters, allowing
    them to be applied via the tuple protocol if the container supports it, which may
    yield a more exact type with lower overhead. */
    template <auto... K> requires (impl::multidimensional_index<decltype(K)...>)
    struct at {
    private:
        // 1) Prefer tuple indexing if available
        template <auto A, typename C> requires (!meta::range<C>)
        static constexpr decltype(auto) call(C&& c)
            noexcept (requires{{meta::get<A>(std::forward<C>(c))} noexcept;})
            requires (requires{{meta::get<A>(std::forward<C>(c))};})
        {
            return (meta::get<A>(std::forward<C>(c)));
        }

        // 2) If tuple indexing is not possible, fall back to runtime indexing
        template <auto A, typename C> requires (!meta::range<C>)
        static constexpr decltype(auto) call(C&& c)
            noexcept (requires{{std::forward<C>(c)[A]} noexcept;})
            requires (
                !requires{{meta::get<A>(std::forward<C>(c))};} &&
                requires{{std::forward<C>(c)[A]};}
            )
        {
            return (std::forward<C>(c)[A]);
        }

        // 3) If direct indexing is not possible and the index type is an integer, then
        //    it can be interpreted as an offset from the `begin()` iterator
        template <auto A, typename C> requires (!meta::range<C>)
        static constexpr decltype(auto) call(C&& c)
            noexcept (requires{{impl::at_offset(A, std::forward<C>(c))} noexcept;})
            requires (
                !requires{{meta::get<A>(std::forward<C>(c))};} &&
                !requires{{std::forward<C>(c)[A]};} &&
                requires{{impl::at_offset(A, std::forward<C>(c))};}
            )
        {
            return (impl::at_offset(A, std::forward<C>(c)));
        }

        // 4) If indexing is not possible, the current argument must be a predicate
        //    function that can be called with the container and does not return void
        template <auto A, typename C> requires (!meta::range<C>)
        static constexpr decltype(auto) call(C&& c)
            noexcept (requires{{A(std::forward<C>(c))} noexcept;})
            requires (
                !requires{{meta::get<A>(std::forward<C>(c))};} &&
                !requires{{std::forward<C>(c)[A]};} &&
                !requires{{impl::at_offset(A, std::forward<C>(c))};} &&
                requires{{A(std::forward<C>(c))} -> meta::not_void;}
            )
        {
            return (A(std::forward<C>(c)));
        }

        // 5a) If the container is a range, then the indexing will be forwarded to the
        //     underlying container using overloads (1), (2), and (3)
        template <auto A, meta::range R>
        static constexpr decltype(auto) call(R&& r)
            noexcept (requires{{call<A>(*std::forward<R>(r).__value)} noexcept;})
            requires (
                !meta::range<decltype((*std::forward<R>(r).__value))> &&
                requires{{call<A>(*std::forward<R>(r).__value)};}
            )
        {
            return (call<A>(*std::forward<R>(r).__value));
        }

        // 5b) If the container is a nested range, then the result will be promoted to
        //     a range in order to preserve the nested structure
        template <auto A, meta::range R>
        static constexpr decltype(auto) call(R&& r)
            noexcept (requires{
                {iter::range(call<A>(*std::forward<R>(r).__value))} noexcept;
            })
            requires (
                meta::range<decltype((*std::forward<R>(r).__value))> &&
                requires{{iter::range(call<A>(*std::forward<R>(r).__value))};}
            )
        {
            return (iter::range(call<A>(*std::forward<R>(r).__value)));
        }

        // 5c) If the container is an `iter::unpack<T>`, where `T` is a range, then
        //     the result will be promoted to an `iter::unpack` range in order to
        //     preserve the unpacking structure
        template <auto A, meta::unpack R>
        static constexpr decltype(auto) call(R&& r)
            noexcept (requires{
                {iter::unpack(call<A>(*std::forward<R>(r).__value))} noexcept;
            })
            requires (
                meta::range<decltype((*std::forward<R>(r).__value))> &&
                requires{{iter::unpack(call<A>(*std::forward<R>(r).__value))};}
            )
        {
            return (iter::unpack(call<A>(*std::forward<R>(r).__value)));
        }

        // Evaluating the metafunction has to keep track of the current result, which
        // gets stored as the argument to the next call
        template <auto A, auto... As, typename C> requires (sizeof...(As) > 0)
        static constexpr decltype(auto) eval(C&& c)
            noexcept (requires{{eval<As...>(call<A>(std::forward<C>(c)))} noexcept;})
            requires (requires{{eval<As...>(call<A>(std::forward<C>(c)))};})
        {
            return (eval<As...>(call<A>(std::forward<C>(c))));
        }
        template <auto A, typename C>
        static constexpr decltype(auto) eval(C&& c)
            noexcept (requires{{call<A>(std::forward<C>(c))} noexcept;})
            requires (requires{{call<A>(std::forward<C>(c))};})
        {
            return (call<A>(std::forward<C>(c)));
        }

    public:
        template <typename C>
        [[nodiscard]] static constexpr decltype(auto) operator()(C&& c)
            noexcept (sizeof...(K) == 0 || requires{{eval<K...>(std::forward<C>(c))} noexcept;})
            requires (sizeof...(K) == 0 || requires{{eval<K...>(std::forward<C>(c))};})
        {
            if constexpr (sizeof...(K) == 0) {
                return (std::forward<C>(c));
            } else {
                return (eval<K...>(std::forward<C>(c)));
            }
        }
    };

    /* Range-based multidimensional indexing operator.  Accepts any number of values,
    which will be used to index into a container when called.  If more than one index
    is given, then they will be applied sequentially, effectively equating to a chain
    of `container[i][j][k]...` calls behind the scenes.

    The precise behavior for each index is as follows (in order of preference):

        1.  Direct indexing via `container[index]` or `get<index>(container)` (or an
            equivalent `container.get<index>()` member method).  The former is
            preferred for runtime indices, while the latter is preferred at compile
            time.
        2.  Integer values that do not satisfy (1).  These will be interpreted as
            offsets from the `begin()` iterator of the container, and will be applied
            using iterator arithmetic.
        3.  Predicate functions that take the container as an argument and return an
            arbitrary value.  This includes range adaptors such as `iter::slice{}`,
            `iter::find{}`, etc.

    The result of each indexing operation will be passed as the container to the next
    index, with the final result being returned to the caller.

    This specialization accepts the indices as run-time constructor arguments,
    restricting their use with the tuple protocol.  Therefore, if the container is
    tuple-like but not otherwise subscriptable, the indexing will be done using a
    vtable dispatch that maps indices to the appropriate element, possibly returning a
    union if the tuple contains heterogenous types. */
    template <auto... K>
        requires (
            impl::multidimensional_index<decltype(K)...> &&
            impl::multidimensional_index_runtime<decltype(K)...>
        )
    struct at<K...> {
        [[no_unique_address]] impl::basic_tuple<typename decltype(K)::type...> idx;

        [[nodiscard]] constexpr at() = default;
        [[nodiscard]] constexpr at(meta::forward<typename decltype(K)::type>... k)
            noexcept (meta::nothrow::constructible_from<
                impl::basic_tuple<typename decltype(K)::type...>,
                meta::forward<typename decltype(K)::type>...
            >)
            requires (meta::constructible_from<
                impl::basic_tuple<typename decltype(K)::type...>,
                meta::forward<typename decltype(K)::type>...
            >)
        :
            idx{std::forward<typename decltype(K)::type>(k)...}
        {}

    private:
        // 1) Prefer direct indexing if available
        template <typename A, typename C> requires (!meta::range<C>)
        static constexpr decltype(auto) call(A&& a, C&& c)
            noexcept (requires{{std::forward<C>(c)[std::forward<A>(a)]} noexcept;})
            requires (requires{{std::forward<C>(c)[std::forward<A>(a)]};})
        {
            return (std::forward<C>(c)[std::forward<A>(a)]);
        }

        // 2) Wrap tuple-like types that do not support direct indexing
        template <typename A, typename C> requires (!meta::range<C>)
        static constexpr decltype(auto) call(A&& a, C&& c)
            noexcept (requires{
                {impl::tuple_range(std::forward<C>(c))[std::forward<A>(a)]} noexcept;
            })
            requires (
                !requires{{std::forward<C>(c)[std::forward<A>(a)]};} &&
                requires{{impl::tuple_range(std::forward<C>(c))[std::forward<A>(a)]};}
            )
        {
            return (impl::tuple_range(std::forward<C>(c))[std::forward<A>(a)]);
        }

        // 3) If direct indexing is not possible and the index type is an integer, then
        //    it can be interpreted as an offset from the `begin()` iterator
        template <meta::integer A, typename C> requires (!meta::range<C>)
        static constexpr decltype(auto) call(A&& a, C&& c)
            noexcept (requires{{impl::at_offset(std::forward<A>(a), std::forward<C>(c))} noexcept;})
            requires (
                !requires{{std::forward<C>(c)[std::forward<A>(a)]};} &&
                !requires{{impl::tuple_range(std::forward<C>(c))[std::forward<A>(a)]};} &&
                requires{{impl::at_offset(std::forward<A>(a), std::forward<C>(c))};}
            )
        {
            return (impl::at_offset(std::forward<A>(a), std::forward<C>(c)));
        }

        // 4) If indexing is not possible, the current argument must be a predicate
        //    function that can be called with the container and does not return void
        template <typename A, typename C> requires (!meta::range<C>)
        static constexpr decltype(auto) call(A&& a, C&& c)
            noexcept (requires{{std::forward<A>(a)(std::forward<C>(c))} noexcept;})
            requires (
                !requires{{std::forward<C>(c)[std::forward<A>(a)]};} &&
                !requires{{impl::tuple_range(std::forward<C>(c))[std::forward<A>(a)]};} &&
                !requires{{impl::at_offset(std::forward<A>(a), std::forward<C>(c))};} &&
                requires{{std::forward<A>(a)(std::forward<C>(c))} -> meta::not_void;}
            )
        {
            return (std::forward<A>(a)(std::forward<C>(c)));
        }

        // 5a) If the container is a range, then the indexing will be forwarded to the
        //     underlying container using overloads (1), (2), (3), and (4)
        template <typename A, meta::range R>
        static constexpr decltype(auto) call(A&& a, R&& r)
            noexcept (requires{{call(std::forward<A>(a), *std::forward<R>(r).__value)} noexcept;})
            requires (
                !meta::range<decltype((*std::forward<R>(r).__value))> &&
                requires{{call(std::forward<A>(a), *std::forward<R>(r).__value)};}
            )
        {
            return (call(std::forward<A>(a), *std::forward<R>(r).__value));
        }

        // 5b) If the container is a nested range, then the result will be promoted to
        //     a range in order to preserve the nested structure
        template <typename A, meta::range R>
        static constexpr decltype(auto) call(A&& a, R&& r)
            noexcept (requires{
                {iter::range(call(std::forward<A>(a), *std::forward<R>(r).__value))} noexcept;
            })
            requires (
                meta::range<decltype((*std::forward<R>(r).__value))> &&
                requires{{iter::range(call(std::forward<A>(a), *std::forward<R>(r).__value))};}
            )
        {
            return (iter::range(call(std::forward<A>(a), *std::forward<R>(r).__value)));
        }

        // 5c) If the container is an `iter::unpack<T>`, where `T` is a range, then
        //     the result will be promoted to an `iter::unpack` range in order to
        //     preserve the unpacking structure
        template <typename A, meta::unpack R>
        static constexpr decltype(auto) call(A&& a, R&& r)
            noexcept (requires{
                {iter::unpack(call(std::forward<A>(a), *std::forward<R>(r).__value))} noexcept;
            })
            requires (
                meta::range<decltype((*std::forward<R>(r).__value))> &&
                requires{{iter::unpack(call(std::forward<A>(a), *std::forward<R>(r).__value))};}
            )
        {
            return (iter::unpack(call(std::forward<A>(a), *std::forward<R>(r).__value)));
        }

    public:
        template <size_t N = 0, typename C> requires (N == sizeof...(K))
        [[nodiscard]] static constexpr decltype(auto) operator()(C&& c)
            noexcept (requires{{std::forward<C>(c)} noexcept;})
            requires (requires{{std::forward<C>(c)};})
        {
            return (std::forward<C>(c));
        }

        template <size_t N = 0, typename Self, typename C> requires (N < sizeof...(K))
        [[nodiscard]] constexpr decltype(auto) operator()(this Self&& self, C&& c)
            noexcept (requires{{std::forward<Self>(self).template operator()<N + 1>(call(
                std::forward<Self>(self).idx.template get<N>(),
                std::forward<C>(c)
            ))} noexcept;})
            requires (requires{{std::forward<Self>(self).template operator()<N + 1>(call(
                std::forward<Self>(self).idx.template get<N>(),
                std::forward<C>(c)
            ))};})
        {
            /// NOTE: without this constexpr branch, a reference to a temporary may be
            /// returned from the base case, which can cause UB.
            if constexpr (N + 1 == sizeof...(K)) {
                return (call(
                    std::forward<Self>(self).idx.template get<N>(),
                    std::forward<C>(c)
                ));
            } else {
                return (std::forward<Self>(self).template operator()<N + 1>(call(
                    std::forward<Self>(self).idx.template get<N>(),
                    std::forward<C>(c)
                )));
            }
        }
    };

    template <typename... K>
    at(K&&...) -> at<type<meta::remove_rvalue<K>>...>;

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
            noexcept (requires{{impl::ref<C>{C(std::forward<A>(args)...)}} noexcept;})
            requires (!meta::range<C> && requires{{impl::ref<C>{C(std::forward<A>(args)...)}};})
        :
            __value{C(std::forward<A>(args)...)}
        {}

        template <typename... A> requires (sizeof...(A) > 0)
        [[nodiscard]] constexpr explicit range(A&&... args)
            noexcept (requires{{impl::ref<C>{C(std::forward<A>(args)...)}} noexcept;})
            requires (meta::range<C> && requires{{impl::ref<C>{C(std::forward<A>(args)...)}};})
        :
            __value{C(std::forward<A>(args)...)}
        {}

        template <typename Start, typename Stop = impl::iota_tag, typename Step = impl::iota_tag>
        [[nodiscard]] constexpr explicit range(
            Start&& start,
            Stop&& stop,
            Step&& step = {}
        )
            requires (!meta::iterator<Start> && meta::inherits<C, impl::iota_tag>)
        :
            __value{C(
                std::forward<Start>(start),
                std::forward<Stop>(stop),
                std::forward<Step>(step)
            )}
        {}

        template <typename Start, typename Stop = impl::subrange_tag, typename Step = impl::subrange_tag>
        [[nodiscard]] constexpr explicit range(
            Start&& start,
            Stop&& stop,
            Step&& step = {}
        )
            requires (meta::iterator<Start> && meta::inherits<C, impl::subrange_tag>)
        :
            __value{C(
                std::forward<Start>(start),
                std::forward<Stop>(stop),
                std::forward<Step>(step)
            )}
        {}

        /* `swap()` operator between ranges. */
        constexpr void swap(range& other)
            noexcept (requires{{std::ranges::swap(__value, other.__value)} noexcept;})
            requires (requires{{std::ranges::swap(__value, other.__value)};})
        {
            std::ranges::swap(__value, other.__value);
        }

        /* Strip one layer of range, potentially exposing the underlying container.
        This operator is the logical inverse of the nested `range()` constructor,
        such that `*range(x)` is equivalent to `x` and `*range(range(x))` equates to
        `range(x)`.  Together, these operators give fine control over the nesting
        level of ranges, and contributes to a generally pointer-like interface for
        range monads, similar to other monadic types. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept {
            if constexpr (impl::range_transparent<C>) {
                return (**std::forward<Self>(self).__value);
            } else {
                return (*std::forward<Self>(self).__value);
            }
        }

        /* Indirectly access a member of the underlying container.  Note that this
        operator skips nested ranges, and always returns a pointer directly to the
        underlying container.  Use `*` if you want to strip just one level at a time. */
        [[nodiscard]] constexpr auto operator->() noexcept {
            return std::addressof(impl::range_value(*this));
        }

        /* Indirectly access a member of the underlying container.  Note that this
        operator skips nested ranges, and always returns a pointer directly to the
        underlying container.  Use `*` if you want to strip just one level at a time. */
        [[nodiscard]] constexpr auto operator->() const noexcept {
            return std::addressof(impl::range_value(*this));
        }

        /* Get a forward iterator to the start of the range. */
        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{impl::range_iterator{std::ranges::begin(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{std::ranges::begin(*__value)}};})
        {
            return impl::range_iterator{std::ranges::begin(*__value)};
        }

        /* Get a forward iterator to the start of the range. */
        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{impl::range_iterator{std::ranges::begin(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{std::ranges::begin(*__value)}};})
        {
            return impl::range_iterator{std::ranges::begin(*__value)};
        }

        /* Get a forward iterator to the start of the range. */
        [[nodiscard]] constexpr auto cbegin() const
            noexcept (requires{{impl::range_iterator{std::ranges::cbegin(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{std::ranges::cbegin(*__value)}};})
        {
            return impl::range_iterator{std::ranges::cbegin(*__value)};
        }

        /* Get a forward iterator to one past the last element of the range. */
        [[nodiscard]] constexpr auto end()
            noexcept (requires{{impl::range_iterator{std::ranges::end(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{std::ranges::end(*__value)}};})
        {
            return impl::range_iterator{std::ranges::end(*__value)};
        }

        /* Get a forward iterator to one past the last element of the range. */
        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{impl::range_iterator{std::ranges::end(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{std::ranges::end(*__value)}};})
        {
            return impl::range_iterator{std::ranges::end(*__value)};
        }

        /* Get a forward iterator to one past the last element of the range. */
        [[nodiscard]] constexpr auto cend() const
            noexcept (requires{{impl::range_iterator{std::ranges::cend(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{std::ranges::cend(*__value)}};})
        {
            return impl::range_iterator{std::ranges::cend(*__value)};
        }

        /* Get a reverse iterator to the last element of the range. */
        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{impl::range_iterator{std::ranges::rbegin(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{std::ranges::rbegin(*__value)}};})
        {
            return impl::range_iterator{std::ranges::rbegin(*__value)};
        }

        /* Get a reverse iterator to the last element of the range. */
        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{impl::range_iterator{std::ranges::rbegin(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{std::ranges::rbegin(*__value)}};})
        {
            return impl::range_iterator{std::ranges::rbegin(*__value)};
        }

        /* Get a reverse iterator to the last element of the range. */
        [[nodiscard]] constexpr auto crbegin() const
            noexcept (requires{{impl::range_iterator{std::ranges::crbegin(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{std::ranges::crbegin(*__value)}};})
        {
            return impl::range_iterator{std::ranges::crbegin(*__value)};
        }

        /* Get a reverse iterator to one before the first element of the range. */
        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{impl::range_iterator{std::ranges::rend(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{std::ranges::rend(*__value)}};})
        {
            return impl::range_iterator{std::ranges::rend(*__value)};
        }

        /* Get a reverse iterator to one before the first element of the range. */
        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{impl::range_iterator{std::ranges::rend(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{std::ranges::rend(*__value)}};})
        {
            return impl::range_iterator{std::ranges::rend(*__value)};
        }

        /* Get a reverse iterator to one before the first element of the range. */
        [[nodiscard]] constexpr auto crend() const
            noexcept (requires{{impl::range_iterator{std::ranges::crend(*__value)}} noexcept;})
            requires (requires{{impl::range_iterator{std::ranges::crend(*__value)}};})
        {
            return impl::range_iterator{std::ranges::crend(*__value)};
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

        /* Forwarding `size()` operator for the underlying container, provided the
        container supports it. */
        [[nodiscard]] constexpr decltype(auto) size() const
            noexcept (requires{{std::ranges::size(*__value)} noexcept;})
            requires (requires{{std::ranges::size(*__value)};})
        {
            return (std::ranges::size(*__value));
        }

        /* Forwarding `ssize()` operator for the underlying container, provided the
        container supports it. */
        [[nodiscard]] constexpr decltype(auto) ssize() const
            noexcept (requires{{__value->ssize()} noexcept;})
            requires (requires{{__value->ssize()} -> meta::signed_integer;})
        {
            return __value->ssize();
        }

        /* Forwarding `ssize()` operator for the underlying container, provided the
        container supports it. */
        [[nodiscard]] constexpr decltype(auto) ssize() const
            noexcept (requires{{std::ranges::ssize(*__value)} noexcept;})
            requires (
                !requires{{__value->ssize()} -> meta::signed_integer;} &&
                requires{{std::ranges::ssize(*__value)};}
            )
        {
            return (std::ranges::ssize(*__value));
        }

        /* Forwarding `empty()` operator for the underlying container, provided the
        container supports it. */
        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{std::ranges::empty(*__value)} noexcept;})
            requires (requires{{std::ranges::empty(*__value)};})
        {
            return std::ranges::empty(*__value);
        }

        /* Access the first element in the underlying container, assuming the container
        exposes a `.front()` member method.  A debug `AssertionError` may be thrown if
        the container is empty when this method is called. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self)
            noexcept (!DEBUG && requires{{(*std::forward<Self>(self).__value).front()} noexcept;})
            requires (requires{{(*std::forward<Self>(self).__value).front()};})
        {
            if constexpr (DEBUG) {
                if (self.empty()) {
                    throw impl::range_front_error;
                }
            }
            if constexpr (meta::range<C>) {
                return (iter::range((*std::forward<Self>(self).__value).front()));
            } else {
                return ((*std::forward<Self>(self).__value).front());
            }
        }

        /* Access the first element in the underlying container by manually
        dereferencing the `begin()` iterator.  This overload is only chosen if a
        `.front()` member method is unavailable for the underlying container.  A debug
        `AssertionError` may be thrown if the container is empty when this method is
        called. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self)
            noexcept (!DEBUG && requires{{*self.begin()} noexcept;})
            requires (
                !requires{{(*std::forward<Self>(self).__value).front()};} &&
                requires{{*self.begin()};}
            )
        {
            if constexpr (DEBUG) {
                if (self.empty()) {
                    throw impl::range_front_error;
                }
            }
            return (*self.begin());
        }

        /* Access the last element in the underlying container, assuming the container
        exposes a `.back()` member method.  A debug `AssertionError` may be thrown if
        the container is empty when this method is called. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) back(this Self&& self)
            noexcept (!DEBUG && requires{{(*std::forward<Self>(self).__value).back()} noexcept;})
            requires (requires{{(*std::forward<Self>(self).__value).back()};})
        {
            if constexpr (DEBUG) {
                if (self.empty()) {
                    throw impl::range_back_error;
                }
            }
            if constexpr (meta::range<C>) {
                return (iter::range((*std::forward<Self>(self).__value).back()));
            } else {
                return ((*std::forward<Self>(self).__value).back());
            }
        }

        /* Access the last element in the underlying container by manually
        dereferencing the `rbegin()` iterator, assuming one exists.  This overload is
        only chosen if a `.back()` member method does not exist for the underlying
        container.  A debug `AssertionError` may be thrown if the container is empty
        when this method is called. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) back(this Self&& self)
            noexcept (!DEBUG && requires{{*self.rbegin()} noexcept;})
            requires (
                !requires{{(*std::forward<Self>(self).__value).back()};} &&
                requires{{*self.rbegin()};}
            )
        {
            if constexpr (DEBUG) {
                if (self.empty()) {
                    throw impl::range_back_error;
                }
            }
            return (*self.rbegin());
        }

        /* Access the last element in the underlying container by offsetting the
        `begin()` iterator, assuming it supports random access and the underlying
        container is sized.  This overload is only chosen if a `.back()` member method
        does not exist for the underlying container, and the container is not
        reverse-iterable.  A debug `AssertionError` may be thrown if the container is
        empty when this method is called. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) back(this Self&& self)
            noexcept (!DEBUG && requires(decltype(self.begin()) it) {
                {self.begin()} noexcept;
                {it += self.ssize() - 1} noexcept;
                {*it} noexcept;
            })
            requires (
                !requires{{(*std::forward<Self>(self)).back()};} &&
                !requires{{*self.rbegin()};} &&
                requires(decltype(self.begin()) it) {
                    {self.begin()};
                    {it += self.ssize() - 1};
                    {*it};
                }
            )
        {
            if constexpr (DEBUG) {
                if (self.empty()) {
                    throw impl::range_back_error;
                }
            }
            auto it = self.begin();
            it += self.ssize() - 1;
            return (*it);
        }

        /* Access the last element in the underlying container by decrementing the
        `end()` iterator, assuming no other method is available, and the end iterator
        supports bidirectional iteration.  A debug `AssertionError` may be thrown if
        the container is empty when this method is called. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) back(this Self&& self)
            noexcept (!DEBUG && requires(decltype(self.end()) it) {
                {self.end()} noexcept;
                {--it} noexcept;
                {*it} noexcept;
            })
            requires (
                !requires{{(*std::forward<Self>(self)).back()};} &&
                !requires{{*self.rbegin()};} &&
                !requires(decltype(self.begin()) it) {
                    {self.begin()};
                    {it += self.ssize() - 1};
                    {*it};
                } &&
                requires(decltype(self.end()) it) {
                    {self.end()};
                    {--it};
                    {*it};
                }
            )
        {
            if constexpr (DEBUG) {
                if (self.empty()) {
                    throw impl::range_back_error;
                }
            }
            auto it = self.end();
            --it;
            return (*it);
        }

        /* Range-based multidimensional tuple accessor.  This is expression-equivalent
        to `iter::at<K...>{}(self)`.  See that algorithm for more information. */
        template <auto... K, typename Self>
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{at<K...>{}(std::forward<Self>(self))} noexcept;})
            requires (requires{{at<K...>{}(std::forward<Self>(self))};})
        {
            return (at<K...>{}(std::forward<Self>(self)));
        }

        /* Range-based multidimensional indexing operator.  This is
        expression-equivalent to `iter::at{k...}(self)`.  See that algorithm for more
        information. */
        template <typename Self, typename... K>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, K&&... k)
            noexcept (requires{{at{std::forward<K>(k)...}(std::forward<Self>(self))} noexcept;})
            requires (requires{{at{std::forward<K>(k)...}(std::forward<Self>(self))};})
        {
            return (at{std::forward<K>(k)...}(std::forward<Self>(self)));
        }

        /* Range-based membership test.  This is expression-equivalent to
        `iter::contains{k}(self)`.  See that algorithm for more information. */
        template <typename T>
        [[nodiscard]] constexpr bool contains(const T& k) const
            noexcept (requires{{iter::contains{k}(*this)} noexcept;})
            requires (requires{{iter::contains{k}(*this)};})
        {
            return (iter::contains{k}(*this));
        }

        /* Prefer direct conversions for the underlying container if possible.  Note
        that this skips over any intermediate ranges if the type is nested. */
        template <typename Self, typename to> requires (!meta::prefer_constructor<to>)
        [[nodiscard]] constexpr operator to(this Self&& self)
            noexcept (requires{{
                impl::range_value(std::forward<Self>(self))
            } noexcept -> meta::nothrow::convertible_to<to>;})
            requires (impl::range_direct_conversion<Self, to>)
        {
            return to(impl::range_value(std::forward<Self>(self)));
        }

        /* If no direct conversion exists, allow conversion to any type `T` that
        implements a constructor of the form `T(std::from_range, self)`. */
        template <typename Self, typename to> requires (!meta::prefer_constructor<to>)
        [[nodiscard]] constexpr operator to(this Self&& self)
            noexcept (requires{{to(std::from_range, std::forward<Self>(self))} noexcept;})
            requires (
                !impl::range_direct_conversion<Self, to> &&
                impl::range_conversion<Self, to>
            )
        {
            return to(std::from_range, std::forward<Self>(self));
        }

        /* If neither a direct conversion nor a `std::from_range` constructor exist,
        check for a constructor of the form `T(begin(), end())`. */
        template <typename Self, typename to> requires (!meta::prefer_constructor<to>)
        [[nodiscard]] constexpr operator to(this Self&& self)
            noexcept (requires{{to(self.begin(), self.end())} noexcept;})
            requires (
                !impl::range_direct_conversion<Self, to> &&
                !impl::range_conversion<Self, to> &&
                impl::range_traversal<Self, to>
            )
        {
            return to(self.begin(), self.end());
        }

        /* If no direct conversion or constructor exists, and both the range and the
        target type are tuple-like, then a fold expression may be used to directly
        construct (via braced initialization) the target from the elements of the
        range. */
        template <typename Self, meta::tuple_like to> requires (!meta::prefer_constructor<to>)
        [[nodiscard]] constexpr operator to(this Self&& self)
            noexcept (requires{{impl::range_convert_tuple_to_tuple<to>(
                std::forward<Self>(self),
                std::make_index_sequence<meta::tuple_size<to>>{}
            )} noexcept;})
            requires (
                !impl::range_direct_conversion<Self, to> &&
                !impl::range_conversion<Self, to> &&
                !impl::range_traversal<Self, to> &&
                impl::range_tuple_to_tuple<Self, to>
            )
        {
            return impl::range_convert_tuple_to_tuple<to>(
                std::forward<Self>(self),
                std::make_index_sequence<meta::tuple_size<to>>{}
            );
        }

        /* If no direct conversion or constructor exists, and the target type is
        tuple-like but the range is not, then an iterator-based fold expression may be
        used to directly construct (via braced initialization) the target, which
        dereferences the iterator at each step.  If the range size does not exactly
        match the target tuple size, then a `TypeError` will be thrown at run time. */
        template <typename Self, meta::tuple_like to> requires (!meta::prefer_constructor<to>)
        [[nodiscard]] constexpr operator to(this Self&& self)
            requires (
                !impl::range_direct_conversion<Self, to> &&
                !impl::range_conversion<Self, to> &&
                !impl::range_traversal<Self, to> &&
                !impl::range_tuple_to_tuple<Self, to> &&
                impl::range_iter_to_tuple<Self, to>
            )
        {
            return impl::range_convert_iter_to_tuple<to>(
                self,
                std::make_index_sequence<meta::tuple_size<to>>{}
            );
        }
    
        constexpr range& operator=(const range&) = default;
        constexpr range& operator=(range&&) = default;

        /* Prefer direct assignment to the underlying container if possible.  Note that
        this skips over any intermediate ranges if the type is nested. */
        template <typename Self, typename T>
        constexpr Self operator=(this Self&& self, T&& c)
            noexcept (requires{{impl::range_value(self) = std::forward<T>(c)} noexcept;})
            requires (impl::range_direct_assignment<Self, T>)
        {
            impl::range_value(self) = impl::range_value(std::forward<T>(c));
            if constexpr (meta::rvalue<Self>) {
                return std::move(self);
            } else {
                return self;
            }
        }

        /* If no direct assignment exists, and the other operand is not a range, then
        it must be a scalar that can be assigned across the range using `begin()` as
        an output iterator. */
        template <typename Self, typename T> requires (!meta::range<T>)
        constexpr Self operator=(this Self&& self, const T& v)
            noexcept (meta::nothrow::iterable<Self> && requires(meta::yield_type<Self> x) {
                {std::forward<decltype(x)>(x) = v} noexcept;
            })
            requires (
                !impl::range_direct_assignment<Self, T> &&
                impl::range_from_scalar<Self, T>
            )
        {
            for (auto&& x : self) std::forward<decltype(x)>(x) = v;
            if constexpr (meta::rvalue<Self>) {
                return std::move(self);
            } else {
                return self;
            }
        }

        /* If the other operand is a range and direct assignment is not available, then
        fall back to elementwise assignment by iterating over both ranges using
        `begin()` as an output iterator.  If the ranges are not the same size, then a
        `TypeError` will be thrown at run time.  If both ranges are tuple-like, then
        the size check will be performed at compile-time instead, causing this method
        to fail compilation. */
        template <typename Self, meta::range T>
        constexpr Self operator=(this Self&& self, T&& r)
            noexcept (meta::tuple_like<C> && meta::tuple_like<T> && requires(
                decltype(self.begin()) s_it,
                decltype(self.end()) s_end,
                decltype(r.begin()) r_it,
                decltype(r.end()) r_end
            ) {
                {self.begin()} noexcept;
                {self.end()} noexcept;
                {r.begin()} noexcept;
                {r.end()} noexcept;
                {s_it != s_end} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {r_it != r_end} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {*s_it = *r_it} noexcept;
                {++s_it} noexcept;
                {++r_it} noexcept;
            })
            requires (
                !impl::range_direct_assignment<Self, T> &&
                !impl::range_from_scalar<Self, T> &&
                impl::range_iter_from_iter<Self, T>
            )
        {
            static constexpr bool sized = requires{{self.size()};} && requires{{r.size()};};
            if constexpr (sized && (!meta::tuple_like<C> || !meta::tuple_like<T>)) {
                if (self.size() != r.size()) {
                    throw impl::range_assign_size_error(self.size(), r.size());
                }
            }
            auto s_it = self.begin();
            auto s_end = self.end();
            auto r_it = r.begin();
            auto r_end = r.end();
            if constexpr (sized) {
                while (s_it != s_end && r_it != r_end) {
                    *s_it = *r_it;
                    ++s_it;
                    ++r_it;
                }
            } else {
                size_t n = 0;
                while (s_it != s_end && r_it != r_end) {
                    *s_it = *r_it;
                    ++s_it;
                    ++r_it;
                    ++n;
                }
                if (s_it != s_end) {
                    throw impl::range_assign_iter_too_small(n);
                }
                if (r_it != r_end) {
                    throw impl::range_assign_iter_too_big(n);
                }
            }
            if constexpr (meta::rvalue<Self>) {
                return std::move(self);
            } else {
                return self;
            }
        }

        /* If the other operand is a range and neither direct assignment nor
        elementwise assignment using iterators is available, then check for
        tuple-based elementwise assignment using a fold expression.  This will only
        be chosen if both ranges are tuple-like, and their sizes match exactly. */
        template <typename Self, meta::range T>
        constexpr Self operator=(this Self&& self, T&& r)
            noexcept (requires{{impl::range_assign_tuple_from_tuple(
                self,
                std::forward<T>(r),
                std::make_index_sequence<meta::tuple_size<C>>{}
            )} noexcept;})
            requires (
                !impl::range_direct_assignment<Self, T> &&
                !impl::range_from_scalar<Self, T> &&
                !impl::range_iter_from_iter<Self, T> &&
                impl::range_tuple_from_tuple<Self, T>
            )
        {
            impl::range_assign_tuple_from_tuple(
                self,
                std::forward<T>(r),
                std::make_index_sequence<meta::tuple_size<C>>{}
            );
            if constexpr (meta::rvalue<Self>) {
                return std::move(self);
            } else {
                return self;
            }
        }

        /* If the other operand is a tuple-like range but this range is not, and
        elementwise assignment using their iterators is not possible, then check for an
        algorithm that uses this range's `begin()` iterator as an output iterator and
        assigns elements from the other range using tuple indexing.  If the ranges are
        not the same size, then a `TypeError` will be thrown. */
        template <typename Self, meta::range T>
        constexpr Self operator=(this Self&& self, T&& r)
            noexcept (requires{{impl::range_assign_iter_from_tuple(
                self,
                std::forward<T>(r),
                std::make_index_sequence<meta::tuple_size<C>>{}
            )} noexcept;})
            requires (
                !impl::range_direct_assignment<Self, T> &&
                !impl::range_from_scalar<Self, T> &&
                !impl::range_iter_from_iter<Self, T> &&
                !impl::range_tuple_from_tuple<Self, T> &&
                impl::range_iter_from_tuple<Self, T>
            )
        {
            impl::range_assign_iter_from_tuple(
                self,
                std::forward<T>(r),
                std::make_index_sequence<meta::tuple_size<T>>{}
            );
            if constexpr (meta::rvalue<Self>) {
                return std::move(self);
            } else {
                return self;
            }
        }

        /* If this range is tuple-like but the other operand is not, and elementwise
        assignment using their iterators is not possible, then check for an algorithm
        that uses the other range's `begin()` iterator as an input iterator and assigns
        each element to this range using tuple indexing.  If the ranges are not the
        same size, then a `TypeError` will be thrown. */
        template <typename Self, meta::range T>
        constexpr Self operator=(this Self&& self, T&& r)
            noexcept (requires{{impl::range_assign_tuple_from_iter(
                self,
                std::forward<T>(r),
                std::make_index_sequence<meta::tuple_size<C>>{}
            )} noexcept;})
            requires (
                !impl::range_direct_assignment<Self, T> &&
                !impl::range_from_scalar<Self, T> &&
                !impl::range_iter_from_iter<Self, T> &&
                !impl::range_tuple_from_tuple<Self, T> &&
                !impl::range_iter_from_tuple<Self, T> &&
                impl::range_tuple_from_iter<Self, T>
            )
        {
            impl::range_assign_tuple_from_iter(
                self,
                std::forward<T>(r),
                std::make_index_sequence<meta::tuple_size<C>>{}
            );
            if constexpr (meta::rvalue<Self>) {
                return std::move(self);
            } else {
                return self;
            }
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

    /* A trivial subclass of `range` that allows the range to be destructured when
    used as an argument to a Bertrand function. */
    template <meta::not_rvalue C>
    struct unpack : iter::range<C> {
        using iter::range<C>::range;

        /// TODO: include nested range promotion for front(), back(), and the indexing
        /// operators.  Also, if I drop the `*` operator from `range` and reduce it
        /// to just a container accessor, then I can decouple ranges somewhat from
        /// unpacking types, which would reduce the number of circular references.

        /* Get a forward iterator to the start of the range. */
        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{impl::unpack_iterator{std::ranges::begin(**this)}} noexcept;})
            requires (requires{{impl::unpack_iterator{std::ranges::begin(**this)}};})
        {
            return impl::unpack_iterator{std::ranges::begin(**this)};
        }

        /* Get a forward iterator to the start of the range. */
        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{impl::unpack_iterator{std::ranges::begin(**this)}} noexcept;})
            requires (requires{{impl::unpack_iterator{std::ranges::begin(**this)}};})
        {
            return impl::unpack_iterator{std::ranges::begin(**this)};
        }

        /* Get a forward iterator to the start of the range. */
        [[nodiscard]] constexpr auto cbegin() const
            noexcept (requires{{impl::unpack_iterator{std::ranges::cbegin(**this)}} noexcept;})
            requires (requires{{impl::unpack_iterator{std::ranges::cbegin(**this)}};})
        {
            return impl::unpack_iterator{std::ranges::cbegin(**this)};
        }

        /* Get a forward iterator to one past the last element of the range. */
        [[nodiscard]] constexpr auto end()
            noexcept (requires{{impl::unpack_iterator{std::ranges::end(**this)}} noexcept;})
            requires (requires{{impl::unpack_iterator{std::ranges::end(**this)}};})
        {
            return impl::unpack_iterator{std::ranges::end(**this)};
        }

        /* Get a forward iterator to one past the last element of the range. */
        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{impl::unpack_iterator{std::ranges::end(**this)}} noexcept;})
            requires (requires{{impl::unpack_iterator{std::ranges::end(**this)}};})
        {
            return impl::unpack_iterator{std::ranges::end(**this)};
        }

        /* Get a forward iterator to one past the last element of the range. */
        [[nodiscard]] constexpr auto cend() const
            noexcept (requires{{impl::unpack_iterator{std::ranges::cend(**this)}} noexcept;})
            requires (requires{{impl::unpack_iterator{std::ranges::cend(**this)}};})
        {
            return impl::unpack_iterator{std::ranges::cend(**this)};
        }

        /* Get a reverse iterator to the last element of the range. */
        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{impl::unpack_iterator{std::ranges::rbegin(**this)}} noexcept;})
            requires (requires{{impl::unpack_iterator{std::ranges::rbegin(**this)}};})
        {
            return impl::unpack_iterator{std::ranges::rbegin(**this)};
        }

        /* Get a reverse iterator to the last element of the range. */
        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{impl::unpack_iterator{std::ranges::rbegin(**this)}} noexcept;})
            requires (requires{{impl::unpack_iterator{std::ranges::rbegin(**this)}};})
        {
            return impl::unpack_iterator{std::ranges::rbegin(**this)};
        }

        /* Get a reverse iterator to the last element of the range. */
        [[nodiscard]] constexpr auto crbegin() const
            noexcept (requires{{impl::unpack_iterator{std::ranges::crbegin(**this)}} noexcept;})
            requires (requires{{impl::unpack_iterator{std::ranges::crbegin(**this)}};})
        {
            return impl::unpack_iterator{std::ranges::crbegin(**this)};
        }

        /* Get a reverse iterator to one before the first element of the range. */
        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{impl::unpack_iterator{std::ranges::rend(**this)}} noexcept;})
            requires (requires{{impl::unpack_iterator{std::ranges::rend(**this)}};})
        {
            return impl::unpack_iterator{std::ranges::rend(**this)};
        }

        /* Get a reverse iterator to one before the first element of the range. */
        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{impl::unpack_iterator{std::ranges::rend(**this)}} noexcept;})
            requires (requires{{impl::unpack_iterator{std::ranges::rend(**this)}};})
        {
            return impl::unpack_iterator{std::ranges::rend(**this)};
        }

        /* Get a reverse iterator to one before the first element of the range. */
        [[nodiscard]] constexpr auto crend() const
            noexcept (requires{{impl::unpack_iterator{std::ranges::crend(**this)}} noexcept;})
            requires (requires{{impl::unpack_iterator{std::ranges::crend(**this)}};})
        {
            return impl::unpack_iterator{std::ranges::crend(**this)};
        }
    };

}





/// TODO: since sequences now encode whether they are sized at compile time, there's
/// no need for `has_size()` or any related behavior.

/// TODO: maybe sequences should also allow a generic `contains()` method as well,
/// which equates to `iter::contains{v}(self)`?


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

        template <typename Start, typename Stop, typename Step>
        constexpr bool enable_borrowed_range<bertrand::impl::subrange<Start, Stop, Step>> = true;

        template <typename C>
        constexpr bool enable_borrowed_range<bertrand::iter::range<C>> =
            std::ranges::borrowed_range<C>;

        template <typename C>
        constexpr bool enable_borrowed_range<bertrand::iter::unpack<C>> =
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
        using type = decltype((bertrand::meta::get<I>(std::declval<T>())));
    };

    template <size_t I, bertrand::meta::tuple_like T> requires (I < bertrand::meta::tuple_size<T>)
    struct tuple_element<I, bertrand::iter::range<T>> {
        using type = decltype((bertrand::meta::get<I>(std::declval<T>())));
    };

    template <ssize_t I, bertrand::meta::range R>
        requires (
            bertrand::meta::tuple_like<R> &&
            bertrand::impl::valid_index<bertrand::meta::tuple_size<R>, I>
        )
    constexpr decltype(auto) get(R&& r)
        noexcept (requires{{bertrand::meta::get<I>(std::forward<R>(r))} noexcept;})
        requires (requires{{bertrand::meta::get<I>(std::forward<R>(r))};})
    {
        return (bertrand::meta::get<I>(std::forward<R>(r)));
    }

}


namespace bertrand::iter {

    // static_assert([] {
    //     auto r = (range(range(std::array{1, 2, 3})) = std::array{4, 5, 6});
    //     if (r[0].front() != 4) return false;

    //     // auto r2 = range(5);
    //     // auto r3 = *r2;

    //     return true;
    // }());


    // static_assert(range(std::array{1, 2, 3}).contains(2));

    // static_assert(range(range(std::array{1, 2, 3}))->size() == 3);


    // static constexpr int x = 2;
    // static constexpr impl::ref r {x};


    // static_assert([] {
    //     impl::ref f {[](int i) { return i * 2; }};
    //     const impl::ref g = f;
    //     f = g;

    //     // impl::ref f {x};
    //     // const impl::ref g = f;
    //     // f = g;

    //     return true;
    // }());


    // static_assert([] {
    //     std::vector vec {1, 2, 3};
    //     std::tuple<int, int, int> tup = range(1, 4);
    //     auto [a, b, c] = tup;
    //     if (a != 1 || b != 2 || c != 3) {
    //         return false;
    //     }

    //     return true;
    // }());


    // static constexpr auto r10 = range<std::tuple<int, int, double>>(1, 2, 3);

    // static constexpr auto r11 = range(0, 5);

    // static_assert([] {
    //     for (auto&& x : r11) {
    //         (void)x;
    //     }

    //     return true;
    // }());


}


#endif  // BERTRAND_ITER_RANGE_H