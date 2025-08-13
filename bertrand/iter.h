#ifndef BERTRAND_ITER_H
#define BERTRAND_ITER_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/union.h"


/// TODO: split this file into multiple smaller files in an iter/ directory.
/// iter.h will just include all of them in the proper order.


namespace bertrand {


template <meta::not_void... Ts>
struct Tuple;


template <meta::not_void... Ts>
Tuple(Ts&&...) -> Tuple<meta::remove_rvalue<Ts>...>;



/// TODO: generating a full tuple specialization could easily exceed the recursion
/// limit.  There should be a better way to do this that allows for gigantic arrays
/// without a huge instantiation cost.


/* A trivial subclass of `Tuple` that consists of `N` repretitions of a homogenous
type.

Note that `bertrand::Tuple` specializations will optimize to arrays internally as long
as they contain only a single type.  Because this class guarantees that condition is
always met, it will reliably trigger the optimization, and give the same behavior as a
typical bounded array in addition to all the monadic properties of tuples, including
the ability to use Python-style indexing, store references, build expression templates,
and participate in pattern matching. */
template <meta::not_void T, size_t N>
struct Array : meta::repeat<N, T>::template eval<Tuple> {};


template <meta::not_void T, meta::is<T>... Ts>
Array(T&&, Ts&&...) -> Array<
    meta::common_type<meta::remove_rvalue<T>, meta::remove_rvalue<Ts>...>,
    sizeof...(Ts) + 1
>;


namespace impl {
    struct range_tag {};
    struct iota_tag {};
    struct sequence_tag {};

    template <typename T>
    concept strictly_positive =
        meta::unsigned_integer<T> ||
        !requires(meta::as_const_ref<T> t) {{t < 0} -> meta::explicitly_convertible_to<bool>;};

    template <typename Start, typename Stop, typename Step>
    concept base_iota_concept =
        meta::unqualified<Start> &&
        meta::unqualified<Stop> &&
        meta::copyable<Start> &&
        meta::copyable<Stop> &&
        (meta::is_void<Step> || (meta::unqualified<Step> && meta::copyable<Step>));

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
        base_iota_concept<Start, Stop, Step> &&
        (iota_bounded<Start, Stop, Step> || meta::None<Stop>) &&
        iota_incrementable<Start, Stop, Step>;

    template <typename Start, typename Stop, typename Step> requires (iota_concept<Start, Stop, Step>)
    struct iota;

}


template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
struct range;


template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
range(C&&) -> range<meta::remove_rvalue<C>>;


template <typename Stop>
    requires (
        !meta::iterable<Stop> &&
        !meta::tuple_like<Stop> &&
        impl::iota_concept<Stop, Stop, void>
    )
range(Stop) -> range<impl::iota<Stop, Stop, void>>;


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


template <meta::not_void Start, meta::not_void Stop, meta::not_void Step>
struct slice;


namespace impl {

    /* A trivial subclass of `range` that allows the range to be destructured when
    used as an argument to a Bertrand function. */
    template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
    struct unpack : range<C> {
        [[nodiscard]] explicit constexpr unpack(meta::forward<C> c)
            noexcept (meta::nothrow::constructible_from<range<C>, meta::forward<C>>)
            requires (meta::constructible_from<range<C>, meta::forward<C>>)
        :
            range<C>(std::forward<C>(c))
        {}
    };

    template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
    unpack(C&&) -> unpack<meta::remove_rvalue<C>>;

    template <typename T>
    concept enable_tuple_iterator = meta::lvalue<T> && meta::tuple_like<T>;

    /* Tuple iterators can be optimized away if the tuple is empty, or into an array of
    pointers if all elements unpack to the same lvalue type.  Otherwise, they must
    build a vtable and perform a dynamic dispatch to yield a proper value type, which
    may be a union. */
    enum class tuple_iterator_kind : uint8_t {
        DYNAMIC,
        ARRAY,
        EMPTY,
    };

    /* Indexing and/or iterating over a tuple requires the creation of some kind of
    array, which can either be a flat array of homogenous references or a vtable of
    function pointers that produce a common type (which may be a `Union`) to which all
    results are convertible. */
    template <typename>
    struct _tuple_iterator_traits;
    template <typename... Ts>
    struct _tuple_iterator_traits<meta::pack<Ts...>> {
        using reference = meta::union_type<Ts...>;
        static constexpr tuple_iterator_kind kind = meta::is_void<reference> ?
            tuple_iterator_kind::EMPTY :
            meta::visitable<reference> ?
                tuple_iterator_kind::DYNAMIC :
                tuple_iterator_kind::ARRAY;
        static constexpr bool nothrow = (meta::nothrow::convertible_to<Ts, reference> && ...);
    };
    template <enable_tuple_iterator T>
    struct tuple_iterator_traits : _tuple_iterator_traits<typename meta::tuple_types<T>> {
    private:
        using base = _tuple_iterator_traits<typename meta::tuple_types<T>>;

        template <size_t I>
        struct fn {
            static constexpr base::reference operator()(T t) noexcept (base::nothrow) {
                return meta::unpack_tuple<I>(t);
            }
        };

    public:
        using dispatch = impl::vtable<fn>::template dispatch<
            std::make_index_sequence<meta::tuple_size<T>>
        >;
    };

    /* A special case of `tuple_iterator` for empty tuples, which do not yield any
    results, and are optimized away by the compiler. */
    template <enable_tuple_iterator T>
    struct tuple_iterator {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const NoneType;
        using pointer = const NoneType*;
        using reference = const NoneType&;

        [[nodiscard]] constexpr tuple_iterator(difference_type = 0) noexcept {}
        [[nodiscard]] constexpr tuple_iterator(T, difference_type = 0) noexcept {}

        [[nodiscard]] constexpr reference operator*() const noexcept {
            return None;
        }

        [[nodiscard]] constexpr pointer operator->() const noexcept {
            return &None;
        }

        [[nodiscard]] constexpr reference operator[](difference_type) const noexcept {
            return None;
        }

        constexpr tuple_iterator& operator++() noexcept {
            return *this;
        }

        [[nodiscard]] constexpr tuple_iterator operator++(int) noexcept {
            return *this;
        }

        [[nodiscard]] friend constexpr tuple_iterator operator+(
            const tuple_iterator& self,
            difference_type
        ) noexcept {
            return self;
        }

        [[nodiscard]] friend constexpr tuple_iterator operator+(
            difference_type,
            const tuple_iterator& self
        ) noexcept {
            return self;
        }

        constexpr tuple_iterator& operator+=(difference_type) noexcept {
            return *this;
        }

        constexpr tuple_iterator& operator--() noexcept {
            return *this;
        }

        [[nodiscard]] constexpr tuple_iterator operator--(int) noexcept {
            return *this;
        }

        [[nodiscard]] constexpr tuple_iterator operator-(difference_type) const noexcept {
            return *this;
        }

        [[nodiscard]] constexpr difference_type operator-(const tuple_iterator&) const noexcept {
            return 0;
        }

        constexpr tuple_iterator& operator-=(difference_type) noexcept {
            return *this;
        }

        [[nodiscard]] constexpr auto operator<=>(const tuple_iterator&) const noexcept {
            return std::strong_ordering::equal;
        }

        [[nodiscard]] constexpr bool operator==(const tuple_iterator&) const noexcept {
            return true;
        }
    };

    /* A special case of `tuple_iterator` for tuples where all elements share the
    same addressable type.  In this case, the vtable is reduced to a simple array of
    pointers that are initialized on construction, without requiring dynamic
    dispatch. */
    template <enable_tuple_iterator T>
        requires (tuple_iterator_traits<T>::kind == tuple_iterator_kind::ARRAY)
    struct tuple_iterator<T> {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using reference = tuple_iterator_traits<T>::reference;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::address_type<reference>;

    private:
        using indices = std::make_index_sequence<meta::tuple_size<T>>;
        using store = impl::ref<reference>;
        using array = std::array<store, meta::tuple_size<T>>;

        array data;
        difference_type index;

        template <size_t... Is>
        static constexpr array init(std::index_sequence<Is...>, T t)
            noexcept ((requires{
                {meta::unpack_tuple<Is>(t)} noexcept -> meta::nothrow::convertible_to<store>;
            } && ...))
        {
            return {meta::unpack_tuple<Is>(t)...};
        }

        [[nodiscard]] constexpr tuple_iterator(const array& data, difference_type index) noexcept :
            data(data),
            index(index)
        {}

    public:
        [[nodiscard]] constexpr tuple_iterator(difference_type index = meta::tuple_size<T>) noexcept :
            data{},
            index(index)
        {}

        [[nodiscard]] constexpr tuple_iterator(T t, difference_type index = 0)
            noexcept (requires{{init(indices{}, t)} noexcept;})
        :
            data(init(indices{}, t)),
            index(index)
        {}

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept {
            return (*self.data[self.index]);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self) noexcept {
            return impl::arrow_proxy{*std::forward<Self>(self)};
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](
            this Self&& self,
            difference_type n
        ) noexcept {
            return (*self.data[self.index + n]);
        }

        constexpr tuple_iterator& operator++() noexcept {
            ++index;
            return *this;
        }

        [[nodiscard]] constexpr tuple_iterator operator++(int) noexcept {
            auto tmp = *this;
            ++index;
            return tmp;
        }

        [[nodiscard]] friend constexpr tuple_iterator operator+(
            const tuple_iterator& self,
            difference_type n
        ) noexcept {
            return {self.data, self.index + n};
        }

        [[nodiscard]] friend constexpr tuple_iterator operator+(
            difference_type n,
            const tuple_iterator& self
        ) noexcept {
            return {self.data, self.index + n};
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
            auto tmp = *this;
            --index;
            return tmp;
        }

        [[nodiscard]] constexpr tuple_iterator operator-(difference_type n) const noexcept {
            return {data, index - n};
        }

        [[nodiscard]] constexpr difference_type operator-(const tuple_iterator& rhs) const noexcept {
            return index - rhs.index;
        }

        constexpr tuple_iterator& operator-=(difference_type n) noexcept {
            index -= n;
            return *this;
        }

        [[nodiscard]] constexpr bool operator==(const tuple_iterator& other) const noexcept {
            return index == other.index;
        }

        [[nodiscard]] constexpr auto operator<=>(const tuple_iterator& other) const noexcept {
            return index <=> other.index;
        }
    };

    /* An iterator over an otherwise non-iterable tuple type, which constructs a vtable
    of callback functions yielding each value.  This allows tuples to be used as inputs
    to iterable algorithms, as long as those algorithms are built to handle possible
    `Union` values. */
    template <enable_tuple_iterator T>
        requires (tuple_iterator_traits<T>::kind == tuple_iterator_kind::DYNAMIC)
    struct tuple_iterator<T> {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using reference = tuple_iterator_traits<T>::reference;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<value_type>;

    private:
        using table = tuple_iterator_traits<T>;
        using indices = std::make_index_sequence<meta::tuple_size<T>>;
        using storage = meta::as_pointer<T>;

        storage data;
        difference_type index;

        [[nodiscard]] constexpr tuple_iterator(storage data, difference_type index) noexcept :
            data(data),
            index(index)
        {}

    public:
        [[nodiscard]] constexpr tuple_iterator(difference_type index = meta::tuple_size<T>) noexcept :
            data(nullptr),
            index(index)
        {}

        [[nodiscard]] constexpr tuple_iterator(T tuple, difference_type index = 0)
            noexcept (meta::nothrow::address_returns<storage, T>)
            requires (meta::address_returns<storage, T>)
        :
            data(std::addressof(tuple)),
            index(index)
        {}

        [[nodiscard]] constexpr reference operator*() const noexcept (table::nothrow) {
            return typename table::dispatch{size_t(index)}(*data);
        }

        [[nodiscard]] constexpr auto operator->() const noexcept (table::nothrow) {
            return impl::arrow_proxy(**this);
        }

        [[nodiscard]] constexpr reference operator[](
            difference_type n
        ) const noexcept (table::nothrow) {
            return typename table::dispatch{index + n}(*data);
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
            return {self.data, self.index + n};
        }

        [[nodiscard]] friend constexpr tuple_iterator operator+(
            difference_type n,
            const tuple_iterator& self
        ) noexcept {
            return {self.data, self.index + n};
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
            return {data, index - n};
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

    template <typename C>
    struct make_range_begin { using type = void; };
    template <meta::iterable C>
    struct make_range_begin<C> { using type = meta::begin_type<C>; };

    template <typename C>
    struct make_range_end { using type = void; };
    template <meta::iterable C>
    struct make_range_end<C> { using type = meta::end_type<C>; };

    template <typename C>
    struct make_range_rbegin { using type = void; };
    template <meta::reverse_iterable C>
    struct make_range_rbegin<C> { using type = meta::rbegin_type<C>; };

    template <typename C>
    struct make_range_rend { using type = void; };
    template <meta::reverse_iterable C>
    struct make_range_rend<C> { using type = meta::rend_type<C>; };

    /* `make_range_iterator` abstracts the forward iterator methods for a `range`,
    synthesizing a corresponding tuple iterator if the underlying container is not
    already iterable. */
    template <meta::lvalue C>
    struct make_range_iterator {
        static constexpr bool tuple = true;
        using begin_type = tuple_iterator<C>;
        using end_type = begin_type;

        C container;

        [[nodiscard]] constexpr begin_type begin()
            noexcept (requires{{begin_type{container}} noexcept;})
        {
            return begin_type(container);
        }

        [[nodiscard]] constexpr end_type end()
            noexcept (requires{{end_type{}} noexcept;})
        {
            return end_type{};
        }
    };
    template <meta::lvalue C> requires (meta::iterable<C>)
    struct make_range_iterator<C> {
        static constexpr bool tuple = false;
        using begin_type = make_range_begin<C>::type;
        using end_type = make_range_end<C>::type;

        C container;

        [[nodiscard]] constexpr begin_type begin()
            noexcept (meta::nothrow::has_begin<C>)
            requires (meta::has_begin<C>)
        {
            return std::ranges::begin(container);
        }

        [[nodiscard]] constexpr end_type end()
            noexcept (meta::nothrow::has_end<C>)
            requires (meta::has_end<C>)
        {
            return std::ranges::end(container);
        }
    };

    template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
    make_range_iterator(C&) -> make_range_iterator<C&>;

    template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
    using range_begin = make_range_iterator<meta::as_lvalue<C>>::begin_type;

    template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
    using range_end = make_range_iterator<meta::as_lvalue<C>>::end_type;

    /* `make_range_reversed` abstracts the reverse iterator methods for a `range`,
    synthesizing a corresponding tuple iterator if the underlying container is not
    already iterable. */
    template <meta::lvalue C>
    struct make_range_reversed {
        static constexpr bool tuple = true;
        using begin_type = std::reverse_iterator<tuple_iterator<C>>;
        using end_type = begin_type;

        C container;

        [[nodiscard]] constexpr begin_type begin()
            noexcept (requires{{begin_type{begin_type{container, meta::tuple_size<C>}}} noexcept;})
        {
            return begin_type{tuple_iterator<C>{container, meta::tuple_size<C>}};
        }

        [[nodiscard]] constexpr end_type end()
            noexcept (requires{{end_type{begin_type{size_t(0)}}} noexcept;})
        {
            return end_type{begin_type{size_t(0)}};
        }
    };
    template <meta::lvalue C> requires (meta::reverse_iterable<C>)
    struct make_range_reversed<C> {
        static constexpr bool tuple = false;
        using begin_type = make_range_rbegin<C>::type;
        using end_type = make_range_rend<C>::type;

        C container;

        [[nodiscard]] constexpr begin_type begin()
            noexcept (meta::nothrow::has_rbegin<C>)
            requires (meta::has_rbegin<C>)
        {
            return std::ranges::rbegin(container);
        }

        [[nodiscard]] constexpr end_type end()
            noexcept (meta::nothrow::has_rend<C>)
            requires (meta::has_rend<C>)
        {
            return std::ranges::rend(container);
        }
    };

    template <typename C> requires (meta::reverse_iterable<C> || meta::tuple_like<C>)
    make_range_reversed(C&) -> make_range_reversed<C&>;

    template <typename C> requires (meta::reverse_iterable<C> || meta::tuple_like<C>)
    using range_rbegin = make_range_reversed<meta::as_lvalue<C>>::begin_type;

    template <typename C> requires (meta::reverse_iterable<C> || meta::tuple_like<C>)
    using range_rend = make_range_reversed<meta::as_lvalue<C>>::end_type;

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

        template <typename>
        constexpr bool slice = false;
        template <typename... Ts>
        constexpr bool slice<bertrand::slice<Ts...>> = true;

    }

    /// TODO: this idea is actually fantastic, and can be scaled to all other types
    /// as well.  The idea is that for every class, you would have a meta:: concept
    /// that takes the class as the first template argument, and then optionally
    /// takes any number of additional template arguments, which would mirror the
    /// exact signature of the class.

    template <typename T, typename R = void>
    concept range = inherits<T, impl::range_tag> && (is_void<R> || yields<T, R>);

    template <typename T, typename R = void>
    concept sequence = range<T, R> && inherits<T, impl::sequence_tag>;

    template <typename T, typename R = void>
    concept unpack = range<T, R> && detail::unpack<unqualify<T>>;

    template <typename T, typename... Rs>
    concept unpack_to = detail::unpack<unqualify<T>> && (
        (tuple_like<T> && tuple_size<T> == sizeof...(Rs) && detail::unpack_convert<T, Rs...>) ||
        (!tuple_like<T> && ... && yields<T, Rs>)
    );

    template <typename T>
    concept slice = detail::slice<unqualify<T>>;

    namespace detail {

        template <meta::range T>
        constexpr bool prefer_constructor<T> = true;

        /// TODO: exact_size is probably not required?
        template <meta::range T>
        constexpr bool exact_size<T> = meta::exact_size<typename T::__type>;

    }

}


/////////////////////
////    RANGE    ////
/////////////////////


namespace impl {

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
    struct iota : impl::iota_tag {
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
    struct iota<Start, Stop, void> : impl::iota_tag {
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

    template <typename C>
    constexpr decltype(auto) range_subscript(C&& container, size_t i)
        noexcept (requires{{std::forward<C>(container)[i]} noexcept;} || meta::tuple_like<C>)
        requires (requires{{std::forward<C>(container)[i]};} || meta::tuple_like<C>)
    {
        if constexpr (requires{{std::forward<C>(container)[i]};}) {
            return (std::forward<C>(container)[i]);
        } else {
            return typename impl::tuple_iterator_traits<meta::forward<C>>::dispatch{i}(
                std::forward<C>(container)
            );
        }
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
template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
struct range : impl::range_tag {
    using __type = meta::remove_rvalue<C>;

    [[no_unique_address]] impl::ref<__type> __value;

    /* Forwarding constructor for the underlying container. */
    [[nodiscard]] explicit constexpr range(meta::forward<C> c)
        noexcept (meta::nothrow::constructible_from<__type, meta::forward<C>>)
        requires (meta::constructible_from<__type, meta::forward<C>>)
    :
        __value(std::forward<C>(c))
    {}

    /* CTAD constructor for 1-argument iota ranges. */
    template <typename Stop>
        requires (
            !meta::constructible_from<__type, meta::forward<C>> &&
            impl::iota_concept<Stop, Stop, void>
        )
    [[nodiscard]] explicit constexpr range(Stop stop)
        noexcept (meta::nothrow::constructible_from<__type, Stop, Stop>)
        requires (meta::constructible_from<__type, Stop, Stop>)
    :
        __value(Stop(0), stop)
    {}

    /* CTAD constructor for iterator pair subranges. */
    template <meta::iterator Begin, meta::sentinel_for<Begin> End>
    [[nodiscard]] explicit constexpr range(Begin&& begin, End&& end)
        noexcept (meta::nothrow::constructible_from<__type, Begin, End>)
        requires (meta::constructible_from<__type, Begin, End>)
    :
        __value(std::forward<Begin>(begin), std::forward<End>(end))
    {}

    /* CTAD constructor for 2-argument iota ranges. */
    template <typename Start, typename Stop>
        requires (
            (!meta::iterator<Start> || !meta::sentinel_for<Stop, Start>) &&
            impl::iota_concept<Start, Stop, void>
        )
    [[nodiscard]] explicit constexpr range(Start start, Stop stop)
        noexcept (meta::nothrow::constructible_from<__type, Start, Stop>)
        requires (meta::constructible_from<__type, Start, Stop>)
    :
        __value(start, stop)
    {}

    /* CTAD constructor for counted ranges, which consist of a begin iterator and an
    integer size. */
    template <meta::iterator Begin, typename Count>
        requires (
            !meta::sentinel_for<Begin, Count> &&
            !impl::iota_concept<Begin, Count, void> &&
            meta::unsigned_integer<Count>
        )
    [[nodiscard]] constexpr range(Begin begin, Count size)
        noexcept (meta::nothrow::constructible_from<
            __type,
            std::counted_iterator<Begin>,
            const std::default_sentinel_t&
        >)
        requires (meta::constructible_from<
            __type,
            std::counted_iterator<Begin>,
            const std::default_sentinel_t&
        >)
    :
        __value(
            std::counted_iterator(std::move(begin), std::forward<Count>(size)),
            std::default_sentinel
        )
    {}

    /* CTAD constructor for 3-argument iota ranges. */
    template <typename Start, typename Stop, typename Step>
        requires (impl::iota_concept<Start, Stop, Step>)
    [[nodiscard]] explicit constexpr range(Start start, Stop stop, Step step)
        noexcept (meta::nothrow::constructible_from<__type, Start, Stop, Step>)
        requires (meta::constructible_from<__type, Start, Stop, Step>)
    :
        __value(start, stop, step)
    {}

    [[nodiscard]] constexpr range(const range&) = default;
    [[nodiscard]] constexpr range(range&&) = default;
    constexpr range& operator=(const range&) = default;
    constexpr range& operator=(range&&) = default;

    /* `swap()` operator between ranges. */
    constexpr void swap(range& other)
        noexcept (meta::nothrow::swappable<__type>)
        requires (meta::swappable<__type>)
    {
        std::ranges::swap(*__value, *other.__value);
    }

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


    /// TODO: `get<slice>()` and `get<*mask>()` can allow indexing into a range at
    /// compile time using the same semantics as `operator[]`.

    /* Forwarding `get<I>()` accessor, provided the underlying container is
    tuple-like.  Automatically applies Python-style wraparound for negative indices. */
    template <ssize_t I, typename Self>
    constexpr decltype(auto) get(this Self&& self)
        noexcept (requires{{meta::unpack_tuple<I>(*std::forward<Self>(self).__value)} noexcept;})
        requires (requires{{meta::unpack_tuple<I>(*std::forward<Self>(self).__value)};})
    {
        return (meta::unpack_tuple<I>(*std::forward<Self>(self).__value));
    }

    /* Integer indexing operator.  Accepts a single signed integer and retrieves the
    corresponding element from the underlying container after applying Python-style
    wraparound for negative indices.  If the container does not support indexing, but
    is otherwise tuple-like, then a vtable will be synthesized to back this
    operator. */
    template <typename Self>
    constexpr decltype(auto) operator[](this Self&& self, ssize_t i)
        noexcept (requires{{impl::range_subscript(
            *std::forward<Self>(self).__value,
            size_t(impl::normalize_index(self.ssize(), i))
        )} noexcept;})
        requires (requires{{impl::range_subscript(
            *std::forward<Self>(self).__value,
            size_t(impl::normalize_index(self.ssize(), i))
        )};})
    {
        return (impl::range_subscript(
            *std::forward<Self>(self).__value,
            size_t(impl::normalize_index(self.ssize(), i))
        ));
    }

    /* Slice operator, which returns a subset of the range according to a Python-style
    `slice` expression. */
    template <typename Self, meta::slice S>
    [[nodiscard]] constexpr auto operator[](this Self&& self, S&& slice)
        noexcept (requires{{std::forward<S>(slice).range(std::forward<Self>(self))} noexcept;})
        requires (requires{{std::forward<S>(slice).range(std::forward<Self>(self))};})
    {
        return std::forward<S>(slice).range(std::forward<Self>(self));
    }

    /// TODO: The actual implementation for boolean masks is tricky, and has not yet
    /// been implemented.  It should return another expression, similar to slicing.

    /* Mask operator, which returns a subset of the range corresponding to the `true`
    values of boolean range.  The length of the resulting range is given by the number
    of true values in the mask or the size of this range, whichever is smaller. */
    template <typename Self, meta::range<bool> M>
    [[nodiscard]] constexpr auto operator[](this Self&& self, M&& mask)
        noexcept (requires{{std::forward<M>(mask)(std::forward<Self>(self))} noexcept;})
        requires (requires{{std::forward<M>(mask)(std::forward<Self>(self))};})
    {
        return std::forward<M>(mask)(std::forward<Self>(self));
    }

    /* Get a forward iterator to the start of the range. */
    [[nodiscard]] constexpr decltype(auto) begin()
        noexcept (requires{{impl::make_range_iterator{*__value}.begin()} noexcept;})
        requires (requires{{impl::make_range_iterator{*__value}.begin()};})
    {
        return (impl::make_range_iterator{*__value}.begin());
    }

    /* Get a forward iterator to the start of the range. */
    [[nodiscard]] constexpr decltype(auto) begin() const
        noexcept (requires{{impl::make_range_iterator{*__value}.begin()} noexcept;})
        requires (requires{{impl::make_range_iterator{*__value}.begin()};})
    {
        return (impl::make_range_iterator{*__value}.begin());
    }

    /* Get a forward iterator to the start of the range. */
    [[nodiscard]] constexpr decltype(auto) cbegin() const
        noexcept (requires{{impl::make_range_iterator{*__value}.begin()} noexcept;})
        requires (requires{{impl::make_range_iterator{*__value}.begin()};})
    {
        return (impl::make_range_iterator{*__value}.begin());
    }

    /* Get a forward iterator to one past the last element of the range. */
    [[nodiscard]] constexpr decltype(auto) end()
        noexcept (requires{{impl::make_range_iterator{*__value}.end()} noexcept;})
        requires (requires{{impl::make_range_iterator{*__value}.end()};})
    {
        return (impl::make_range_iterator{*__value}.end());
    }

    /* Get a forward iterator to one past the last element of the range. */
    [[nodiscard]] constexpr decltype(auto) end() const
        noexcept (requires{{impl::make_range_iterator{*__value}.end()} noexcept;})
        requires (requires{{impl::make_range_iterator{*__value}.end()};})
    {
        return (impl::make_range_iterator{*__value}.end());
    }

    /* Get a forward iterator to one past the last element of the range. */
    [[nodiscard]] constexpr decltype(auto) cend() const
        noexcept (requires{{impl::make_range_iterator{*__value}.end()} noexcept;})
        requires (requires{{impl::make_range_iterator{*__value}.end()};})
    {
        return (impl::make_range_iterator{*__value}.end());
    }

    /* Get a reverse iterator to the last element of the range. */
    [[nodiscard]] constexpr decltype(auto) rbegin()
        noexcept (requires{{impl::make_range_reversed{*__value}.begin()} noexcept;})
        requires (requires{{impl::make_range_reversed{*__value}.begin()};})
    {
        return (impl::make_range_reversed{*__value}.begin());
    }

    /* Get a reverse iterator to the last element of the range. */
    [[nodiscard]] constexpr decltype(auto) rbegin() const
        noexcept (requires{{impl::make_range_reversed{*__value}.begin()} noexcept;})
        requires (requires{{impl::make_range_reversed{*__value}.begin()};})
    {
        return (impl::make_range_reversed{*__value}.begin());
    }

    /* Get a reverse iterator to the last element of the range. */
    [[nodiscard]] constexpr decltype(auto) crbegin() const
        noexcept (requires{{impl::make_range_reversed{*__value}.begin()} noexcept;})
        requires (requires{{impl::make_range_reversed{*__value}.begin()};})
    {
        return (impl::make_range_reversed{*__value}.begin());
    }

    /* Get a reverse iterator to one before the first element of the range. */
    [[nodiscard]] constexpr decltype(auto) rend()
        noexcept (requires{{impl::make_range_reversed{*__value}.end()} noexcept;})
        requires (requires{{impl::make_range_reversed{*__value}.end()};})
    {
        return (impl::make_range_reversed{*__value}.end());
    }

    /* Get a reverse iterator to one before the first element of the range. */
    [[nodiscard]] constexpr decltype(auto) rend() const
        noexcept (requires{{impl::make_range_reversed{*__value}.end()} noexcept;})
        requires (requires{{impl::make_range_reversed{*__value}.end()};})
    {
        return (impl::make_range_reversed{*__value}.end());
    }

    /* Get a reverse iterator to one before the first element of the range. */
    [[nodiscard]] constexpr decltype(auto) crend() const
        noexcept (requires{{impl::make_range_reversed{*__value}.end()} noexcept;})
        requires (requires{{impl::make_range_reversed{*__value}.end()};})
    {
        return (impl::make_range_reversed{*__value}.end());
    }

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
        constexpr bool sized_lhs = meta::has_size<L> && meta::exact_size<L>;
        constexpr bool sized_rhs = meta::has_size<R> && meta::exact_size<R>;

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

    /// TODO: also a monadic call operator which returns a comprehension that invokes
    /// each element of the range as a function with the given arguments.
    /// -> This requires some work on the `comprehension` class, such that I can
    /// define the expression template operators.

};


/* ADL `swap()` operator for ranges. */
template <typename C>
constexpr void swap(range<C>& lhs, range<C>& rhs)
    noexcept (requires{{lhs.swap(rhs)} noexcept;})
    requires (requires{{lhs.swap(rhs)};})
{
    lhs.swap(rhs);
}


static constexpr std::tuple tup {1, 2, 3.5};
static_assert([] {
    for (auto&& i : range(tup)) {
        if (i != 1 && i != 2 && i != 3.5) {
            return false;
        }
    }
    return true;
}());


////////////////////////
////    SEQUENCE    ////
////////////////////////


namespace impl {

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
            return impl::arrow_proxy(deref_fn(storage.begin));
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
template <meta::not_void T> requires (meta::not_rvalue<T>)
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

    /* Slice operator, which returns a subset of the range according to a Python-style
    `slice` expression. */
    template <typename Self, meta::slice S>
    [[nodiscard]] constexpr auto operator[](this Self&& self, S&& slice)
        noexcept (requires{{std::forward<S>(slice).range(std::forward<Self>(self))} noexcept;})
        requires (requires{{std::forward<S>(slice).range(std::forward<Self>(self))};})
    {
        return std::forward<S>(slice).range(std::forward<Self>(self));
    }

    /* Mask operator, which returns a subset of the range corresponding to the `true`
    values of boolean range.  The length of the resulting range is given by the number
    of true values in the mask or the size of this range, whichever is smaller. */
    template <typename Self, meta::range<bool> M>
    [[nodiscard]] constexpr auto operator[](this Self&& self, M&& mask)
        noexcept (requires{{std::forward<M>(mask)(std::forward<Self>(self))} noexcept;})
        requires (requires{{std::forward<M>(mask)(std::forward<Self>(self))};})
    {
        return std::forward<M>(mask)(std::forward<Self>(self));
    }
};


template <meta::iterable C>
sequence(C&& c) -> sequence<meta::remove_rvalue<meta::yield_type<C>>>;


/* ADL `swap()` operator for type-erased sequences. */
template <typename T>
constexpr void swap(sequence<T>& lhs, sequence<T>& rhs)
    noexcept (requires{{lhs.swap(rhs)} noexcept;})
    requires (requires{{lhs.swap(rhs)};})
{
    lhs.swap(rhs);
}


///////////////////
////    ZIP    ////
///////////////////


/// TODO: if the range is itself a tuple, and doesn't just yield tuples, then unpacking
/// it in a `zip{}` call should expand it and then broadcast the values as scalars.
/// Once that's implemented, `zip{}` should be done.

/// TODO: maybe unpacking a non-range argument should not be a problem, and always
/// attempts to unpack the individual elements rather than the whole argument.  This
/// will require some thought.


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
                meta::unpack_tuple<Is>(std::forward<T>(value))...
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template deref<I + 1>(
                std::forward<A>(args)...,
                meta::unpack_tuple<Is>(std::forward<T>(value))...
            )};})
        {
            return (std::forward<Self>(self).template deref<I + 1>(
                std::forward<A>(args)...,
                meta::unpack_tuple<Is>(std::forward<T>(value))...
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
                meta::unpack_tuple<Is>(std::forward<T>(value))...
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template subscript<I + 1>(
                i,
                std::forward<A>(args)...,
                meta::unpack_tuple<Is>(std::forward<T>(value))...
            )};})
        {
            return (std::forward<Self>(self).template subscript<I + 1>(
                i,
                std::forward<A>(args)...,
                meta::unpack_tuple<Is>(std::forward<T>(value))...
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

    template <typename>
    constexpr bool range_reverse_iterable = false;
    template <typename... A> requires ((!meta::range<A> || meta::reverse_iterable<A>) && ...)
    constexpr bool range_reverse_iterable<meta::pack<A...>> = true;

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
                meta::unpack_tuple<Is>(std::forward<T>(value))...
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template _get<I, J + 1>(
                std::forward<Ts>(args)...,
                meta::unpack_tuple<Is>(std::forward<T>(value))...
            )};})
        {
            return (std::forward<Self>(self).template _get<I, J + 1>(
                std::forward<Ts>(args)...,
                meta::unpack_tuple<Is>(std::forward<T>(value))...
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
                meta::unpack_tuple<Is>(std::forward<T>(value))...
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).template subscript<J + 1>(
                i,
                std::forward<Ts>(args)...,
                meta::unpack_tuple<Is>(std::forward<T>(value))...
            )};})
        {
            return (std::forward<Self>(self).template subscript<J + 1>(
                i,
                std::forward<Ts>(args)...,
                meta::unpack_tuple<Is>(std::forward<T>(value))...
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

    /* If no transformation function is provided, then `zip{}` will default to
    returning each value as a `Tuple`, similar to `std::views::zip()` or `zip()` in
    Python. */
    struct zip_tuple {
        template <typename... A> requires (!meta::range<A> && ...)
        [[nodiscard]] constexpr auto operator()(A&&... args)
            noexcept (requires{{Tuple{std::forward<A>(args)...}} noexcept;})
            requires (requires{{Tuple{std::forward<A>(args)...}};})
        {
            return Tuple{std::forward<A>(args)...};
        }
    };

}


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
    using range = bertrand::range<container<A...>>;

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
    using range = bertrand::range<container<A...>>;

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


static constexpr std::array arr1 {1, 2, 3};
static constexpr std::array arr2 {1, 2, 3, 4, 5};
static constexpr auto z = zip{
    [](int x, int y) { return x + y;}
}(range(arr1), range(arr2));
static_assert([] {
    auto r = zip{[](int x, int y) {
        return x + y;
    }}(range(arr1), range(arr2));

    if (r.size() != 3) return false;
    if ((*r.__value)[1] != 4) return false;

    for (auto&& x : z) {
        if (x != 2 && x != 4 && x != 6) {
            return false;
        }
    }
    return true;
}());


static constexpr std::array arr3 {std::pair{1, 2}, std::pair{3, 4}};
static constexpr auto r = zip{[](int x, int y, int z, int w) {
    return x + y;
}}(*range(arr3), range(arr1), 2);
static_assert([] {
    if (r.size() != 2) return false;
    if (r[0] != 3) return false;
    if (r[1] != 7) return false;

    for (auto&& x : r) {
        if (x != 3 && x != 7) {
            return false;
        }
    }

    return true;
}());


////////////////////
////    JOIN    ////
////////////////////


/// TODO: join{}(**range(a)) is currently ambiguous with a `*` range that yields
/// further `*` ranges.  Probably the only way to handle that is to change the way
/// `*` stacking is handled, and modify the iterator to yield further unpacked
/// ranges with one level removed.  That's the only way to make recursion make sense,
/// and it also means I can drop the unpack boolean flag, since I can just check for
/// an unpacked iterator type instead.


namespace impl {

    /// TODO: join may be called with zero range arguments? -> join{"."}("a", "b", "c")
    /// -> It may be a good idea to enforce the same standard for `zip`, for parity
    /// with join.

    template <typename Sep, typename... A>
    concept join_concept =
        sizeof...(A) > 0 &&
        (meta::not_void<A> && ...) &&
        (meta::not_rvalue<Sep> && ... && meta::not_rvalue<A>);

    template <typename C, size_t I>
    concept join_broadcast =
        !meta::range<typename meta::unqualify<C>::argument_types::template at<I>>;

    /// TODO: unpacking should now apply to any range that yields nested ranges.  That
    /// also means that subranges can just check to see whether the begin iterator
    /// dereferences to a range, without requiring a boolean flag.
    /// Also join_unpack -> join_flatten

    template <typename C, size_t I>
    concept join_unpack =
        meta::unpack<typename meta::unqualify<C>::argument_types::template at<I>> &&
        meta::range<meta::yield_type<typename meta::unqualify<C>::argument_types::template at<I>>>;

    /* Each argument can be either a non-range scalar, a range, or an unpacked range.
    `join_arg` determines the appropriate yield type for each of these categories, as
    applied to a single argument. */
    template <typename A>
    struct _join_arg { using type = A; };
    template <meta::range A>
    struct _join_arg<A> { using type = meta::yield_type<A>; };
    template <meta::unpack A>
    struct _join_arg<A> { using type = _join_arg<meta::yield_type<A>>::type; };
    template <typename A>
    using join_arg = _join_arg<A>::type;

    /* The yield type for the overall `join` range may need to be a union if any of the
    `join_arg` types differ.  If a separator is provided, then its yield type must also
    be included in the union.  If all of the yield types are the same, then the union
    will collapse into a single type, and the `trivial` flag will be set to true. */
    template <typename C, typename>
    struct _join_element;
    template <typename C, size_t... Is>
        requires (meta::is_void<typename meta::unqualify<C>::separator_type>)
    struct _join_element<C, std::index_sequence<Is...>> {
        using type = meta::union_type<
            join_arg<decltype((std::declval<C>().template arg<Is>()))>...
        >;
        static constexpr bool trivial = meta::trivial_union<
            join_arg<decltype((std::declval<C>().template arg<Is>()))>...
        >;
    };
    template <typename C, size_t... Is>
        requires (meta::not_void<typename meta::unqualify<C>::separator_type>)
    struct _join_element<C, std::index_sequence<Is...>> {
        using type = meta::union_type<
            join_arg<decltype((std::declval<C>().sep()))>,
            join_arg<decltype((std::declval<C>().template arg<Is>()))>...
        >;
        static constexpr bool trivial = meta::trivial_union<
            join_arg<decltype((std::declval<C>().sep()))>,
            join_arg<decltype((std::declval<C>().template arg<Is>()))>...
        >;
    };
    template <typename C>
    using join_element = _join_element<C, typename meta::unqualify<C>::indices>;

    template <meta::lvalue C>
    struct join_forward;
    template <typename C>
    join_forward(C&) -> join_forward<C&>;

    template <meta::lvalue C>
    struct join_reverse;
    template <typename C>
    join_reverse(C&) -> join_reverse<C&>;

    template <typename T>
    constexpr bool _is_join_subrange = false;
    template <typename T>
    concept is_join_subrange = _is_join_subrange<meta::unqualify<T>>;
    template <typename T>
    concept is_join_unpack = is_join_subrange<T> && meta::unqualify<T>::unpack;

    template <typename Sep, typename Begin, typename End>
    concept join_subrange_concept =
        (meta::is_void<Sep> || (meta::unqualified<Sep> && is_join_subrange<Sep>)) &&
        meta::unqualified<Begin> &&
        meta::unqualified<End> &&
        meta::iterator<Begin> &&
        meta::sentinel_for<End, Begin>;

    enum class join_direction : uint8_t {
        FORWARD,
        REVERSE
    };

    /* Join iterators always store both the begin and end iterators for each argument,
    so that they can smoothly advance from one to the next.  These subranges are stored
    in an internal union with duplicate types filtered out, which limits dispatch and
    memory overhead as much as possible. */
    template <join_direction Dir, typename Sep, typename Begin, typename End>
        requires (join_subrange_concept<Sep, Begin, End>)
    struct join_subrange {
        using begin_type = Begin;
        using end_type = End;
        static constexpr bool unpack = false;
        static constexpr join_direction direction = Dir;
        begin_type begin;
        end_type end;
    };

    template <join_direction Dir, typename Sep, typename Begin, typename End>
    constexpr bool _is_join_subrange<join_subrange<Dir, Sep, Begin, End>> = true;

    template <join_direction Dir, typename Sep, typename Begin, typename End>
    struct join_unpack_storage {
        struct subrange {
            using nested = meta::dereference_type<Begin>;
            using type = join_subrange<
                Dir,
                Sep,
                meta::unqualify<meta::begin_type<nested>>,
                meta::unqualify<meta::end_type<nested>>
            >;
            impl::ref<nested> data;
            type iters {
                .begin = data->begin(),
                .end = data->end()
            };
        };
        using separator = void;
        using type = subrange;
    };
    template <meta::is_void Sep, typename Begin, typename End>
    struct join_unpack_storage<join_direction::REVERSE, Sep, Begin, End> {
        struct subrange {
            using nested = meta::dereference_type<Begin>;
            using type = join_subrange<
                join_direction::REVERSE,
                Sep,
                meta::unqualify<meta::rbegin_type<nested>>,
                meta::unqualify<meta::rend_type<nested>>
            >;
            impl::ref<nested> data;
            type iters {
                .begin = data->rbegin(),
                .end = data->rend()
            };
        };
        using separator = void;
        using type = subrange;
    };
    template <join_direction Dir, meta::not_void Sep, typename Begin, typename End>
    struct join_unpack_storage<Dir, Sep, Begin, End> {
        using subrange = join_unpack_storage<Dir, void, Begin, End>::subrange;
        struct separator { Sep iters; };
        using type = Union<subrange, separator>;
    };


    template <typename Sep, typename... Subranges>
    concept join_union_concept =
        (meta::is_void<Sep> || (meta::unqualified<Sep> && is_join_subrange<Sep>)) &&
        ((meta::unqualified<Subranges> && is_join_subrange<Subranges>) && ...);

    /* Join iterators and nested subranges store the active subrange in an internal
    union that filters out duplicates as an optimization.  This minimizes the size of
    any corresponding vtables, potentially triggering dispatch optimizations and
    reducing overall binary size.  If all subranges are the same, then the union will
    be optimized out entirely.  The only downside is that the union's index no longer
    reflects the active argument being joined, which necessitates a separate tracking
    index. */
    template <typename Sep, typename... Subranges>
        requires (join_union_concept<Sep, Subranges...>)
    struct _join_union {
        using type = meta::union_type<Subranges...>;
        static constexpr bool trivial = meta::trivial_union<Subranges...>;
    };
    template <meta::not_void Sep, typename... Subranges>
        requires (join_union_concept<Sep, Subranges...>)
    struct _join_union<Sep, Subranges...> {
        using type = meta::union_type<Sep, Subranges...>;
        static constexpr bool trivial = meta::trivial_union<Sep, Subranges...>;
    };
    template <typename Sep, typename... Subranges>
        requires (join_union_concept<Sep, Subranges...>)
    struct join_union {
        using type = _join_union<Sep, Subranges...>::type;
        static constexpr bool trivial = _join_union<Sep, Subranges...>::trivial;
        using indices = std::make_index_sequence<
            trivial ? 1 : visitable<type>::alternatives::size()
        >;

        [[no_unique_address]] Optional<type> value;

        /* Check if the storage union is empty, signifying the end of the range.  The
        other methods in this class assume that this is false, and will result in
        undefined behavior otherwise. */
        [[nodiscard]] constexpr bool empty() const noexcept { return value == None; }

        /* Get the active index of the storage union.  Note that this does not
        necessarily correspond to the active argument in the joined range, due to
        duplicate subranges being filtered out.  Additionally, if a separator is
        present, then it will always be stored at index 0, followed by the unique
        subranges.  This index will be used to specialize any function template
        provided to `visit<F>()`. */
        constexpr size_t active() const noexcept {
            if constexpr (trivial) {
                return 0;
            } else {
                return value.__value.template get<1>().__value.index();
            }
        }

        /* Visit the inner union with a vtable function that accepts the `active()`
        index as a template parameter.  The function will then be called with the
        given arguments, and `get<I>()` can be used to safely access the inner
        subrange(s). */
        template <template <size_t> class F, typename... A>
        constexpr decltype(auto) visit(A&&... args) const
            noexcept (requires{
                {typename impl::vtable<F>::template dispatch<indices>{active()}(
                    std::forward<A>(args)...
                )} noexcept;
            })
            requires (requires{
                {typename impl::vtable<F>::template dispatch<indices>{active()}(
                    std::forward<A>(args)...
                )};
            })
        {
            return (typename impl::vtable<F>::template dispatch<indices>{active()})(
                std::forward<A>(args)...
            );
        }

        /* Access the current subrange as the indicated type, where `I` matches the
        semantics for `active()` and `visit()`. */
        template <size_t I, typename Self> requires (I < indices::size())
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            if constexpr (trivial) {
                return (std::forward<Self>(self).value.__value.template get<1>());
            } else {
                return (std::forward<Self>(self).value.
                    __value.template get<1>().
                    __value.template get<I>()
                );
            }
        }
    };



    /* If a range of ranges is supplied as an unpacked argument to join, then its
    subrange will be marked as such, and will be flattened into the output.  Such a
    subrange will consist of an outer iterator over the unpacked range and an inner
    iterator over its yield type, which will be cached within the subrange as long as
    they are needed.  If a separator is also present, then it will be inserted between
    each element of the outer iterator. */
    template <join_direction Dir, typename Sep, typename Begin, typename End>
        requires (
            join_subrange_concept<Sep, Begin, End> &&
            meta::range<meta::dereference_type<Begin>>
        )
    struct join_subrange<Dir, Sep, Begin, End> {
        using begin_type = Begin;
        using end_type = End;
        static constexpr bool unpack = true;
        static constexpr join_direction direction = Dir;
        begin_type begin;
        end_type end;

        using traits = join_unpack_storage<direction, Sep, Begin, End>;
        using subrange_type = traits::subrange::type;
        using separator_type = Sep;
        using storage_type = traits::type;
        static constexpr bool has_sep = meta::not_void<Sep>;
        static constexpr bool trivial = !has_sep;
        static constexpr size_t total_groups = 1 + has_sep;
        using group_type = impl::union_index_type<total_groups>;
        using group_indices = std::make_index_sequence<total_groups>;

        template <size_t G> requires (G < total_groups)
        static constexpr bool is_separator = G == 1;

        template <size_t G> requires (G < total_groups)
        static constexpr size_t normalize = G;


        /// TODO: store the cached value in a separate optional field and then reuse
        /// the `join_storage` type
        // using inner_type = meta::dereference_type<Begin>;
        // [[no_unique_address]] Optional<impl::ref<inner_type>> data;


        /* The empty state of the `iters` union signifies an empty range, which is
        handled in the traversal logic.  The remaining methods assume `iters` is never
        empty. */
        [[no_unique_address]] Optional<storage_type> iters;

        /* Get the active index of the inner union, which is identical to the encoded
        group index in this case.  This will always be either 0 or 1, where 0 indicates
        an inner subrange and 1 indicates a separator, if one is present. */
        [[nodiscard]] constexpr group_type active() const noexcept {
            if constexpr (trivial) {
                return 0;
            } else {
                return iters.__value.template get<1>().__value.index();
            }
        }

        /* Get the active index of the inner union, which is identical to the encoded
        group index in this case.  This will always be either 0 or 1, where 0 indicates
        an inner subrange and 1 indicates a separator, if one is present. */
        [[nodiscard]] constexpr group_type group() const noexcept { return active(); }

        /* Visit the inner union with a vtable function that accepts the `active()`
        index as a template parameter.  The function will then be called with the outer
        subrange, and `get<I>()` can be used to safely access the inner subrange. */
        template <template <size_t> class F, typename Self>
        constexpr decltype(auto) visit(this Self&& self)
            noexcept (requires{
                {typename impl::vtable<F>::template dispatch<group_indices>{self.active()}(
                    std::forward<Self>(self)
                )} noexcept;
            })
            requires (requires{
                {typename impl::vtable<F>::template dispatch<group_indices>{self.active()}(
                    std::forward<Self>(self)
                )};
            })
        {
            return (typename impl::vtable<F>::template dispatch<group_indices>{self.active()}(
                std::forward<Self>(self)
            ));
        }

        /* Visit the inner union with a vtable function that accepts the `group()`
        index as a template parameter.  The function will then be called with the outer
        subrange, and `get_group<G>()` can be used to safely access the inner
        subrange. */
        template <template <size_t> class F, typename Self>
        constexpr decltype(auto) dispatch(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).template visit<F>()} noexcept;})
            requires (requires{{std::forward<Self>(self).template visit<F>()};})
        {
            return (std::forward<Self>(self).template visit<F>());
        }

        /* Access the current subrange as the indicated type, where `I` is an index
        with the same semantics as `active()` and `visit()`. */
        template <size_t I, typename Self> requires (I < total_groups)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            if constexpr (trivial) {
                return (std::forward<Self>(self).iters.__value.template get<1>().iters);
            } else {
                return (std::forward<Self>(self).iters.
                    __value.template get<1>().
                    __value.template get<I>().
                    iters
                );
            }
        }

        /* Access the current subrange as the indicated type, where `G` is an index
        with the same semantics as `group()` and `dispatch()`. */
        template <size_t G, typename Self> requires (G < total_groups)
        [[nodiscard]] constexpr decltype(auto) get_group(this Self&& self) noexcept {
            return std::forward<Self>(self).template get<G>();
        }

        /* Populate the `iters` union with an inner subrange by dereferencing the
        begin iterator and caching the result.  The result can then be safely accessed
        via `get_group<G>()`. */
        template <size_t G, typename Iter> requires (G == 0)
        constexpr void init_group(Iter& it)
            noexcept (requires{{iters = typename traits::subrange{{*begin}}} noexcept;})
            requires (requires{{iters = typename traits::subrange{{*begin}}};})
        {
            iters = typename traits::subrange{{*begin}};
        }

        /* Populate the `iters` union with a separator subrange.  The result can then
        be safely accessed via `get_group<G>()`. */
        template <size_t G, typename Iter> requires (has_sep && G == 1)
        constexpr void init_group(Iter& it)
            noexcept (requires{
                {iters = typename traits::separator{join_forward{it.container->sep()}.begin()}} noexcept;
            })
            requires (Dir == join_direction::FORWARD && requires{
                {iters = typename traits::separator{join_forward{it.container->sep()}.begin()}};
            })
        {
            iters = typename traits::separator{join_forward{it.container->sep()}.begin()};
        }

        /* Populate the `iters` union with a separator subrange.  The result can then
        be safely accessed via `get_group<G>()`. */
        template <size_t G, typename Iter> requires (has_sep && G == 1)
        constexpr void init_group(Iter& it)
            noexcept (requires{
                {typename traits::separator{join_reverse{it.container->sep()}.begin()}} noexcept;
            })
            requires (Dir == join_direction::REVERSE && requires{
                {typename traits::separator{join_reverse{it.container->sep()}.begin()}};
            })
        {
            iters = typename traits::separator{join_reverse{it.container->sep()}.begin()};
        }
    };

    /* `join_forward` produces a `join_subrange` containing forward iterators over a
    `join` container. */
    template <meta::lvalue C> requires (!meta::range<C>)
    struct join_forward<C> {
        template <typename Sep>
        using type = join_subrange<
            join_direction::FORWARD,
            Sep,
            impl::contiguous_iterator<C>,
            impl::contiguous_iterator<C>
        >;

        C arg;

        template <typename Sep>
        constexpr auto begin()
            noexcept (requires{{type<Sep>{
                .m_begin = {std::addressof(arg)},
                .m_end = {std::addressof(arg) + 1}
            }} noexcept;})
            requires (requires{{type<Sep>{
                .m_begin = {std::addressof(arg)},
                .m_end = {std::addressof(arg) + 1}
            }};})
        {
            return type<Sep>{
                .m_begin = {std::addressof(arg)},
                .m_end = {std::addressof(arg) + 1}
            };
        }

        template <typename Sep>
        constexpr auto end()
            noexcept (requires{{type<Sep>{
                .m_begin = {std::addressof(arg) + 1},
                .m_end = {std::addressof(arg) + 1}
            }} noexcept;})
            requires (requires{{type<Sep>{
                .m_begin = {std::addressof(arg) + 1},
                .m_end = {std::addressof(arg) + 1}
            }};})
        {
            return type<Sep>{
                .m_begin = {std::addressof(arg) + 1},
                .m_end = {std::addressof(arg) + 1}
            };
        }
    };
    template <meta::lvalue C> requires (meta::range<C> && meta::iterable<C>)
    struct join_forward<C> {
        template <typename Sep>
        using type = join_subrange<
            join_direction::FORWARD,
            Sep,
            meta::unqualify<meta::begin_type<C>>,
            meta::unqualify<meta::end_type<C>>
        >;

        C arg;

        template <typename Sep>
        constexpr auto begin()
            noexcept (requires{{type<Sep>{
                .m_begin = arg.begin(),
                .m_end = arg.end()
            }} noexcept;})
            requires (requires{{type<Sep>{
                .m_begin = arg.begin(),
                .m_end = arg.end()
            }};})
        {
            return type<Sep>{
                .m_begin = arg.begin(),
                .m_end = arg.end()
            };
        }

        template <typename Sep>
        constexpr auto end()
            noexcept (requires{{type<Sep>{
                .m_begin = arg.end(),
                .m_end = arg.end()
            }} noexcept;})
            requires (requires{{type<Sep>{
                .m_begin = arg.end(),
                .m_end = arg.end()
            }};})
        {
            return type<Sep>{
                .m_begin = arg.end(),
                .m_end = arg.end()
            };
        }
    };
    template <meta::lvalue C>
        requires (meta::unpack<C> && meta::iterable<C> && meta::range<meta::yield_type<C>>)
    struct join_forward<C> {
        template <typename Sep>
        using type = join_subrange<
            join_direction::FORWARD,
            Sep,
            meta::unqualify<meta::begin_type<meta::yield_type<C>>>,
            meta::unqualify<meta::end_type<meta::yield_type<C>>>
        >;

        C arg;

        template <typename Sep>
        constexpr auto begin()
            noexcept (
                requires{{type<Sep>{.m_begin = arg.begin(), .m_end = arg.end()}} noexcept;} &&
                requires(type<Sep> result) {{result.advance()} noexcept;}
            )
            requires (
                requires{{type<Sep>{.m_begin = arg.begin(), .m_end = arg.end()}};} &&
                requires(type<Sep> result) {{result.advance()};}
            )
        {
            type<Sep> result {
                .m_begin = arg.begin(),
                .m_end = arg.end()
            };
            result.advance();
            return result;
        }

        template <typename Sep>
        constexpr auto end()
            noexcept (requires{{type<Sep>{.m_begin = arg.end(), .m_end = arg.end()}} noexcept;})
            requires (requires{{type<Sep>{.m_begin = arg.end(), .m_end = arg.end()}};})
        {
            return type<Sep>{
                .m_begin = arg.end(),
                .m_end = arg.end()
            };
        }
    };

    /* `join_reverse` produces a `join_subrange` containing reverse iterators over a
    `join` container. */
    template <meta::lvalue C> requires (!meta::range<C>)
    struct join_reverse<C> {
        template <typename Sep>
        using type = join_subrange<
            join_direction::REVERSE,
            Sep,
            std::reverse_iterator<impl::contiguous_iterator<C>>,
            std::reverse_iterator<impl::contiguous_iterator<C>>
        >;

        C arg;

        template <typename Sep>
        constexpr auto begin()
            noexcept (requires{{type<Sep>{
                .m_begin = std::make_reverse_iterator(
                    impl::contiguous_iterator<C>{std::addressof(arg) + 1}
                ),
                .m_end = std::make_reverse_iterator(
                    impl::contiguous_iterator<C>{std::addressof(arg)}
                )
            }} noexcept;})
            requires (requires{{type<Sep>{
                .m_begin = std::make_reverse_iterator(
                    impl::contiguous_iterator<C>{std::addressof(arg) + 1}
                ),
                .m_end = std::make_reverse_iterator(
                    impl::contiguous_iterator<C>{std::addressof(arg)}
                )
            }};})
        {
            return type<Sep>{
                .m_begin = std::make_reverse_iterator(
                    impl::contiguous_iterator<C>{std::addressof(arg) + 1}
                ),
                .m_end = std::make_reverse_iterator(
                    impl::contiguous_iterator<C>{std::addressof(arg)}
                )
            };
        }

        template <typename Sep>
        constexpr auto end()
            noexcept (requires{{type<Sep>{
                .m_begin = std::make_reverse_iterator(
                    impl::contiguous_iterator<C>{std::addressof(arg)}
                ),
                .m_end = std::make_reverse_iterator(
                    impl::contiguous_iterator<C>{std::addressof(arg)}
                )
            }} noexcept;})
            requires (requires{{type<Sep>{
                .m_begin = std::make_reverse_iterator(
                    impl::contiguous_iterator<C>{std::addressof(arg)}
                ),
                .m_end = std::make_reverse_iterator(
                    impl::contiguous_iterator<C>{std::addressof(arg)}
                )
            }};})
        {
            return type<Sep>{
                .m_begin = std::make_reverse_iterator(
                    impl::contiguous_iterator<C>{std::addressof(arg)}
                ),
                .m_end = std::make_reverse_iterator(
                    impl::contiguous_iterator<C>{std::addressof(arg)}
                )
            };
        }
    };
    template <meta::lvalue C> requires (meta::range<C> && meta::reverse_iterable<C>)
    struct join_reverse<C> {
        template <typename Sep>
        using type = join_subrange<
            join_direction::REVERSE,
            Sep,
            meta::unqualify<meta::rbegin_type<C>>,
            meta::unqualify<meta::rend_type<C>>
        >;

        C arg;

        template <typename Sep>
        constexpr auto begin()
            noexcept (requires{{type<Sep>{
                .m_begin = arg.rbegin(),
                .m_end = arg.rend()
            }} noexcept;})
            requires (requires{{type<Sep>{
                .m_begin = arg.rbegin(),
                .m_end = arg.rend()
            }};})
        {
            return type<Sep>{
                .m_begin = arg.rbegin(),
                .m_end = arg.rend()
            };
        }

        template <typename Sep>
        constexpr auto end()
            noexcept (requires{{type<Sep>{
                .m_begin = arg.rend(),
                .m_end = arg.rend()
            }} noexcept;})
            requires (requires{{type<Sep>{
                .m_begin = arg.rend(),
                .m_end = arg.rend()
            }};})
        {
            return type<Sep>{
                .m_begin = arg.rend(),
                .m_end = arg.rend()
            };
        }
    };
    template <meta::lvalue C>
        requires (meta::unpack<C> && meta::reverse_iterable<C> && meta::range<meta::yield_type<C>>)
    struct join_reverse<C> {
        template <typename Sep>
        using type = join_subrange<
            join_direction::REVERSE,
            Sep,
            meta::unqualify<meta::rbegin_type<meta::yield_type<C>>>,
            meta::unqualify<meta::rend_type<meta::yield_type<C>>>
        >;

        C arg;

        template <typename Sep>
        constexpr auto begin()
            noexcept (
                requires{{type<Sep>{.m_begin = arg.rbegin(), .m_end = arg.rend()}} noexcept;} &&
                requires(type<Sep> result) {{result.advance()} noexcept;}
            )
            requires (
                requires{{type<Sep>{.m_begin = arg.rbegin(), .m_end = arg.rend()}};} &&
                requires(type<Sep> result) {{result.advance()};}
            )
        {
            type<Sep> result {
                .m_begin = arg.rbegin(),
                .m_end = arg.rend()
            };
            result.advance();
            return result;
        }

        template <typename Sep>
        constexpr auto begin()
            noexcept (requires{{type<Sep>{.m_begin = arg.rend(), .m_end = arg.rend()}} noexcept;})
            requires (requires{{type<Sep>{.m_begin = arg.rend(), .m_end = arg.rend()}};})
        {
            return type<Sep>{
                .m_begin = arg.rend(),
                .m_end = arg.rend()
            };
        }
    };

    namespace join_deref {

        /// TODO: convert this into a standardized visitor that accepts `I` as a
        /// template parameter.

        template <meta::not_void element_type>
        struct deref {
            template <is_join_subrange subrange>
            [[nodiscard]] static constexpr element_type operator()(subrange&& self)
                noexcept (requires{{
                    *std::forward<subrange>(self).begin
                } noexcept -> meta::nothrow::convertible_to<element_type>;})
                requires (requires{{
                    *std::forward<subrange>(self).begin
                } -> meta::convertible_to<element_type>;})
            {
                return *std::forward<subrange>(self).begin;
            }

            struct fn {
                template <typename inner>
                static constexpr element_type operator()(inner&& self)
                    noexcept (requires{
                        {deref::operator()(std::forward<inner>(self).iters)} noexcept;
                    })
                    requires (requires{
                        {deref::operator()(std::forward<inner>(self).iters)};
                    })
                {
                    return deref::operator()(std::forward<inner>(self).iters);
                }
            };

            template <is_join_unpack subrange>
            [[nodiscard]] static constexpr element_type operator()(subrange&& self)
                noexcept (requires{
                    {impl::visit(fn{}, std::forward<subrange>(self).inner())} noexcept;
                })
                requires (requires{
                    {impl::visit(fn{}, std::forward<subrange>(self).inner())};
                })
            {
                return impl::visit(fn{}, std::forward<subrange>(self).inner());
            }
        };

    }

    namespace join_increment {

        /// TODO: The following must be standardized between join_iterator and unpacked
        /// subranges so that the increment machinery can be recursively applied to
        /// both types:
        /// - has_sep
        /// - separator_type
        /// - subrange_types
        /// - get<I>()
        /// - total_groups

        /// I'm going to have to be really careful when I do this, since it is
        /// incredibly easy to get the member accesses wrong.




        /// NOTE: in this context, `I` indicates the state of the
        /// `join_iterator::groups` counter, which encodes the current argument index
        /// along with possible separators.  If a separator is present, then every odd
        /// index will be a separator, and will transition to the next argument from
        /// the `join{}(args...)` signature when exhausted.  Even indices will
        /// transition to a separator subrange, and since the index is known at compile
        /// time within the vtable context, these decisions will not introduce any
        /// extra branches at runtime.

        template <meta::not_const parent, size_t J> requires (J < parent::total_groups)
        static constexpr bool is_separator = parent::has_sep && (J % 2 == 1);

        template <meta::not_const parent, size_t J> requires (J < parent::total_groups)
        static constexpr size_t normalize = parent::has_sep ? J / 2 : J;

        template <meta::not_const parent, size_t J> requires (J < parent::total_groups)
        using subrange_type = std::conditional_t<
            is_separator<parent, J>,
            typename parent::separator_type,
            typename parent::subrange_types::template at<normalize<parent, J>>
        >;

        /// TODO: in the unpack case, arg<J>() will need to dereference the begin
        /// iterator of the internal subrange after casting it to the right type.
        /// -> maybe value<J>() instead for clarity?

        template <size_t J, meta::not_const Iter, meta::not_const parent>
        constexpr subrange_type<parent, J> start(Iter& it, parent& p)
            noexcept (requires{{
                join_forward{p.template arg<normalize<parent, J>>()}.begin()
            } noexcept -> meta::nothrow::convertible_to<subrange_type<parent, J>>;})
            requires (!is_separator<parent, J> && requires{{
                join_forward{p.template arg<normalize<parent, J>>()}.begin()
            } -> meta::is<subrange_type<parent, J>>;})
        {
            return join_forward{p.template arg<normalize<parent, J>>()}.begin();
        }

        template <size_t J, meta::not_const Iter, meta::not_const parent>
        constexpr subrange_type<parent, J> start(Iter& it, parent& p)
            noexcept (requires{{
                join_reverse{p.template arg<normalize<parent, J>>()}.begin()
            } noexcept -> meta::nothrow::convertible_to<subrange_type<parent, J>>;})
            requires (!is_separator<parent, J> && requires{{
                join_reverse{p.template arg<normalize<parent, J>>()}.begin()
            } -> meta::is<subrange_type<parent, J>>;})
        {
            return join_reverse{p.template arg<normalize<parent, J>>()}.begin();
        }

        template <size_t J, meta::not_const Iter, meta::not_const parent>
        constexpr subrange_type<parent, J> start(Iter& it, parent& p)
            noexcept (requires{{
                join_forward{it.container->sep()}.begin()
            } noexcept -> meta::nothrow::convertible_to<subrange_type<parent, J>>;})
            requires (is_separator<parent, J> && requires{{
                join_forward{it.container->sep()}.begin()
            } -> meta::is<subrange_type<parent, J>>;})
        {
            return join_forward{it.container->sep()}.begin();
        }

        template <size_t J, meta::not_const Iter, meta::not_const parent>
        constexpr subrange_type<parent, J> start(Iter& it, parent& p)
            noexcept (requires{{
                join_reverse{it.container->sep()}.begin()
            } noexcept -> meta::nothrow::convertible_to<subrange_type<parent, J>>;})
            requires (is_separator<parent, J> && requires{{
                join_reverse{it.container->sep()}.begin()
            } -> meta::is<subrange_type<parent, J>>;})
        {
            return join_reverse{it.container->sep()}.begin();
        }

        template <size_t J, meta::not_const Iter, meta::not_const parent>
        constexpr bool skip(Iter& it, parent& p)
            noexcept (J >= parent::total_groups || requires(subrange_type<parent, J> s) {
                {
                    start<J>(it, p)
                } noexcept -> meta::nothrow::convertible_to<subrange_type<parent, J>>;
                {s.begin == s.end} noexcept -> meta::nothrow::convertible_to<bool>;
                {p.iters = std::move(s)} noexcept;
                {skip<J + 1>(it, p)} noexcept;
            })
            requires (J >= parent::total_groups || requires(subrange_type<parent, J> s) {
                {
                    start<J>(it, p)
                } -> meta::convertible_to<subrange_type<parent, J>>;
                {s.begin == s.end} -> meta::convertible_to<bool>;
                {p.iters = std::move(s)};
                {skip<J + 1>(it, p)};
            })
        {
            if constexpr (J < parent::total_groups) {
                subrange_type<parent, J> s = start<J>(it, p);
                if (s.begin == s.end) {
                    ++p.group;
                    return skip<J + 1>(it, p);
                }
                p.iters = std::move(s);
                return true;
            } else {
                return false;
            }
        }

        /* A vtable function that increments a join iterator, accounting for the transition
        from one component range to the next, and inserting separators where appropriate. */
        template <size_t I>
        struct increment {
            template <meta::not_const Iter, meta::not_const parent, is_join_subrange subrange>
            static constexpr void call(Iter& it, parent& p, subrange& s)
                noexcept (requires{
                    {++s.begin} noexcept;
                    {s.begin == s.end} noexcept -> meta::nothrow::convertible_to<bool>;
                    {skip<I + 1>(it, p)} noexcept;
                } && (!is_join_unpack<parent> || requires{
                    {++p.begin} noexcept;
                    {p.begin == p.end} noexcept -> meta::nothrow::convertible_to<bool>;
                    {skip<0>(it, p)} noexcept;
                }))
                requires (requires{
                    {++s.begin};
                    {s.begin == s.end} -> meta::convertible_to<bool>;
                    {skip<I + 1>(it, p)};
                } && (!is_join_unpack<parent> || requires{
                    {++p.begin};
                    {p.begin == p.end} -> meta::convertible_to<bool>;
                    {skip<0>(it, p)};
                }))
            {
                ++s.begin;
                if (s.begin == s.end) {
                    ++p.group;
                    if constexpr (is_join_unpack<parent>) {
                        if (!skip<I + 1>(it, p)) {
                            ++p.begin;
                            if (p.begin == p.end) {
                                return;
                            }
                            p.group = 0;
                            while (!skip<0>(it, p)) {
                                ++p.begin;
                                if (p.begin == p.end) {
                                    return;
                                }
                                p.group = 0;
                            }
                        }
                    } else {
                        skip<I + 1>(it, p);
                    }
                }
            }

            struct fn {
                template <meta::not_const Iter, is_join_unpack subrange, typename inner>
                static constexpr void operator()(Iter& it, subrange s, inner& i)
                    noexcept (requires{{call(it, s, i.iters)} noexcept;})
                    requires (requires{{call(it, s, i.iters)};})
                {
                    return call(it, s, i.iters);
                }
            };

            template <meta::not_const Iter, meta::not_const parent, is_join_unpack subrange>
            static constexpr void call(Iter& it, parent& p, subrange& s)
                noexcept (requires{{impl::visit(fn{}, it, s, s.inner())} noexcept;})
                requires (requires{{impl::visit(fn{}, it, s, s.inner())};})
            {
                return impl::visit(fn{}, it, s, s.inner());
            }

            /// NOTE: the separator subrange is always the first element of the
            /// internal union, so all odd indices map to 0.  Otherwise, even indices
            /// are halved and start at index 1.

            template <meta::not_const Iter>
            static constexpr void operator()(Iter& it)
                noexcept (requires{{call(
                    it,
                    it,
                    it.template get<is_separator<Iter, I> ? 0 : normalize<Iter, I> + Iter::has_sep>()
                )} noexcept;})
                requires (requires{{call(
                    it,
                    it,
                    it.template get<is_separator<Iter, I> ? 0 : normalize<Iter, I> + Iter::has_sep>()
                )};})
            {
                call(
                    it,
                    it,
                    it.template get<is_separator<Iter, I> ? 0 : normalize<Iter, I> + Iter::has_sep>()
                );
            }
        };

    }

    namespace join_compare {

        template <is_join_unpack subrange, typename op>
        struct dispatch {
            template <size_t J>
            struct fn {
                template <typename inner>
                static constexpr auto operator()(const inner& lhs, const inner& rhs)
                    noexcept (requires{{
                        op::call(lhs.template get<J>(), rhs.template get<J>())
                    } noexcept;})
                    requires (requires{{
                        op::call(lhs.template get<J>(), rhs.template get<J>())
                    };})
                {
                    return op::call(lhs.template get<J>(), rhs.template get<J>());
                }
            };

            using vtable = impl::vtable<fn>::template dispatch<
                std::make_index_sequence<2 - subrange::trivial>
            >;

            static constexpr auto operator()(const subrange& lhs, const subrange& rhs)
                noexcept (requires{{
                    vtable{lhs.inner_index()}(lhs.inner(), rhs.inner())
                } noexcept;})
                requires (requires{{
                    vtable{lhs.inner_index()}(lhs.inner(), rhs.inner())
                };})
            {
                return vtable{lhs.inner_index()}(lhs.inner(), rhs.inner());
            }
        };

        template <size_t I>
        struct lt {
            template <is_join_subrange subrange>
            static constexpr bool call(const subrange& lhs, const subrange& rhs)
                noexcept (requires{{
                    lhs.begin < rhs.begin
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.begin < rhs.begin
                } -> meta::convertible_to<bool>;})
            {
                return lhs.begin < rhs.begin;
            }

            template <is_join_unpack subrange>
            static constexpr bool call(const subrange& lhs, const subrange& rhs)
                noexcept (requires{{dispatch<subrange, lt>{}(lhs, rhs)} noexcept;})
                requires (requires{{dispatch<subrange, lt>{}(lhs, rhs)};})
            {
                return lhs.inner_index() < rhs.inner_index() || (
                    lhs.inner_index() == rhs.inner_index() &&
                    dispatch<subrange, lt>{}(lhs, rhs)
                );
            }

            template <typename Iter>
            static constexpr bool operator()(const Iter& lhs, const Iter& rhs)
                noexcept (requires{{
                    call(lhs.template get<I>(), rhs.template get<I>())
                } noexcept;})
                requires (requires{{
                    call(lhs.template get<I>(), rhs.template get<I>())
                };})
            {
                return call(lhs.template get<I>(), rhs.template get<I>());
            }
        };

        template <size_t I>
        struct le {
            template <is_join_subrange subrange>
            static constexpr bool call(const subrange& lhs, const subrange& rhs)
                noexcept (requires{{
                    lhs.begin <= rhs.begin
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.begin <= rhs.begin
                } -> meta::convertible_to<bool>;})
            {
                return lhs.begin <= rhs.begin;
            }

            template <is_join_unpack subrange>
            static constexpr bool call(const subrange& lhs, const subrange& rhs)
                noexcept (requires{{dispatch<subrange, le>{}(lhs, rhs)} noexcept;})
                requires (requires{{dispatch<subrange, le>{}(lhs, rhs)};})
            {
                return lhs.inner_index() < rhs.inner_index() || (
                    lhs.inner_index() == rhs.inner_index() &&
                    dispatch<subrange, le>{}(lhs, rhs)
                );
            }

            template <typename Iter>
            static constexpr bool operator()(const Iter& lhs, const Iter& rhs)
                noexcept (requires{{
                    call(lhs.template get<I>(), rhs.template get<I>())
                } noexcept;})
                requires (requires{{
                    call(lhs.template get<I>(), rhs.template get<I>())
                };})
            {
                return call(lhs.template get<I>(), rhs.template get<I>());
            }
        };

        template <size_t I>
        struct eq {
            template <is_join_subrange subrange>
            static constexpr bool call(const subrange& lhs, const subrange& rhs)
                noexcept (requires{{
                    lhs.begin == rhs.begin
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.begin == rhs.begin
                } -> meta::convertible_to<bool>;})
            {
                return lhs.begin == rhs.begin;
            }

            template <is_join_unpack subrange>
            static constexpr bool call(const subrange& lhs, const subrange& rhs)
                noexcept (requires{{dispatch<subrange, eq>{}(lhs, rhs)} noexcept;})
                requires (requires{{dispatch<subrange, eq>{}(lhs, rhs)};})
            {
                return
                    lhs.inner_index() == rhs.inner_index() &&
                    dispatch<subrange, eq>{}(lhs, rhs);
            }

            template <typename Iter>
            static constexpr bool operator()(const Iter& lhs, const Iter& rhs)
                noexcept (requires{{
                    call(lhs.template get<I>(), rhs.template get<I>())
                } noexcept;})
                requires (requires{{
                    call(lhs.template get<I>(), rhs.template get<I>())
                };})
            {
                return call(lhs.template get<I>(), rhs.template get<I>());
            }
        };

        template <size_t I>
        struct ne {
            template <is_join_subrange subrange>
            static constexpr bool call(const subrange& lhs, const subrange& rhs)
                noexcept (requires{{
                    lhs.begin != rhs.begin
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.begin != rhs.begin
                } -> meta::convertible_to<bool>;})
            {
                return lhs.begin != rhs.begin;
            }

            template <is_join_unpack subrange>
            static constexpr bool call(const subrange& lhs, const subrange& rhs)
                noexcept (requires{{dispatch<subrange, ne>{}(lhs, rhs)} noexcept;})
                requires (requires{{dispatch<subrange, ne>{}(lhs, rhs)};})
            {
                return
                    lhs.inner_index() != rhs.inner_index() ||
                    dispatch<subrange, ne>{}(lhs, rhs);
            }

            template <typename Iter>
            static constexpr bool operator()(const Iter& lhs, const Iter& rhs)
                noexcept (requires{{
                    call(lhs.template get<I>(), rhs.template get<I>())
                } noexcept;})
                requires (requires{{
                    call(lhs.template get<I>(), rhs.template get<I>())
                };})
            {
                return call(lhs.template get<I>(), rhs.template get<I>());
            }
        };

        template <size_t I>
        struct ge {
            template <is_join_subrange subrange>
            static constexpr bool call(const subrange& lhs, const subrange& rhs)
                noexcept (requires{{
                    lhs.begin >= rhs.begin
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.begin >= rhs.begin
                } -> meta::convertible_to<bool>;})
            {
                return lhs.begin >= rhs.begin;
            }

            template <is_join_unpack subrange>
            static constexpr bool call(const subrange& lhs, const subrange& rhs)
                noexcept (requires{{dispatch<subrange, ge>{}(lhs, rhs)} noexcept;})
                requires (requires{{dispatch<subrange, ge>{}(lhs, rhs)};})
            {
                return lhs.inner_index() > rhs.inner_index() || (
                    lhs.inner_index() == rhs.inner_index() &&
                    dispatch<subrange, ge>{}(lhs, rhs)
                );
            }

            template <typename Iter>
            static constexpr bool operator()(const Iter& lhs, const Iter& rhs)
                noexcept (requires{{
                    call(lhs.template get<I>(), rhs.template get<I>())
                } noexcept;})
                requires (requires{{
                    call(lhs.template get<I>(), rhs.template get<I>())
                };})
            {
                return call(lhs.template get<I>(), rhs.template get<I>());
            }
        };

        template <size_t I>
        struct gt {
            template <is_join_subrange subrange>
            static constexpr bool call(const subrange& lhs, const subrange& rhs)
                noexcept (requires{{
                    lhs.begin > rhs.begin
                } noexcept -> meta::nothrow::convertible_to<bool>;})
                requires (requires{{
                    lhs.begin > rhs.begin
                } -> meta::convertible_to<bool>;})
            {
                return lhs.begin > rhs.begin;
            }

            template <is_join_unpack subrange>
            static constexpr bool call(const subrange& lhs, const subrange& rhs)
                noexcept (requires{{dispatch<subrange, gt>{}(lhs, rhs)} noexcept;})
                requires (requires{{dispatch<subrange, gt>{}(lhs, rhs)};})
            {
                return lhs.inner_index() > rhs.inner_index() || (
                    lhs.inner_index() == rhs.inner_index() &&
                    dispatch<subrange, gt>{}(lhs, rhs)
                );
            }

            template <typename Iter>
            static constexpr bool operator()(const Iter& lhs, const Iter& rhs)
                noexcept (requires{{
                    call(lhs.template get<I>(), rhs.template get<I>())
                } noexcept;})
                requires (requires{{
                    call(lhs.template get<I>(), rhs.template get<I>())
                };})
            {
                return call(lhs.template get<I>(), rhs.template get<I>());
            }
        };

        /// TODO: distance is going to have to account for the index array for iterators
        /// where the indices differ, which is still very much a work in progress.

        template <typename difference_type>
        struct distance {
            template <size_t I>
            struct fn {
                template <is_join_subrange subrange>
                static constexpr difference_type call(const subrange& lhs, const subrange& rhs)
                    noexcept (requires{{
                        lhs.begin - rhs.begin
                    } noexcept -> meta::nothrow::convertible_to<difference_type>;})
                    requires (requires{{
                        lhs.begin - rhs.begin
                    } -> meta::convertible_to<difference_type>;})
                {
                    return lhs.begin - rhs.begin;
                }

                template <is_join_unpack subrange>
                static constexpr difference_type call(const subrange& lhs, const subrange& rhs)
                    noexcept (requires{{dispatch<subrange, fn>{}(lhs, rhs)} noexcept;})
                    requires (requires{{dispatch<subrange, fn>{}(lhs, rhs)};})
                {
                    return lhs.inner_index() > rhs.inner_index() || (
                        lhs.inner_index() == rhs.inner_index() &&
                        dispatch<subrange, fn>{}(lhs, rhs)
                    );
                }

                template <typename Iter>
                static constexpr difference_type operator()(const Iter& lhs, const Iter& rhs)
                    noexcept (requires{{
                        call(lhs.template get<I>(), rhs.template get<I>())
                    } noexcept;})
                    requires (requires{{
                        call(lhs.template get<I>(), rhs.template get<I>())
                    };})
                {
                    return call(lhs.template get<I>(), rhs.template get<I>());
                }
            };
        };

    }

    template <typename C, typename Sep, typename... Subranges>
    concept join_iterator_concept =
        sizeof...(Subranges) > 0 &&
        meta::lvalue<C> &&
        join_union_concept<Sep, Subranges...>;

    /// TODO: C -> R (for range) on both this and zip iterators.

    /* Join iterators work by storing a pointer to the joined range, an index recording
    the current sub-range, and a union of backing iterators, which store both the begin
    and end iterators for each range.  Scalars are promoted to ranges of length 1 for
    the purposes of this union.  When the iterator is incremented, the current begin
    iterator will be advanced, and if it equals the corresponding end iterator, the
    index will be incremented and the next range will be initialized, skipping any that
    are empty.  If all of the begin and end types for each of the input ranges are
    the same, then the joined range will model `std::common_range`, and the end
    iterator will be initialized with the end iterator for the final argument.

    Note that if a common type exists between all of the iterators, then the internal
    union will be optimized out, and the common iterator type will be stored directly
    as an optimization, which avoids extra dispatching overhead.  Additionally, if all
    of the component ranges return a single type, then the overall joined iterator will
    dereference to that type directly.  Otherwise, it will return a union of the
    possible types across all of the ranges. */
    template <typename C, typename Sep, typename... Subranges>
        requires (join_iterator_concept<C, Sep, Subranges...>)
    struct join_iterator {
        using container_type = C;
        using element_type = join_element<C>::type;
        using separator_type = Sep;
        using subrange_types = meta::pack<Subranges...>;
        using storage = join_union<Sep, Subranges...>;
        using iterator_category = range_category<
            typename meta::unqualify<C>::ranges,
            Subranges...
        >::type;
        using difference_type = range_difference<
            typename meta::unqualify<C>::ranges,
            Subranges...
        >::type;
        using value_type = meta::remove_reference<element_type>;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::address_type<value_type>;

        meta::as_pointer<C> container = nullptr;
        [[no_unique_address]] storage store;

        /* The `group` index identifies which of the original joined arguments is
        currently being iterated over, and encodes separators as odd indices if they
        are present.  The encoded value is used as vtable index for the iterator's
        traversal methods, which need to know the precise argument or separator being
        iterated over, so that they can transition to the next argument or separator in
        the overall range. */
        static constexpr bool has_sep = meta::not_void<Sep>;
        static constexpr size_t total_groups =
            sizeof...(Subranges) + (sizeof...(Subranges) - 1) * has_sep;
        using group_type = impl::union_index_type<total_groups>;
        using group_indices = std::make_index_sequence<total_groups>;
        template <size_t G> requires (G < total_groups)
        static constexpr bool is_separator = has_sep && (G % 2 == 1);
        template <size_t G> requires (G < total_groups)
        static constexpr size_t normalize = has_sep ? G / 2 : G;
        template <size_t G> requires (G < total_groups)
        static constexpr join_direction direction = 
            meta::unpack_type<normalize<G>, Subranges...>::direction;

        [[no_unique_address]] group_type _group = 0;

        /* Get the active group index, which is used to determine which of the joined
        arguments is currently being iterated over.  If separators are present, then
        they will be encoded as odd indices, in which case the argument index can be
        obtained by halving the group index.  This index will be used to specialize any
        function template provided to `dispatch<F>()`. */
        [[nodiscard]] constexpr group_type group() const noexcept { return _group; }

        /* Visit the inner union with a vtable function that accepts the `group()`
        index as a template parameter.  The function will then be called with the
        iterator object, and `get_group<G>()` can be used to safely access the inner
        subrange. */
        template <template <size_t> class F, typename Self>
        constexpr decltype(auto) dispatch(this Self&& self)
            noexcept (requires{
                {typename impl::vtable<F>::template dispatch<group_indices>{self.active()}(
                    std::forward<Self>(self)
                )} noexcept;
            })
            requires (requires{
                {typename impl::vtable<F>::template dispatch<group_indices>{self.active()}(
                    std::forward<Self>(self)
                )};
            })
        {
            return (typename impl::vtable<F>::template dispatch<group_indices>{self.active()})(
                std::forward<Self>(self)
            );
        }

        /// TODO: get_group<G>(), which involves an index translation from G to I.
        /// That can be done by mapping odd G to get<0>() and even G to the storage
        /// index of the subrange_type at G / 2.

        /* Populate the `store` union with an inner subrange over the group at index
        `G`.  The result can then be safely accessed via `get_group<G>()`. */
        template <size_t G> requires (!is_separator<G>)
        constexpr void init_group(join_iterator& it)
            noexcept (requires{
                {store.value = join_forward{container->template arg<normalize<G>>()}.begin()} noexcept;
            })
            requires (direction<G> == join_direction::FORWARD && requires{
                {store.value = join_forward{container->template arg<normalize<G>>()}.begin()};
            })
        {
            store.value = join_forward{container->template arg<normalize<G>>()}.begin();
        }

        /* Populate the `iters` union with an inner subrange over the group at index
        `G`.  The result can then be safely accessed via `get_group<G>()`. */
        template <size_t G> requires (!is_separator<G>)
        constexpr void init_group(join_iterator& it)
            noexcept (requires{
                {store.value = join_reverse{container->template arg<normalize<G>>()}.begin()} noexcept;
            })
            requires (direction<G> == join_direction::REVERSE && requires{
                {store.value = join_reverse{container->template arg<normalize<G>>()}.begin()};
            })
        {
            store.value = join_reverse{container->template arg<normalize<G>>()}.begin();
        }

        /* Populate the `iters` union with an inner subrange over the group at index
        `G`.  The result can then be safely accessed via `get_group<G>()`. */
        template <size_t G> requires (is_separator<G>)
        constexpr void init_group(join_iterator& it)
            noexcept (requires{
                {store.value = join_forward{container->sep()}.begin()} noexcept;
            })
            requires (direction<G> == join_direction::FORWARD && requires{
                {store.value = join_forward{container->sep()}.begin()};
            })
        {
            store.value = join_forward{container->sep()}.begin();
        }

        /* Populate the `iters` union with an inner subrange over the group at index
        `G`.  The result can then be safely accessed via `get_group<G>()`. */
        template <size_t G> requires (is_separator<G>)
        constexpr void init_group(join_iterator& it)
            noexcept (requires{
                {store.value = join_reverse{container->sep()}.begin()} noexcept;
            })
            requires (direction<G> == join_direction::REVERSE && requires{
                {store.value = join_reverse{container->sep()}.begin()};
            })
        {
            store.value = join_reverse{container->sep()}.begin();
        }

    private:
        using increment = impl::vtable<join_increment::increment>::template dispatch<group_indices>;

        /// TODO: other increment/decrement methods
        /// -> use dispatch<F>() to keep track of the current group index

    public:
        [[nodiscard]] constexpr element_type operator*()
            noexcept (requires{
                {impl::visit(join_deref::deref<element_type>{}, iters)} noexcept;
            })
            requires (requires{
                {impl::visit(join_deref::deref<element_type>{}, iters)};
            })
        {
            return impl::visit(join_deref::deref<element_type>{}, iters);
        }

        [[nodiscard]] constexpr element_type operator*() const
            noexcept (requires{
                {impl::visit(join_deref::deref<element_type>{}, iters)} noexcept;
            })
            requires (requires{
                {impl::visit(join_deref::deref<element_type>{}, iters)};
            })
        {
            return impl::visit(join_deref::deref<element_type>{}, iters);
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{impl::arrow_proxy{**this}} noexcept;})
            requires (requires{{impl::arrow_proxy{**this}};})
        {
            return impl::arrow_proxy{**this};
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow_proxy{**this}} noexcept;})
            requires (requires{{impl::arrow_proxy{**this}};})
        {
            return impl::arrow_proxy{**this};
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, difference_type i)
            noexcept (false)
            requires (false)
        {
            /// TODO: implement this once random access has been nailed down
        }

        constexpr join_iterator& operator++()
            noexcept (requires{{increment{active()}(*this)} noexcept;})
            requires (requires{{increment{active()}(*this)};})
        {
            increment{active()}(*this);
            return *this;
        }

        [[nodiscard]] constexpr join_iterator operator++(int)
            noexcept (
                meta::nothrow::copyable<join_iterator> &&
                meta::nothrow::has_preincrement<join_iterator>
            )
            requires (
                meta::copyable<join_iterator> &&
                meta::has_preincrement<join_iterator>
            )
        {
            join_iterator tmp = *this;
            ++*this;
            return tmp;
        }

        [[nodiscard]] friend constexpr join_iterator operator+(
            const join_iterator& self,
            difference_type i
        )
            noexcept (false)
            requires (false)
        {
            /// TODO: implement this  random access has been nailed down
        }

        [[nodiscard]] friend constexpr join_iterator operator+(
            difference_type i,
            const join_iterator& self
        )
            noexcept (false)
            requires (false)
        {
            /// TODO: implement this once random access has been nailed down
        }

        constexpr join_iterator& operator+=(difference_type i)
            noexcept (false)
            requires (false)
        {
            /// TODO: implement this random access has been nailed down
        }

        constexpr join_iterator& operator--()
            noexcept (false)
            requires (false)
        {
            /// TODO: implement this once storage has been nailed down
        }

        [[nodiscard]] constexpr join_iterator operator--(int)
            noexcept (
                meta::nothrow::copyable<join_iterator> &&
                meta::nothrow::has_predecrement<join_iterator>
            )
            requires (
                meta::copyable<join_iterator> &&
                meta::has_predecrement<join_iterator>
            )
        {
            join_iterator tmp = *this;
            --*this;
            return tmp;
        }

        [[nodiscard]] constexpr join_iterator operator-(difference_type i) const
            noexcept (false)
            requires (false)
        {
            /// TODO: implement this once random access has been nailed down
        }

        [[nodiscard]] constexpr difference_type operator-(
            const join_iterator& other
        ) const
            noexcept (false)
            requires (false)
        {
            // return

            /// TODO: implement this once random access has been nailed down
        }

        [[nodiscard]] constexpr join_iterator& operator-=(difference_type i)
            noexcept (false)
            requires (false)
        {
            /// TODO: implement this once random access has been nailed down
        }

        [[nodiscard]] constexpr bool operator<(const join_iterator& other) const
            noexcept (requires{{
                store.template visit<join_compare::lt>(*this, other)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                store.template visit<join_compare::lt>(*this, other)
            } -> meta::convertible_to<bool>;})
        {
            return group() < other.group() || (
                group() == other.group() &&
                store.template visit<join_compare::lt>(*this, other)
            );
        }

        [[nodiscard]] constexpr bool operator<=(const join_iterator& other) const
            noexcept (requires{{
                store.template visit<join_compare::le>(*this, other)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                store.template visit<join_compare::le>(*this, other)
            } -> meta::convertible_to<bool>;})
        {
            return group() < other.group() || (
                group() == other.group() &&
                store.template visit<join_compare::le>(*this, other)
            );
        }

        [[nodiscard]] constexpr bool operator==(const join_iterator& other) const
            noexcept (requires{{
                store.template visit<join_compare::eq>(*this, other)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                store.template visit<join_compare::eq>(*this, other)
            } -> meta::convertible_to<bool>;})
        {
            return
                group() == other.group() &&
                store.template visit<join_compare::eq>(*this, other);
        }

        [[nodiscard]] friend constexpr bool operator==(
            const join_iterator& self,
            NoneType
        ) noexcept {
            return self.group >= sizeof...(Subranges);
        }

        [[nodiscard]] friend constexpr bool operator==(
            NoneType,
            const join_iterator& self
        ) noexcept {
            return self.group >= sizeof...(Subranges);
        }

        [[nodiscard]] constexpr bool operator!=(const join_iterator& other) const
            noexcept (requires{{
                store.template visit<join_compare::ne>(*this, other)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                store.template visit<join_compare::ne>(*this, other)
            } -> meta::convertible_to<bool>;})
        {
            return
                group() != other.group() ||
                store.template visit<join_compare::ne>(*this, other);
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const join_iterator& self,
            NoneType
        ) noexcept {
            return self.group() < sizeof...(Subranges);
        }

        [[nodiscard]] friend constexpr bool operator!=(
            NoneType,
            const join_iterator& self
        ) noexcept {
            return self.group() < sizeof...(Subranges);
        }

        [[nodiscard]] constexpr bool operator>=(const join_iterator& other) const
            noexcept (requires{{
                store.template visit<join_compare::ge>(*this, other)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                store.template visit<join_compare::ge>(*this, other)
            } -> meta::convertible_to<bool>;})
        {
            return group() > other.group() || (
                group() == other.group() &&
                store.template visit<join_compare::ge>(*this, other)
            );
        }

        [[nodiscard]] constexpr bool operator>(const join_iterator& other) const
            noexcept (requires{{
                store.template visit<join_compare::gt>(*this, other)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                store.template visit<join_compare::gt>(*this, other)
            } -> meta::convertible_to<bool>;})
        {
            return group() > other.group() || (
                group() == other.group() &&
                store.template visit<join_compare::gt>(*this, other)
            );
        }
    };

    /* Forward iterators over `join` ranges consist of an inner union holding either
    an iterator over the current range or a trivial iterator over a single element.
    If all ranges use the same begin and end types, then the overall joined iterators
    will also match. */
    template <meta::lvalue T, typename>
    struct _make_join_iterator;
    template <meta::lvalue T, size_t... Is>
    struct _make_join_iterator<T, std::index_sequence<Is...>> {
    private:
        using last = decltype(join_forward{
            std::declval<T>().template arg<sizeof...(Is) - 1>()
        }.begin());

    public:
        static constexpr bool common =
            std::same_as<typename last::begin_type, typename last::end_type>;
        using begin = join_iterator<
            T,
            decltype(join_forward{std::declval<T>().template arg<Is>()}.begin())...
        >;
        using end = std::conditional_t<common, begin, NoneType>;
    };
    template <meta::lvalue T>
    struct make_join_iterator {
        T container;

    private:
        using indices = meta::unqualify<T>::indices;
        using type = _make_join_iterator<T, indices>;

    public:
        using begin_type = type::begin;
        using end_type = type::end;

        [[nodiscard]] constexpr begin_type begin()
            noexcept (requires{{begin_type{
                .container = std::addressof(container),
                .iters = {join_forward{container.template arg<0>()}.begin()}
            }} noexcept;})
            requires (requires{{begin_type{
                .container = std::addressof(container),
                .iters = {join_forward{container.template arg<0>()}.begin()}
            }};})
        {
            return {
                .container = std::addressof(container),
                .iters = {join_forward{container.template arg<0>()}.begin()}
            };
        }

        [[nodiscard]] constexpr end_type end()
            noexcept (requires{{end_type{
                .container = std::addressof(container),
                .iters = {join_forward{container.template arg<indices::size() - 1>()}.end()}
            }} noexcept;})
            requires (type::common && requires{{end_type{
                .container = std::addressof(container),
                .iters = {join_forward{container.template arg<indices::size() - 1>()}.end()}
            }};})
        {
            return {
                .container = std::addressof(container),
                .iters = {join_forward{container.template arg<indices::size() - 1>()}.end()}
            };
        }

        [[nodiscard]] constexpr end_type end() noexcept requires (!type::common) {
            return None;
        }
    };
    template <typename T>
    make_join_iterator(T&) -> make_join_iterator<T&>;

    /* If all of the input ranges happen to be reverse iterable, then the joined range
    will alos be reverse iterable, and the rbegin and rend iterators will match if all
    of the input ranges use the same rbegin and rend types. */
    template <meta::lvalue T, typename>
    struct _make_join_reversed;
    template <meta::lvalue T, size_t... Is>
    struct _make_join_reversed<T, std::index_sequence<Is...>> {
    private:
        using last = decltype(join_reverse{
            std::declval<T>().template arg<sizeof...(Is) - 1>()
        }.begin());

    public:
        static constexpr bool common =
            std::same_as<typename last::begin_type, typename last::end_type>;
        using begin = join_iterator<
            T,
            decltype(join_reverse{std::declval<T>().template arg<Is>()}.begin())...
        >;
        using end = std::conditional_t<common, begin, NoneType>;
    };
    template <meta::lvalue T>
        requires (range_reverse_iterable<typename meta::unqualify<T>::argument_types>)
    struct make_join_reversed {
        T container;

    private:
        using indices = meta::unqualify<T>::indices;
        using type = _make_zip_reversed<T, indices>;

    public:
        using begin_type = type::begin;
        using end_type = type::end;

        [[nodiscard]] constexpr begin_type begin()
            noexcept (requires{{typename type::begin{
                .container = std::addressof(container),
                .iters = {join_reverse{container.template arg<0>()}.begin()}
            }} noexcept;})
            requires (requires{{typename type::begin{
                .container = std::addressof(container),
                .iters = {join_reverse{container.template arg<0>()}.begin()}
            }};})
        {
            return {
                .container = std::addressof(container),
                .iters = {join_reverse{container.template arg<0>()}.begin()}
            };
        }

        constexpr type::end end()
            noexcept (requires{{typename type::end{
                .container = std::addressof(container),
                .iters = {join_reverse{container.template arg<indices::size() - 1>()}.begin()}
            }} noexcept;})
            requires (requires{{typename type::end{
                .container = std::addressof(container),
                .iters = {join_reverse{container.template arg<indices::size() - 1>()}.begin()}
            }};})
        {
            return {
                .container = std::addressof(container),
                .iters = {join_reverse{container.template arg<indices::size() - 1>()}.begin()}
            };
        }
    };
    template <typename T>
    make_join_reversed(T&) -> make_join_reversed<T&>;

    template <typename Sep>
    constexpr size_t join_sep_size = 1;
    template <meta::is_void Sep>
    constexpr size_t join_sep_size<Sep> = 0;
    template <meta::range Sep> requires (meta::tuple_like<Sep>)
    constexpr size_t join_sep_size<Sep> = meta::tuple_size<Sep>;
    template <meta::unpack Sep>
        requires (meta::tuple_like<Sep> && meta::tuple_like<meta::yield_type<Sep>>)
    constexpr size_t join_sep_size<Sep> =
        meta::tuple_size<Sep> * meta::tuple_size<meta::yield_type<Sep>>;

    template <typename T, typename Sep>
    constexpr size_t join_arg_size = 1;
    template <meta::range T, typename Sep> requires (meta::tuple_like<T>)
    constexpr size_t join_arg_size<T, Sep> = meta::tuple_size<T>;
    template <meta::unpack T, typename Sep>
        requires (meta::tuple_like<T> && meta::tuple_like<meta::yield_type<T>>)
    constexpr size_t join_arg_size<T, Sep> =
        meta::tuple_size<meta::yield_type<T>> * meta::tuple_size<T> +
        join_sep_size<Sep> * (meta::tuple_size<T> - (meta::tuple_size<T> > 0));

    template <typename Sep, typename... A>
    constexpr size_t join_tuple_size = (
        join_arg_size<A, Sep> +
        ... +
        (join_sep_size<Sep> * (sizeof...(A) - (sizeof...(A) > 0))
    ));

    template <typename Sep, typename... A> requires (join_concept<Sep, A...>)
    struct join_storage {
        [[no_unique_address]] impl::ref<Sep> sep;
        [[no_unique_address]] impl::basic_tuple<A...> args;
    };

    template <meta::is_void Sep, typename... A> requires (join_concept<Sep, A...>)
    struct join_storage<Sep, A...> {
        [[no_unique_address]] impl::basic_tuple<A...> args;
    };

    /* Joined ranges store an arbitrary set of argument types as well as a possible
    separator to insert between each one.  The arguments are not required to be ranges,
    and will be inserted as single elements at their respective position if not.  If
    any of the arguments are ranges, then they will be iterated over in sequence. */
    template <typename Sep, typename... A> requires (join_concept<Sep, A...>)
    struct join {
        using separator_type = Sep;
        using argument_types = meta::pack<A...>;
        using size_type = size_t;
        using index_type = ssize_t;
        using indices = std::make_index_sequence<sizeof...(A)>;
        using ranges = range_indices<A...>;

        [[no_unique_address]] join_storage<Sep, A...> m_storage;

        /* Perfectly forward the stored separator, if one was given. */
        template <typename Self> requires (meta::not_void<Sep>)
        [[nodiscard]] constexpr decltype(auto) sep(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_storage.sep);
        }

        /* Perfectly forward the I-th joined argument. */
        template <size_t I, typename Self> requires (I < sizeof...(A))
        [[nodiscard]] constexpr decltype(auto) arg(this Self&& self) noexcept {
            return (std::forward<Self>(self).m_storage.args.template get<I>());
        }

    private:
        static constexpr bool sized = (
            (!meta::range<Sep> || (meta::has_size<Sep> && (
                !meta::unpack<Sep> || meta::tuple_like<meta::yield_type<Sep>>
            ))) &&
            ... &&
            (!meta::range<A> || (meta::has_size<A> && (
                !meta::unpack<A> || meta::tuple_like<meta::yield_type<A>>
            )))
        );
        static constexpr bool tuple_like = (
            (!meta::range<Sep> || meta::tuple_like<Sep>) &&
            ... &&
            (!meta::range<A> || meta::tuple_like<A>)
        );
        static constexpr bool sequence_like = (meta::sequence<Sep> || ... || meta::sequence<A>);

        template <size_t J>
        static constexpr size_type unpack_size =
            meta::tuple_size<meta::yield_type<meta::unpack_type<J, A...>>>;

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

        template <typename T>
        constexpr size_type size_impl(const T& arg) const
            noexcept (
                !meta::range<T> ||
                (meta::nothrow::size_returns<size_type, const T&> && (
                    !meta::unpack<T> ||
                    (meta::tuple_like<meta::yield_type<T>> && (
                        !meta::range<Sep> ||
                        requires{{size_type(sep().size())} noexcept;}
                    ))
                ))
            )
            requires (
                !meta::range<T> ||
                (meta::size_returns<size_type, const T&> && (
                    !meta::unpack<T> ||
                    (meta::tuple_like<meta::yield_type<T>> && (
                        !meta::range<Sep> ||
                        requires{{size_type(sep().size())};}
                    ))
                ))
            )
        {
            if constexpr (meta::unpack<T>) {
                constexpr size_type element_size = meta::tuple_size<meta::yield_type<T>>;
                size_type n = arg.size();
                if constexpr (meta::is_void<Sep>) {
                    return n * element_size;
                } else if constexpr (meta::range<Sep>) {
                    return n * element_size + (n - (n > 0)) * size_type(sep().size());
                } else {
                    return n * element_size + (n - (n > 0));
                }
            } else if constexpr (meta::range<T>) {
                return arg.size();
            } else {
                return 1;
            }
        }

        template <size_t... Is> requires (sizeof...(Is) == indices::size())
        constexpr size_type _size(std::index_sequence<Is...>) const
            noexcept (requires{{(size_impl(arg<Is>()) + ... + 0)} noexcept;})
            requires (meta::is_void<Sep> && requires{{(size_impl(arg<Is>()) + ... + 0)};})
        {
            return (size_impl(arg<Is>()) + ... + 0);
        }

        template <size_t... Is> requires (sizeof...(Is) == indices::size())
        constexpr size_type _size(std::index_sequence<Is...>) const
            noexcept (requires{{(
                size_impl(arg<Is>()) +
                ... +
                (size_type(sep().size()) * (sizeof...(Is) - (sizeof...(Is) > 0)))
            )} noexcept;})
            requires (meta::range<Sep> && requires{{(
                size_impl(arg<Is>()) +
                ... +
                (size_type(sep().size()) * (sizeof...(Is) - (sizeof...(Is) > 0)))
            )};})
        {
            return (
                size_impl(arg<Is>()) +
                ... +
                (size_type(sep().size()) * (sizeof...(Is) - (sizeof...(Is) > 0)))
            );
        }

        template <size_t... Is> requires (sizeof...(Is) == indices::size())
        constexpr size_type _size(std::index_sequence<Is...>) const
            noexcept (requires{{(
                size_impl(arg<Is>()) +
                ... +
                (sizeof...(Is) - (sizeof...(Is) > 0))
            )} noexcept;})
            requires (meta::not_void<Sep> && !meta::range<Sep> && requires{{(
                size_impl(arg<Is>()) +
                ... +
                (sizeof...(Is) - (sizeof...(Is) > 0))
            )};})
        {
            return (
                size_impl(arg<Is>()) +
                ... +
                (sizeof...(Is) - (sizeof...(Is) > 0))
            );
        }

    public:
        /* If any of the input ranges are `sequence` types, then it's possible that
        `.size()` could throw a runtime error due to type erasure, which acts as a
        SFINAE barrier with respect to the underlying container.  In order to handle
        this, a joined range consisting of one or more sequences will expose the same
        `has_size()` accessor as the sequences themselves, and will return their
        logical conjunction. */
        [[nodiscard]] static constexpr bool has_size() noexcept requires (sized && !sequence_like) {
            return true;
        }

        /* If any of the input ranges are `sequence` types, then it's possible that
        `.size()` could throw a runtime error due to type erasure, which acts as a
        SFINAE barrier with respect to the underlying container.  In order to handle
        this, a joined range consisting of one or more sequences will expose the same
        `has_size()` accessor as the sequences themselves, and will return their
        logical conjunction. */
        [[nodiscard]] constexpr bool has_size() const noexcept requires (sized && sequence_like) {
            return _has_size(ranges{});
        }

        /* The overall size of the joined range as an unsigned integer.  This is only
        enabled if all of the arguments are either sized ranges or non-range inputs,
        and the separator is either absent, a scalar value, or a sized range.  If one
        or more unpacking operators are used, then the unpacked element type must be
        either scalar or tuple-like, so that the size can be computed in constant time.
        Otherwise, this method will fail to compile.

        Note that if any of the input ranges are `sequence` types (which may or may not
        be sized), then this method may throw an `IndexError` if `sequence.has_size()`
        evaluates to `false` for any of the sequences.  This is a consequence of type
        erasure on the underlying container, which acts as a SFINAE barrier for the
        compiler, and may be worked around via the `has_size()` method.  If that method
        returns `true`, then this method will never throw. */
        [[nodiscard]] static constexpr size_type size() noexcept requires (tuple_like) {
            return join_tuple_size<Sep, A...>;
        }

        /* The overall size of the joined range as an unsigned integer.  This is only
        enabled if all of the arguments are either sized ranges or non-range inputs,
        and the separator is either absent, a scalar value, or a sized range.  If one
        or more unpacking operators are used, then the unpacked element type must be
        either scalar or tuple-like, so that the size can be computed in constant time.
        Otherwise, this method will fail to compile.

        Note that if any of the input ranges are `sequence` types (which may or may not
        be sized), then this method may throw an `IndexError` if `sequence.has_size()`
        evaluates to `false` for any of the sequences.  This is a consequence of type
        erasure on the underlying container, which acts as a SFINAE barrier for the
        compiler, and may be worked around via the `has_size()` method.  If that method
        returns `true`, then this method will never throw. */
        [[nodiscard]] constexpr size_type size() const
            noexcept (requires{{_size(indices{})} noexcept;})
            requires (!tuple_like && sized)
        {
            return _size(indices{});
        }

        /* The overall size of the joined range as a signed integer.  This is only
        enabled if all of the arguments are either sized ranges or non-range inputs,
        and the separator is either absent, a scalar value, or a sized range. */
        [[nodiscard]] constexpr index_type ssize() const
            noexcept (requires{{index_type(size())} noexcept;})
            requires (sized)
        {
            return index_type(size());
        }

        /* True if the joined range contains no elements.  False otherwise. */
        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{begin() == end()} noexcept;})
        {
            return begin() == end();
        }

    private:
        template <size_t I, typename Self> requires (I < join_sep_size<Sep>)
        constexpr decltype(auto) get_sep_impl(this Self&& self)
            noexcept ((meta::unpack<Sep> && requires{
                {std::forward<Self>(self).sep().
                    template get<I / meta::tuple_size<meta::yield_type<Sep>>>().
                    template get<I % meta::tuple_size<meta::yield_type<Sep>>>()
                } noexcept;
            }) || (!meta::unpack<Sep> && meta::range<Sep> && requires{
                {std::forward<Self>(self).sep().template get<I>()} noexcept;
            }) || (!meta::range<Sep> && requires{
                {std::forward<Self>(self).sep()} noexcept;
            }))
            requires ((meta::unpack<Sep> && requires{
                {std::forward<Self>(self).sep().
                    template get<I / meta::tuple_size<meta::yield_type<Sep>>>().
                    template get<I % meta::tuple_size<meta::yield_type<Sep>>>()
                };
            }) || (!meta::unpack<Sep> && meta::range<Sep> && requires{
                {std::forward<Self>(self).sep().template get<I>()};
            }) || (!meta::range<Sep> && requires{
                {std::forward<Self>(self).sep()};
            }))
        {
            if constexpr (meta::unpack<Sep>) {
                return (std::forward<Self>(self).sep().
                    template get<I / meta::tuple_size<meta::yield_type<Sep>>>().
                    template get<I % meta::tuple_size<meta::yield_type<Sep>>>()
                );
            } else if constexpr (meta::range<Sep>) {
                return (std::forward<Self>(self).sep().template get<I>());
            } else {
                return (std::forward<Self>(self).sep());
            }
        }

        /* Index `I` matches a separator. */
        template <size_t I, size_t J, typename Self> requires (I < join_sep_size<Sep>)
        constexpr decltype(auto) get_sep(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).template get_sep_impl<I>()} noexcept;})
            requires (requires{{std::forward<Self>(self).template get_sep_impl<I>()};})
        {
            return std::forward<Self>(self).template get_sep_impl<I>();
        }

        template <size_t I, size_t J, typename Self>
        static constexpr size_t quotient = 
            (join_sep_size<Sep> + I) / (join_sep_size<Sep> + unpack_size<J>);

        template <size_t I, size_t J, typename Self>
        static constexpr size_t remainder =
            (join_sep_size<Sep> + I) % (join_sep_size<Sep> + unpack_size<J>);

        /* Index `I` matches the `J`-th joined argument, accounting for separators. */
        template <size_t I, size_t J, typename Self>
            requires (I < join_arg_size<meta::unpack_type<J, A...>, Sep>)
        constexpr decltype(auto) _get(this Self&& self)
            noexcept ((join_unpack<Self, J> && (
                (
                    remainder<I, J, Self> < join_sep_size<Sep> &&
                    requires{{std::forward<Self>(self).
                        template get_sep_impl<remainder<I, J, Self>>()
                    } noexcept;}
                ) || (
                    remainder<I, J, Self> >= join_sep_size<Sep> &&
                    requires{{std::forward<Self>(self).template arg<J>().
                        template get<quotient<I, J, Self>>().
                        template get<remainder<I, J, Self> - join_sep_size<Sep>>()
                    } noexcept;}
                )
            )) || (!join_unpack<Self, J> && !join_broadcast<Self, J> && requires{
                {std::forward<Self>(self).template arg<J>().template get<I>()} noexcept;
            }) || (join_broadcast<Self, J> && requires{
                {std::forward<Self>(self).template arg<J>()} noexcept;
            }))
            requires ((join_unpack<Self, J> && (
                (
                    remainder<I, J, Self> < join_sep_size<Sep> &&
                    requires{{std::forward<Self>(self).
                        template get_sep_impl<remainder<I, J, Self>>()
                    };}
                ) || (
                    remainder<I, J, Self> >= join_sep_size<Sep> &&
                    requires{{std::forward<Self>(self).template arg<J>().
                        template get<quotient<I, J, Self>>().
                        template get<remainder<I, J, Self> - join_sep_size<Sep>>()
                    };}
                )
            )) || (!join_unpack<Self, J> && !join_broadcast<Self, J> && requires{
                {std::forward<Self>(self).template arg<J>().template get<I>()} noexcept;
            }) || (join_broadcast<Self, J> && requires{
                {std::forward<Self>(self).template arg<J>()} noexcept;
            }))
        {
            if constexpr (join_unpack<Self, J>) {
                constexpr size_t r = remainder<I, J, Self>;
                if constexpr (r < join_sep_size<Sep>) {
                    return (std::forward<Self>(self).template get_sep_impl<r>());
                } else {
                    return (std::forward<Self>(self).template arg<J>().
                        template get<quotient<I, J, Self>>().
                        template get<r - join_sep_size<Sep>>()
                    );
                }
            } else if constexpr (!join_broadcast<Self, J>) {
                return (std::forward<Self>(self).template arg<J>().template get<I>());
            } else {
                return (std::forward<Self>(self).template arg<J>());
            }
        }

        /* Index `I` does NOT match the `J-th` joined argument. */
        template <size_t I, size_t J, typename Self>
            requires (I >= join_arg_size<meta::unpack_type<J, A...>, Sep>)
        constexpr decltype(auto) _get(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).template get_sep<
                I - join_arg_size<meta::unpack_type<J, A...>, Sep>,
                J + 1
            >()} noexcept;})
            requires (requires{{std::forward<Self>(self).template get_sep<
                I - join_arg_size<meta::unpack_type<J, A...>, Sep>,
                J + 1
            >()};})
        {
            return (std::forward<Self>(self).template get_sep<
                I - join_arg_size<meta::unpack_type<J, A...>, Sep>,
                J + 1
            >());
        }

        /* Index `I` does NOT match the current separator. */
        template <size_t I, size_t J, typename Self> requires (I >= join_sep_size<Sep>)
        constexpr decltype(auto) get_sep(this Self&& self)
            noexcept (requires{{
                std::forward<Self>(self).template _get<I - join_sep_size<Sep>, J>()
            } noexcept;})
            requires (requires{{
                std::forward<Self>(self).template _get<I - join_sep_size<Sep>, J>()
            };})
        {
            return (std::forward<Self>(self).template _get<I - join_sep_size<Sep>, J>());
        }

        constexpr size_type subscript_sep_size() const
            noexcept (
                !meta::range<Sep> ||
                (meta::unpack<Sep> && requires{{
                    sep().size() * meta::tuple_size<meta::yield_type<Sep>>
                } noexcept -> meta::nothrow::convertible_to<size_type>;}) ||
                (!meta::unpack<Sep> && requires{
                    {sep().size()} noexcept -> meta::nothrow::convertible_to<size_type>;
                })
            )
            requires (meta::not_void<Sep> && (
                !meta::range<Sep> ||
                (meta::unpack<Sep> && requires{{
                    sep().size() * meta::tuple_size<meta::yield_type<Sep>>
                } -> meta::convertible_to<size_type>;}) ||
                (!meta::unpack<Sep> && requires{
                    {sep().size()} -> meta::convertible_to<size_type>;
                })
            ))
        {
            if constexpr (meta::unpack<Sep>) {
                return sep().size() * meta::tuple_size<meta::yield_type<Sep>>;
            } else if constexpr (meta::range<Sep>) {
                return sep().size();
            } else {
                return 1;
            }
        }

        template <size_t J>
        constexpr size_type subscript_size() const
            noexcept (
                join_broadcast<const join&, J> ||
                (
                    join_unpack<const join&, J> &&
                    requires{
                        {arg<J>().size()} noexcept -> meta::nothrow::convertible_to<size_type>;
                    } && (meta::is_void<Sep> || requires{{subscript_sep_size()} noexcept;})
                ) || (
                    !join_unpack<const join&, J> &&
                    requires{
                        {arg<J>().size()} noexcept -> meta::nothrow::convertible_to<size_type>;
                    }
                )
            )
            requires (
                join_broadcast<const join&, J> ||
                (
                    join_unpack<const join&, J> &&
                    requires{
                        {arg<J>().size()} -> meta::convertible_to<size_type>;
                    } && (meta::is_void<Sep> || requires{{subscript_sep_size()};})
                ) || (
                    !join_unpack<const join&, J> &&
                    requires{
                        {arg<J>().size()} -> meta::convertible_to<size_type>;
                    }
                )
            )
        {
            if constexpr (join_unpack<const join&, J>) {
                size_type n = arg<J>().size();
                if constexpr (meta::not_void<Sep>) {
                    return n * unpack_size<J> + (n - (n > 0)) * subscript_sep_size();
                } else {
                    return n * unpack_size<J>;
                }
            } else if constexpr (!join_broadcast<const join&, J>) {
                return arg<J>().size();
            } else {
                return 1;
            }
        }

        template <typename Self> requires (meta::not_void<Sep>)
        constexpr impl::join_element<Self> subscript_sep(this Self&& self, size_type i)
            noexcept (
                (meta::unpack<Sep> && requires{
                    {
                        std::forward<Self>(self).sep()
                            [i / meta::tuple_size<meta::yield_type<Sep>>()]
                            [i % meta::tuple_size<meta::yield_type<Sep>>()]
                    } noexcept -> meta::nothrow::convertible_to<impl::join_element<Self>>;
                }) || (!meta::unpack<Sep> && meta::range<Sep> && requires{
                    {
                        std::forward<Self>(self).sep()[i]
                    } noexcept -> meta::nothrow::convertible_to<impl::join_element<Self>>;
                }) || (!meta::range<Sep> && requires{
                    {
                        std::forward<Self>(self).sep()
                    } noexcept -> meta::nothrow::convertible_to<impl::join_element<Self>>;
                })
            )
            requires (
                (meta::unpack<Sep> && requires{
                    {
                        std::forward<Self>(self).sep()
                            [i / meta::tuple_size<meta::yield_type<Sep>>()]
                            [i % meta::tuple_size<meta::yield_type<Sep>>()]
                    } -> meta::convertible_to<impl::join_element<Self>>;
                }) || (!meta::unpack<Sep> && meta::range<Sep> && requires{
                    {
                        std::forward<Self>(self).sep()[i]
                    } -> meta::convertible_to<impl::join_element<Self>>;
                }) || (!meta::range<Sep> && requires{
                    {
                        std::forward<Self>(self).sep()
                    } -> meta::convertible_to<impl::join_element<Self>>;
                })
            )
        {
            if constexpr (meta::unpack<Sep>) {
                constexpr size_type n = meta::tuple_size<meta::yield_type<Sep>>;
                return std::forward<Self>(self).sep()[i / n][i % n];
            } else if constexpr (meta::range<Sep>) {
                return std::forward<Self>(self).sep()[i];
            } else {
                return std::forward<Self>(self).sep();
            }
        }

        template <size_t J, typename Self> requires (J < sizeof...(A))
        constexpr impl::join_element<Self> subscript(this Self&& self, size_type i)
            noexcept (requires{{self.template subscript_size<J>()} noexcept;} && (
                meta::is_void<Sep> || requires{
                    {self.subscript_sep_size()} noexcept;
                    {std::forward<Self>(self).subscript_sep(i)} noexcept;
                }
            ) && (
                (
                    join_unpack<Self, J> && requires{{
                        std::forward<Self>(self).template arg<J>()[i][i]
                    } noexcept -> meta::nothrow::convertible_to<impl::join_element<Self>>;}
                ) || (
                    !join_unpack<Self, J> && !join_broadcast<Self, J> &&
                    requires{{
                        std::forward<Self>(self).template arg<J>()[i]
                    } noexcept -> meta::nothrow::convertible_to<impl::join_element<Self>>;}
                ) || (
                    join_broadcast<Self, J> &&
                    requires{{
                        std::forward<Self>(self).template arg<J>()
                    } noexcept -> meta::nothrow::convertible_to<impl::join_element<Self>>;}
                )
            ) && (
                J + 1 >= sizeof...(A) || requires{
                    {std::forward<Self>(self).template subscript<J + 1>(i)} noexcept;
                }
            ))
            requires (requires{{self.template subscript_size<J>()};} && (
                meta::is_void<Sep> || requires{
                    {self.subscript_sep_size()};
                    {std::forward<Self>(self).subscript_sep(i)};
                }
            ) && (
                (
                    join_unpack<Self, J> && requires{{
                        std::forward<Self>(self).template arg<J>()[i][i]
                    } -> meta::convertible_to<impl::join_element<Self>>;}
                ) || (
                    !join_unpack<Self, J> && !join_broadcast<Self, J> &&
                    requires{{
                        std::forward<Self>(self).template arg<J>()[i]
                    } -> meta::convertible_to<impl::join_element<Self>>;}
                ) || (
                    join_broadcast<Self, J> &&
                    requires{{
                        std::forward<Self>(self).template arg<J>()
                    } -> meta::convertible_to<impl::join_element<Self>>;}
                )
            ) && (
                J + 1 >= sizeof...(A) || requires{
                    {std::forward<Self>(self).template subscript<J + 1>(i)};
                }
            ))
        {
            size_type size = self.template subscript_size<J>();
            if (i < size) {
                if constexpr (join_unpack<Self, J>) {
                    if constexpr (meta::not_void<Sep>) {
                        size_type sep_size = self.subscript_sep_size();
                        i += sep_size;
                        size_type j = i / (sep_size + unpack_size<J>);
                        i %= (sep_size + unpack_size<J>);
                        if (i < sep_size) {
                            return std::forward<Self>(self).subscript_sep(i);
                        }
                        return std::forward<Self>(self).template arg<J>()[j][i - sep_size];
                    } else {
                        constexpr size_type n = unpack_size<J>;
                        return std::forward<Self>(self).template arg<J>()[i / n][i % n];
                    }
                } else if constexpr (!join_broadcast<Self, J>) {
                    return std::forward<Self>(self).template arg<J>()[i];
                } else {
                    return std::forward<Self>(self).template arg<J>();
                }
            }
            if constexpr (J + 1 < sizeof...(A)) {
                i -= size;
                if constexpr (meta::not_void<Sep>) {
                    size_type sep_size = self.subscript_sep_size();
                    if (i < sep_size) {
                        return std::forward<Self>(self).subscript_sep(i);
                    }
                    i -= sep_size;
                }
                return std::forward<Self>(self).template subscript<J + 1>(i);
            } else {
                throw IndexError();  // unreachable
            }
        }

    public:
        /* Access the `I`-th element of a tuple-like, joined range.  Non-range
        arguments will be forwarded according to the current cvref qualifications of
        the `join` range, while range arguments will be accessed using the provided
        index before forwarding.  If the index is invalid for one or more of the input
        ranges, then this method will fail to compile. */
        template <size_t I, typename Self> requires (tuple_like && I < join_tuple_size<Sep, A...>)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).template _get<I, 0>()} noexcept;})
            requires (requires{{std::forward<Self>(self).template _get<I, 0>()};})
        {
            return (std::forward<Self>(self).template _get<I, 0>());
        }

        /* Index into the joined range.  Non-range arguments will be forwarded
        according to the current cvref qualifications of the `join` range, while range
        arguments will be accessed using the provided index before forwarding.  If the
        index is not supported for one or more of the input ranges, then this method
        will fail to compile. */
        template <typename Self>
        [[nodiscard]] constexpr impl::join_element<Self> operator[](this Self&& self, size_type i)
            noexcept (requires{{std::forward<Self>(self).template subscript<0>(i)} noexcept;})
            requires (requires{{std::forward<Self>(self).template subscript<0>(i)};})
        {
            return std::forward<Self>(self).template subscript<0>(i);
        }
        
        /* Get a forward iterator over the joined range. */
        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{make_join_iterator{*this}.begin()} noexcept;})
            requires (requires{{make_join_iterator{*this}.begin()};})
        {
            return make_join_iterator{*this}.begin();
        }

        /* Get a forward iterator over the joined range. */
        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{make_join_iterator{*this}.begin()} noexcept;})
            requires (requires{{make_join_iterator{*this}.begin()};})
        {
            return make_join_iterator{*this}.begin();
        }

        /* Get a forward sentinel one past the end of the joined range. */
        [[nodiscard]] constexpr auto end()
            noexcept (requires{{make_join_iterator{*this}.end()} noexcept;})
            requires (requires{{make_join_iterator{*this}.end()};})
        {
            return make_join_iterator{*this}.end();
        }

        /* Get a forward sentinel one past the end of the joined range. */
        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{make_join_iterator{*this}.end()} noexcept;})
            requires (requires{{make_join_iterator{*this}.end()};})
        {
            return make_join_iterator{*this}.end();
        }

        /* Get a reverse iterator over the joined range. */
        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{make_join_reversed{*this}.begin()} noexcept;})
            requires (requires{{make_join_reversed{*this}.begin()};})
        {
            return make_join_reversed{*this}.begin();
        }

        /* Get a reverse iterator over the joined range. */
        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{make_join_reversed{*this}.begin()} noexcept;})
            requires (requires{{make_join_reversed{*this}.begin()};})
        {
            return make_join_reversed{*this}.begin();
        }

        /* Get a reverse sentinel one before the beginning of the joined range. */
        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{make_join_reversed{*this}.end()} noexcept;})
            requires (requires{{make_join_reversed{*this}.end()};})
        {
            return make_join_reversed{*this}.end();
        }

        /* Get a reverse sentinel one before the beginning of the joined range. */
        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{make_join_reversed{*this}.end()} noexcept;})
            requires (requires{{make_join_reversed{*this}.end()};})
        {
            return make_join_reversed{*this}.end();
        }
    };

}


/// TODO: maybe `join` should be able to accept multiple separators, which will be
/// applied to each respective level of unpacking.  So

/// join{".", "-"}([["a", "b"], ["c", "d"]]) -> "a-b.c-d"
/// join{"."}([["a", "b"], ["c", "d"]]) -> "ab.cd"


template <meta::not_rvalue Sep = void>
struct join {
private:
    template <typename... A>
    using container = impl::join<Sep, meta::remove_rvalue<A>...>;

    template <typename... A>
    using range = bertrand::range<container<A...>>;

public:
    [[no_unique_address]] Sep sep;

    template <typename Self, typename... A> requires (impl::join_concept<Sep, A...>)
    [[nodiscard]] constexpr auto operator()(this Self&& self, A&&... a)
        noexcept (requires{{range<A...>{container<A...>{.m_storage = {
            .sep = std::forward<Self>(self).sep,
            .args = {std::forward<A>(a)...}
        }}}} noexcept;})
        requires (requires{{range<A...>{container<A...>{.m_storage = {
            .sep = std::forward<Self>(self).sep,
            .args = {std::forward<A>(a)...}
        }}}};})
    {
        return range<A...>{container<A...>{.m_storage = {
            .sep = std::forward<Self>(self).sep,
            .args = {std::forward<A>(a)...}
        }}};
    }
};


template <meta::is_void V> requires (meta::not_rvalue<V>)
struct join<V> {
private:
    template <typename... A>
    using container = impl::join<void, meta::remove_rvalue<A>...>;

    template <typename... A>
    using range = bertrand::range<container<A...>>;

public:
    template <typename Self, typename... A> requires (impl::join_concept<void, A...>)
    [[nodiscard]] constexpr auto operator()(this Self&& self, A&&... a)
        noexcept (requires{{range<A...>{container<A...>{.m_storage = {
            .args = {std::forward<A>(a)...}
        }}}} noexcept;})
        requires (requires{{range<A...>{container<A...>{.m_storage = {
            .args = {std::forward<A>(a)...}
        }}}};})
    {
        return range<A...>{container<A...>{.m_storage = {
            .args = {std::forward<A>(a)...}
        }}};
    }
};


template <typename Sep>
join(Sep&&) -> join<meta::remove_rvalue<Sep>>;


//////////////////////
////    REPEAT    ////
//////////////////////


/// TODO: it may be possible to turn `repeat{}` ranges into common ranges, where
/// the begin and end iterators are the same type.


/// TODO: a static repeat count of 1 is identical to the original range, so that may
/// be worth optimizing.  Maybe the `repeat{}` adaptor should special-case that in the
/// call operator and just return the original range (or convert to a range if it was
/// not one already).


namespace impl {

    /* Repeat iterator implementations are tailored to the capabilities of the
    underlying iterator type.  The base specialization is chosen for forward-only
    iterators, which simply reset to the begin iterator once a full repetition has been
    completed. */
    template <meta::iterator Begin, meta::sentinel_for<Begin> End>
    struct repeat_iterator {
        using iterator_category = meta::iterator_category<Begin>;
        using difference_type = meta::iterator_difference_type<Begin>;
        using value_type = meta::iterator_value_type<Begin>;
        using reference = meta::iterator_reference_type<Begin>;
        using pointer = meta::iterator_pointer_type<Begin>;

        Begin begin;
        End end;
        size_t count = 0;  // repetition count
        Begin iter = begin;  // current iterator

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).iter} noexcept;})
            requires (requires{{*std::forward<Self>(self).iter};})
        {
            return (*std::forward<Self>(self).iter);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{meta::to_arrow(std::forward<Self>(self).iter)} noexcept;})
            requires (requires{{meta::to_arrow(std::forward<Self>(self).iter)};})
        {
            return meta::to_arrow(std::forward<Self>(self).iter);
        }

        constexpr repeat_iterator& operator++()
            noexcept (requires{
                {++iter} noexcept;
                {iter == end} noexcept;
                {iter = begin} noexcept;
            })
            requires (requires{
                {++iter};
                {iter == end};
                {iter = begin};
            })
        {
            ++iter;
            if (iter == end) {
                iter = begin;
                --count;
            }
            return *this;
        }

        [[nodiscard]] constexpr repeat_iterator operator++(int)
            noexcept (
                meta::nothrow::copyable<repeat_iterator> &&
                meta::has_preincrement<repeat_iterator&>
            )
            requires (meta::copyable<repeat_iterator> && meta::has_preincrement<repeat_iterator&>)
        {
            repeat_iterator tmp = *this;
            ++*this;
            return tmp;
        }

        [[nodiscard]] constexpr bool operator==(const repeat_iterator& other) const
            noexcept (requires{{iter == other.iter} noexcept;})
            requires (requires{{iter == other.iter};})
        {
            return count == other.count && iter == other.iter;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const repeat_iterator& self,
            NoneType
        ) noexcept {
            return self.count == 0;
        }

        [[nodiscard]] friend constexpr bool operator==(
            NoneType,
            const repeat_iterator& self
        ) noexcept {
            return self.count == 0;
        }

        [[nodiscard]] constexpr bool operator!=(const repeat_iterator& other) const
            noexcept (requires{{iter != other.iter} noexcept;})
            requires (requires{{iter != other.iter};})
        {
            return count != other.count || iter != other.iter;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const repeat_iterator& self,
            NoneType
        ) noexcept {
            return self.count != 0;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            NoneType,
            const repeat_iterator& self
        ) noexcept {
            return self.count != 0;
        }
    };

    /* Bidirectional iterators have to also store an iterator to the last item of the
    previous repetition so that it can decrement backwards across the boundary. */
    template <meta::bidirectional_iterator Begin, meta::sentinel_for<Begin> End>
    struct repeat_iterator<Begin, End> {
        using iterator_category = meta::iterator_category<Begin>;
        using difference_type = meta::iterator_difference_type<Begin>;
        using value_type = meta::iterator_value_type<Begin>;
        using reference = meta::iterator_reference_type<Begin>;
        using pointer = meta::iterator_pointer_type<Begin>;

        Begin begin;
        End end;
        size_t count = 0;  // repetition count
        Begin iter = begin;  // current iterator
        Begin last = begin;  // last iterator

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).iter} noexcept;})
            requires (requires{{*std::forward<Self>(self).iter};})
        {
            return (*std::forward<Self>(self).iter);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{meta::to_arrow(std::forward<Self>(self).iter)} noexcept;})
            requires (requires{{meta::to_arrow(std::forward<Self>(self).iter)};})
        {
            return meta::to_arrow(std::forward<Self>(self).iter);
        }

        constexpr repeat_iterator& operator++()
            noexcept (requires{
                {++iter} noexcept;
                {iter == end} noexcept;
                {iter = begin} noexcept;
            })
            requires (requires{
                {++iter};
                {iter == end};
                {iter = begin};
            })
        {
            ++iter;
            if (iter == end) {
                if (last == begin) {
                    --iter;
                    last = iter;
                }
                iter = begin;
                --count;
            }
            return *this;
        }

        [[nodiscard]] constexpr repeat_iterator operator++(int)
            noexcept (
                meta::nothrow::copyable<repeat_iterator> &&
                meta::has_preincrement<repeat_iterator&>
            )
            requires (meta::copyable<repeat_iterator> && meta::has_preincrement<repeat_iterator&>)
        {
            repeat_iterator tmp = *this;
            ++*this;
            return tmp;
        }

        constexpr repeat_iterator& operator--()
            noexcept (requires{
                {--iter} noexcept;
                {iter == begin} noexcept;
                {iter = last} noexcept;
            })
            requires (requires{
                {--iter};
                {iter == begin};
                {iter = last};
            })
        {
            if (iter == begin) {
                if (last == begin) {
                    --iter;
                } else {
                    iter = last;
                    ++count;
                }
            } else {
                --iter;
            }
            return *this;
        }

        [[nodiscard]] constexpr repeat_iterator operator--(int)
            noexcept (
                meta::nothrow::copyable<repeat_iterator> &&
                meta::has_predecrement<repeat_iterator&>
            )
            requires (meta::copyable<repeat_iterator> && meta::has_predecrement<repeat_iterator&>)
        {
            --*this;
            repeat_iterator tmp = *this;
            return tmp;
        }

        [[nodiscard]] constexpr bool operator==(const repeat_iterator& other) const
            noexcept (requires{{iter == other.iter} noexcept;})
            requires (requires{{iter == other.iter};})
        {
            return count == other.count && iter == other.iter;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const repeat_iterator& self,
            NoneType
        ) noexcept {
            return self.count == 0;
        }

        [[nodiscard]] friend constexpr bool operator==(
            NoneType,
            const repeat_iterator& self
        ) noexcept {
            return self.count == 0;
        }

        [[nodiscard]] constexpr bool operator!=(const repeat_iterator& other) const
            noexcept (requires{{iter != other.iter} noexcept;})
            requires (requires{{iter != other.iter};})
        {
            return count != other.count || iter != other.iter;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const repeat_iterator& self,
            NoneType
        ) noexcept {
            return self.count != 0;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            NoneType,
            const repeat_iterator& self
        ) noexcept {
            return self.count != 0;
        }
    };

    /* Random access iterators have to also store the size of the range and current
    index in order to map indices onto the repeated range. */
    template <meta::random_access_iterator Begin, meta::sentinel_for<Begin> End>
    struct repeat_iterator<Begin, End> {
        using iterator_category = meta::iterator_category<Begin>;
        using difference_type = meta::iterator_difference_type<Begin>;
        using value_type = meta::iterator_value_type<Begin>;
        using reference = meta::iterator_reference_type<Begin>;
        using pointer = meta::iterator_pointer_type<Begin>;

        Begin begin;
        End end;
        size_t count = 0;  // repetition count
        difference_type size = std::ranges::distance(begin, end);  // size of each repetition
        difference_type index = 0;  // index in current repetition
        Begin iter = begin;  // current iterator

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).iter} noexcept;})
            requires (requires{{*std::forward<Self>(self).iter};})
        {
            return (*std::forward<Self>(self).iter);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (requires{{meta::to_arrow(std::forward<Self>(self).iter)} noexcept;})
            requires (requires{{meta::to_arrow(std::forward<Self>(self).iter)};})
        {
            return meta::to_arrow(std::forward<Self>(self).iter);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, difference_type n)
            noexcept (requires{{std::forward<Self>(self).begin[(self.index + n) % self.size]} noexcept;})
            requires (requires{{std::forward<Self>(self).begin[(self.index + n) % self.size]};})
        {
            return (std::forward<Self>(self).begin[(self.index + n) % self.size]);
        }

        constexpr repeat_iterator& operator++()
            noexcept (requires{{iter = begin + index} noexcept;})
            requires (requires{{iter = begin + index};})
        {
            ++index;
            count -= index / size;
            index %= size;
            iter = begin + index;
            return *this;
        }

        [[nodiscard]] constexpr repeat_iterator operator++(int)
            noexcept (
                meta::nothrow::copyable<repeat_iterator> &&
                meta::has_preincrement<repeat_iterator&>
            )
            requires (meta::copyable<repeat_iterator> && meta::has_preincrement<repeat_iterator&>)
        {
            repeat_iterator tmp = *this;
            ++*this;
            return tmp;
        }

        [[nodiscard]] friend constexpr repeat_iterator operator+(
            const repeat_iterator& self,
            difference_type n
        )
            noexcept (requires{{repeat_iterator{
                .begin = self.begin,
                .end = self.end,
                .count = self.count - n,
                .size = self.size,
                .index = n % self.size,
                .iter = self.begin + n
            }} noexcept;})
            requires (requires{{repeat_iterator{
                .begin = self.begin,
                .end = self.end,
                .count = self.count - n,
                .size = self.size,
                .index = n % self.size,
                .iter = self.begin + n
            }};})
        {
            n += self.index;
            difference_type q = n / self.size;
            n %= self.size;
            if (n < 0) {
                --q;
                n += self.size;
            }
            return {
                .begin = self.begin,
                .end = self.end,
                .count = self.count - q,
                .size = self.size,
                .index = n,
                .iter = self.begin + n
            };
        }

        [[nodiscard]] friend constexpr repeat_iterator operator+(
            difference_type n,
            const repeat_iterator& self
        )
            noexcept (requires{{repeat_iterator{
                .begin = self.begin,
                .end = self.end,
                .count = self.count - n,
                .size = self.size,
                .index = n % self.size,
                .iter = self.begin + n
            }} noexcept;})
            requires (requires{{repeat_iterator{
                .begin = self.begin,
                .end = self.end,
                .count = self.count - n,
                .size = self.size,
                .index = n % self.size,
                .iter = self.begin + n
            }};})
        {
            n += self.index;
            difference_type q = n / self.size;
            n %= self.size;
            if (n < 0) {
                --q;
                n += self.size;
            }
            return {
                .begin = self.begin,
                .end = self.end,
                .count = self.count - q,
                .size = self.size,
                .index = n,
                .iter = self.begin + n
            };
        }

        constexpr repeat_iterator& operator+=(difference_type n)
            noexcept (requires{{iter = begin + index} noexcept;})
            requires (requires{{iter = begin + index};})
        {
            index += n;
            difference_type q = index / size;
            index %= size;
            if (index < 0) {
                --q;
                index += size;
            }
            count -= q;
            iter = begin + index;
            return *this;
        }

        constexpr repeat_iterator& operator--()
            noexcept (requires{{iter = begin + index} noexcept;})
            requires (requires{{iter = begin + index};})
        {
            --index;
            bool neg = index < 0;
            count += neg;
            index += size * neg;
            iter = begin + index;
            return *this;
        }

        [[nodiscard]] constexpr repeat_iterator operator--(int)
            noexcept (
                meta::nothrow::copyable<repeat_iterator> &&
                meta::has_predecrement<repeat_iterator&>
            )
            requires (meta::copyable<repeat_iterator> && meta::has_predecrement<repeat_iterator&>)
        {
            repeat_iterator tmp = *this;
            --*this;
            return tmp;
        }

        [[nodiscard]] constexpr repeat_iterator operator-(difference_type n) const
            noexcept (requires{{repeat_iterator{
                .begin = begin,
                .end = end,
                .count = count - n,
                .size = size,
                .index = n % size,
                .iter = begin + n
            }} noexcept;})
            requires (requires{{repeat_iterator{
                .begin = begin,
                .end = end,
                .count = count - n,
                .size = size,
                .index = n % size,
                .iter = begin + n
            }};})
        {
            n = index - n;
            difference_type q = n / size;
            n %= size;
            if (n < 0) {
                --q;
                n += size;
            }
            return {
                .begin = begin,
                .end = end,
                .count = count - q,
                .size = size,
                .index = n,
                .iter = begin + n
            };
        }

        [[nodiscard]] constexpr difference_type operator-(
            const repeat_iterator& other
        ) const noexcept {
            return (count - other.count) * size + (index - other.index);
        }

        constexpr repeat_iterator& operator-=(difference_type n)
            noexcept (requires{{iter = begin + index} noexcept;})
            requires (requires{{iter = begin + index};})
        {
            index = index - n;
            difference_type q = index / size;
            index %= size;
            if (index < 0) {
                --q;
                index += size;
            }
            count -= q;
            iter = begin + index;
            return *this;
        }

        [[nodiscard]] constexpr bool operator<(const repeat_iterator& other) const
            noexcept (requires{{iter < other.iter} noexcept;})
            requires (requires{{iter < other.iter};})
        {
            return count > other.count || (count == other.count && iter < other.iter);
        }

        [[nodiscard]] constexpr bool operator<=(const repeat_iterator& other) const
            noexcept (requires{{iter <= other.iter} noexcept;})
            requires (requires{{iter <= other.iter};})
        {
            return count > other.count || (count == other.count && iter <= other.iter);
        }

        [[nodiscard]] constexpr bool operator==(const repeat_iterator& other) const
            noexcept (requires{{iter == other.iter} noexcept;})
            requires (requires{{iter == other.iter};})
        {
            return count == other.count && iter == other.iter;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const repeat_iterator& self,
            NoneType
        ) noexcept {
            return self.count == 0;
        }

        [[nodiscard]] friend constexpr bool operator==(
            NoneType,
            const repeat_iterator& self
        ) noexcept {
            return self.count == 0;
        }

        [[nodiscard]] constexpr bool operator!=(const repeat_iterator& other) const
            noexcept (requires{{iter != other.iter} noexcept;})
            requires (requires{{iter != other.iter};})
        {
            return count != other.count || iter != other.iter;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const repeat_iterator& self,
            NoneType
        ) noexcept {
            return self.count != 0;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            NoneType,
            const repeat_iterator& self
        ) noexcept {
            return self.count != 0;
        }

        [[nodiscard]] constexpr bool operator>=(const repeat_iterator& other) const
            noexcept (requires{{iter >= other.iter} noexcept;})
            requires (requires{{iter >= other.iter};})
        {
            return count < other.count || (count == other.count && iter >= other.iter);
        }

        [[nodiscard]] constexpr bool operator>(const repeat_iterator& other) const
            noexcept (requires{{iter > other.iter} noexcept;})
            requires (requires{{iter > other.iter};})
        {
            return count < other.count || (count == other.count && iter > other.iter);
        }
    };

    template <typename B, typename E, typename... rest>
    repeat_iterator(B, E, rest...) -> repeat_iterator<B, E>; 

    /* If the repetition count is known at compile time, then we can emit an optimized
    range that retains tuple-like access.  Otherwise, tuple inputs will lose their
    original structure. */
    template <meta::not_rvalue C, Optional<size_t> N>
        requires (meta::iterable<C> || meta::tuple_like<C>)
    struct repeat {
        using type = C;
        using size_type = size_t;
        using index_type = ssize_t;

    private:
        static constexpr size_type static_count = N == None ? 0 : N.__value.template get<1>();

        [[no_unique_address]] impl::ref<type> m_range;
        size_type m_count = static_count;

        [[nodiscard]] constexpr size_type base_size() const
            noexcept (
                meta::nothrow::has_size<meta::as_const_ref<type>> ||
                (!meta::has_size<meta::as_const_ref<type>> && meta::tuple_like<type>)
            )
        {
            if constexpr (meta::has_size<meta::as_const_ref<type>>) {
                return std::ranges::size(value());
            } else {
                return meta::tuple_size<type>;
            }
        }

    public:
        [[nodiscard]] constexpr repeat(meta::forward<type> range)
            noexcept (requires{{impl::ref<type>(std::forward<type>(range))} noexcept;})
            requires (N != None && requires{{impl::ref<type>(std::forward<type>(range))};})
        :
            m_range(std::forward<type>(range))
        {}

        [[nodiscard]] constexpr repeat(meta::forward<type> range, size_type count)
            noexcept (requires{{impl::ref<type>(std::forward<type>(range))} noexcept;})
            requires (N == None && requires{{impl::ref<type>(std::forward<type>(range))};})
        :
            m_range(std::forward<type>(range)),
            m_count(count)
        {}

        /* Perfectly forward the underlying container according to the repeated range's
        current cvref qualifications. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_range);
        }

        /* Indirectly access a member of the underlying container. */
        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{meta::to_arrow(value())} noexcept;})
            requires (requires{{meta::to_arrow(value())};})
        {
            return meta::to_arrow(value());
        }

        /* Indirectly access a member of the underlying container. */
        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{meta::to_arrow(value())} noexcept;})
            requires (requires{{meta::to_arrow(value())};})
        {
            return meta::to_arrow(value());
        }

        /* The repetition count for the repeated range. */
        [[nodiscard]] constexpr size_type count() const noexcept {
            if constexpr (N != None) {
                return static_count;
            } else {
                return m_count;
            }
        }

        /* The total number of elements that will be included in the repeated range, as
        an unsigned integer. */
        [[nodiscard]] constexpr size_type size() const
            noexcept (
                (N != None && static_count == 0) ||
                requires{{count() * base_size()} noexcept;}
            )
            requires (
                (N != None && static_count == 0) ||
                requires{{count() * base_size()};}
            )
        {
            if constexpr (N != None && static_count == 0) {
                return 0;
            } else {
                return count() * base_size();
            }
        }

        /* The total number of elements that will be included in the repeated range, as
        a signed integer. */
        [[nodiscard]] constexpr index_type ssize() const
            noexcept (
                (N != None && static_count == 0) ||
                requires{{count() * index_type(base_size())} noexcept;}
            )
            requires (
                (N != None && static_count == 0) ||
                requires{{count() * index_type(base_size())};}
            )
        {
            if constexpr (N != None && static_count == 0) {
                return 0;
            } else {
                return count() * index_type(base_size());
            }
        }

        /* True if the repeated range has zero elements, which can occur when either
        the underlying container is empty or the repetition count is zero.  False
        otherwise. */
        [[nodiscard]] constexpr bool empty() const
            noexcept (
                (N != None && static_count == 0) ||
                meta::nothrow::has_empty<meta::as_const_ref<type>> ||
                (!meta::has_empty<meta::as_const_ref<type>> && meta::tuple_like<type>)
            )
            requires (
                (N != None && static_count == 0) ||
                meta::has_empty<meta::as_const_ref<type>> ||
                meta::tuple_like<type>
            )
        {
            if constexpr (N != None && static_count == 0) {
                return true;
            } else if constexpr (meta::has_empty<meta::as_const_ref<type>>) {
                return std::ranges::empty(value());
            } else {
                return meta::tuple_size<type> > 0;
            }
        }

        /* Maintain tuple-like access as long as the repetition count is known at
        compile time and the underlying container is a tuple. */
        template <size_type I, typename Self> requires (N != None && meta::tuple_like<type>)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{meta::unpack_tuple<I % meta::tuple_size<type>>(
                std::forward<Self>(self).value()
            )} noexcept;})
            requires (requires{{meta::unpack_tuple<I % meta::tuple_size<type>>(
                std::forward<Self>(self).value()
            )};})
        {
            /// NOTE: Python-style wraparound has already been applied by
            /// `range.get<I>()`.
            return (meta::unpack_tuple<I % meta::tuple_size<type>>(
                std::forward<Self>(self).value()
            ));
        }

        /* Index into the repeated range as long as the underlying container is sized
        or tuple-like. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, size_type i)
            noexcept (requires{{impl::range_subscript(
                std::forward<Self>(self).value(),
                i % self.base_size()
            )} noexcept;})
            requires (requires{{impl::range_subscript(
                std::forward<Self>(self).value(),
                i % self.base_size()
            )};})
        {
            /// NOTE: Python-style wraparound has already been applied by `range[i]`.
            return (impl::range_subscript(
                std::forward<Self>(self).value(),
                i % self.base_size()
            ));
        }

        /* Get a forward iterator over the repeated range. */
        template <typename Self>
        [[nodiscard]] constexpr auto begin(this Self& self)
            noexcept (requires{
                {impl::make_range_iterator{self.value()}.begin()} -> meta::random_access_iterator;
            } && (meta::has_size<meta::as_const_ref<type>> || meta::tuple_like<type>) ?
                requires{{repeat_iterator{
                    impl::make_range_iterator{self.value()}.begin(),
                    impl::make_range_iterator{self.value()}.end(),
                    self.count(),
                    index_type(self.base_size())
                }} noexcept;} :
                requires{{repeat_iterator{
                    impl::make_range_iterator{self.value()}.begin(),
                    impl::make_range_iterator{self.value()}.end(),
                    self.count()
                }} noexcept;}
            )
            requires (requires{{repeat_iterator{
                impl::make_range_iterator{self.value()}.begin(),
                impl::make_range_iterator{self.value()}.end(),
                self.count()
            }};})
        {
            if constexpr (
                meta::random_access_iterator<
                    decltype(impl::make_range_iterator{self.value()}.begin())
                > && (meta::has_size<meta::as_const_ref<type>> || meta::tuple_like<type>)
            ) {
                /// NOTE: if the underlying iterator is random access and the range has
                /// a definite size, then we can avoid an extra
                /// `std::ranges::distance()` call by passing the size directly to the
                /// repeat iterator.
                return repeat_iterator{
                    impl::make_range_iterator{self.value()}.begin(),
                    impl::make_range_iterator{self.value()}.end(),
                    self.count(),
                    index_type(self.base_size()),
                };
            } else {
                return repeat_iterator{
                    impl::make_range_iterator{self.value()}.begin(),
                    impl::make_range_iterator{self.value()}.end(),
                    self.count()
                };
            }
        }

        /* Get a forward sentinel for the end of the repeated range. */
        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }

        /* Get a forward iterator over the repeated range. */
        template <typename Self>
        [[nodiscard]] constexpr auto rbegin(this Self& self)
            noexcept (requires{
                {impl::make_range_reversed{self.value()}.begin()} -> meta::random_access_iterator;
            } && (meta::has_ssize<meta::as_const_ref<type>> || meta::tuple_like<type>) ?
                requires{{repeat_iterator{
                    impl::make_range_reversed{self.value()}.begin(),
                    impl::make_range_reversed{self.value()}.end(),
                    self.count(),
                    index_type(self.base_size())
                }} noexcept;} :
                requires{{repeat_iterator{
                    impl::make_range_reversed{self.value()}.begin(),
                    impl::make_range_reversed{self.value()}.end(),
                    self.count()
                }} noexcept;}
            )
            requires (requires{{repeat_iterator{
                impl::make_range_reversed{self.value()}.begin(),
                impl::make_range_reversed{self.value()}.end(),
                self.count()
            }};})
        {
            if constexpr (requires{
                {impl::make_range_reversed{self.value()}.begin()} -> meta::random_access_iterator;
            } && (meta::has_size<meta::as_const_ref<type>> || meta::tuple_like<type>)) {
                /// NOTE: if the underlying iterator is random access and the range has
                /// a definite size, then we can avoid an extra
                /// `std::ranges::distance()` call by passing the size directly to the
                /// repeat iterator.
                return repeat_iterator{
                    impl::make_range_reversed{self.value()}.begin(),
                    impl::make_range_reversed{self.value()}.end(),
                    self.count(),
                    index_type(self.base_size()),
                };
            } else {
                return repeat_iterator{
                    impl::make_range_reversed{self.value()}.begin(),
                    impl::make_range_reversed{self.value()}.end(),
                    self.count()
                };
            }
        }

        /* Get a reverse sentinel for the end of the repeated range. */
        [[nodiscard]] static constexpr NoneType rend() noexcept { return {}; }
    };

}


/// TODO: the repeat{}() call operator should be able to take ranges or scalar values,
/// and will treat any non-ranges as single elements.  It should also accept a variadic
/// list of these, which will be concatenated and then repeated as a unit.  Probably,
/// the best way to do this is to implement `join{}` before `repeat{}`, and then have
/// the `repeat{}` call operator automatically join the arguments before passing the
/// result to `impl::repeat{}`.

/// -> join{} and zip{} are the basic entry points for translating generic argument
/// lists into ranges.  If either is invoked with a single scalar value, then they
/// degenerate to the same operation, and will simply return a contiguous iterator
/// with a single element.




/* A function object that repeats the contents of an incoming container a given number
of times, concatenating the results into a single range.

This class comes in 2 flavors: one that encodes the repetition count at compile time
and retains tuple-like access to the repeated range, and the other that only knows the
repetition count at run time.  Other than tuple-like access, both cases behave the
same, and produce the same results when iterated over or indexed. */
template <Optional<size_t> N = None>
struct repeat {
    static constexpr size_t count = N.__value.template get<1>();

private:
    template <typename C>
    using container = impl::repeat<meta::remove_rvalue<C>, N>;

    template <typename C>
    using range = bertrand::range<container<C>>;

public:
    /* Invoking the repeat adaptor produces a corresponding range type. */
    template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
    [[nodiscard]] constexpr range<C> operator()(C&& c)
        noexcept (requires{{range<C>{container<C>{std::forward<C>(c)}}} noexcept;})
        requires (requires{{range<C>{container<C>{std::forward<C>(c)}}};})
    {
        return range<C>{container<C>{std::forward<C>(c)}};
    }
};


/* A function object that repeats the contents of an incoming container a given number
of times, concatenating the results into a single range.

This class comes in 2 flavors: one that encodes the repetition count at compile time
and retains tuple-like access to the repeated range, and the other that only knows the
repetition count at run time.  Other than tuple-like access, both cases behave the
same, and produce the same results when iterated over or indexed. */
template <>
struct repeat<None> {
    size_t count;

private:
    template <typename C>
    using container = impl::repeat<meta::remove_rvalue<C>, None>;

    template <typename C>
    using range = bertrand::range<container<C>>;

public:
    /* When compiled in debug mode, the constructor ensures that the repetition count
    is always non-negative, and throws an `IndexError` otherwise. */
    template <meta::integer T>
    [[nodiscard]] constexpr repeat(T n) noexcept (!DEBUG || meta::unsigned_integer<T>) :
        count(size_t(n))
    {
        if constexpr (DEBUG && meta::signed_integer<T>) {
            if (n < 0) {
                throw IndexError("repetition count must be non-negative");
            }
        }
    }

    /* Invoking the repeat adaptor produces a corresponding range type. */
    template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
    [[nodiscard]] constexpr range<C> operator()(C&& c)
        noexcept (requires{{range<C>{container<C>{std::forward<C>(c), count}}} noexcept;})
        requires (requires{{range<C>{container<C>{std::forward<C>(c), count}}};})
    {
        return range<C>{container<C>{std::forward<C>(c), count}};
    }
};


template <meta::integer T>
repeat(T n) -> repeat<None>;


/////////////////////
////    SLICE    ////
/////////////////////


/// TODO: maybe slice{} should also be able to accept boolean masks as step sizes,
/// which would standardize that case in the range indexing operator.


/// TODO: zip is necessary for complex slicing involving function predicates, so that's
/// a blocker for the rest of the range interface atm.


namespace impl {

    template <meta::lvalue Self>
    struct slice_iterator;

    template <typename C>
    concept slice_container = (meta::iterable<C> && meta::has_ssize<C>) || meta::tuple_like<C>;

    template <typename F, typename C>
    concept slice_predicate =
        meta::lvalue<C> && slice_container<C> && requires(
            F f,
            impl::range_begin<C> it,
            impl::range_begin<meta::as_const_ref<C>> c_it
        ) {
            { std::forward<F>(f)(*it) } -> meta::convertible_to<bool>;
            { std::forward<F>(f)(*c_it) } -> meta::convertible_to<bool>;
        };

    template <typename F, typename C>
    concept nothrow_slice_predicate =
        meta::lvalue<C> && slice_container<C> && requires(
            F f,
            impl::range_begin<C> it,
            impl::range_begin<meta::as_const_ref<C>> c_it
        ) {
            { std::forward<F>(f)(*it) } noexcept -> meta::nothrow::convertible_to<bool>;
            { std::forward<F>(f)(*c_it) } noexcept -> meta::nothrow::convertible_to<bool>;
        };

    template <typename T, typename C>
    concept slice_param = meta::None<T> || meta::integer<T> || slice_predicate<T, C>;

    /* A normalized set of slice indices that can be used to initialize a proper slice
    range.  An instance of this class must be provided to the `impl::slice`
    constructor, and is usually produced by the `bertrand::slice{...}.normalize(ssize)`
    helper method in the case of integer indices.  Containers that allow non-integer
    indices can construct an instance of this within their own `operator[](slice)`
    method to provide custom indexing, if needed. */
    struct slice_indices {
        ssize_t start = 0;
        ssize_t stop = 0;
        ssize_t step = 1;

        [[nodiscard]] constexpr size_t size() const noexcept { return size_t(ssize()); }
        [[nodiscard]] constexpr ssize_t ssize() const noexcept {
            ssize_t bias = step + (step < 0) - (step > 0);
            ssize_t length = (stop - start + bias) / step;
            return length * (length > 0);
        }
        [[nodiscard]] constexpr bool empty() const noexcept { return ssize() == 0; }
    };

    template <meta::lvalue C> requires (slice_container<C>)
    constexpr bool slice_from_tail = false;
    template <meta::lvalue C>
        requires (slice_container<C> && (
            requires(C c) {{
                impl::make_range_reversed{c}.begin()
            } -> meta::explicitly_convertible_to<impl::range_begin<C>>;} ||
            requires(C c) {{
                impl::make_range_reversed{c}.begin().base()
            } -> meta::explicitly_convertible_to<impl::range_begin<C>>;}
        ))
    constexpr bool slice_from_tail<C> = true;

    template <meta::lvalue C> requires (slice_container<C>)
    constexpr bool slice_nothrow_from_tail = false;
    template <meta::lvalue C>
        requires (slice_container<C> && (
            requires(C c) {{
                impl::make_range_reversed{c}.begin()
            } noexcept -> meta::nothrow::explicitly_convertible_to<impl::range_begin<C>>;} || (
                !requires(C c) {{
                    impl::make_range_reversed{c}.begin()
                } -> meta::explicitly_convertible_to<impl::range_begin<C>>;} &&
                requires(C c) {{
                    impl::make_range_reversed{c}.begin().base()
                } noexcept -> meta::nothrow::explicitly_convertible_to<impl::range_begin<C>>;}
            )
        ))
    constexpr bool slice_nothrow_from_tail<C> =
        (meta::nothrow::has_ssize<C> || (!meta::has_ssize<C> && meta::tuple_like<C>)) &&
        requires(C c) {
            {impl::make_range_reversed{c}.begin()} noexcept -> meta::nothrow::has_preincrement;
        };

    /* Get an iterator to the given index of an iterable or tuple-like container with a
    known size.  Note that the index is assumed to have been already normalized via
    `impl::normalize_index()` or some other means.

    This will attempt to choose the most efficient method of obtaining the
    iterator, depending on the characteristics of the container and the index.

        1.  If the iterator type is random access, then a `begin()` iterator will be
            advanced to the index in constant time.
        2.  If the iterator type is bidirectional, and reverse iterators are
            convertible to forward iterators or posses a `base()` method that returns
            a forward iterator (as is the case for `std::reverse_iterator` instances),
            and the index is closer to the end than it is to the beginning, then a
            reverse iterator will be obtained and advanced to the index before being
            converted to a forward iterator.
        3.  Otherwise, a `begin()` iterator will be obtained and advanced to the index
            using a series of increments.

    The second case ensures that as long as the preconditions are met, the worst-case
    time complexity of this operation is `O(n/2)`.  Without it, the complexity rises to
    `O(n)`.

    Implementing this operation as a free method allows it to be abstracted over any
    container (which is important for the `slice_iterator` class) and greatly
    simplifies the implementation of custom `at()` methods for user-defined types,
    which will apply the same optimizations automatically. */
    template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
    [[nodiscard]] constexpr auto at(C& container, ssize_t index)
        noexcept (
            requires{{impl::make_range_iterator{container}.begin()} noexcept;} && (
            meta::random_access_iterator<impl::range_begin<C>> ?
                meta::nothrow::has_iadd<impl::range_begin<C>, ssize_t> :
                (!slice_from_tail<C&> || slice_nothrow_from_tail<C&>) &&
                meta::nothrow::has_preincrement<impl::range_begin<C>>
            )
        )
        requires (meta::random_access_iterator<impl::range_begin<C>> ?
            meta::has_iadd<impl::range_begin<C>, ssize_t> :
            meta::has_preincrement<impl::range_begin<C>>
        )
    {
        using wrapped = impl::range_begin<C>;

        // if the iterator supports random access, then we can just jump to the
        // start index in constant time.
        if constexpr (meta::random_access_iterator<wrapped>) {
            wrapped it = impl::make_range_iterator{container}.begin();
            it += index;
            return it;

        // otherwise, obtaining a begin iterator requires a series of increments
        // or decrements, depending on the capabilities of the range and the
        // position of the start index.
        } else {
            // if a reverse iterator is available and convertible to a forward
            // iterator, and the start index is closer to the end than it is to
            // beginning, then we can start from the end to minimize iterations.
            if constexpr (impl::slice_from_tail<C&>) {
                ssize_t size;
                if constexpr (meta::has_ssize<C&>) {
                    size = std::ranges::ssize(container);
                } else {
                    size = ssize_t(meta::tuple_size<C&>);
                }
                if (index >= ((size + 1) / 2)) {
                    auto it = impl::make_range_reversed{container}.begin();
                    for (ssize_t i = size - index; i-- > 0;) {
                        ++it;
                    }
                    if constexpr (meta::explicitly_convertible_to<decltype((it)), wrapped>) {
                        return wrapped(it);
                    } else {
                        ++it;  // it.base() trails the current element by 1
                        return wrapped(it.base());
                    }
                }
            }

            // start from the beginning and advance until the start index
            wrapped it = impl::make_range_iterator{container}.begin();
            for (ssize_t i = 0; i < index; ++i) {
                ++it;
            }
            return it;
        }
    }

    /// TODO: start and stop can be functions that take the container's yield type
    /// (both mutable and immutable) and return a boolean.  The start index will
    /// resolve to the first element that returns true, and the stop index will
    /// resolve to the first element that returns true after the start index.
    /// If the step size is given as a function, then the slice will include all the
    /// elements that meet the step condition, which allows slices to act like
    /// std::views::filter(), without need for a separate `where` operator.

    /// TODO: range predicates are complicated, since they may visit unions and
    /// decompose tuples.  That will be hard to account for in the slicing
    /// ecosystem, but I might as well start here.  That will become more robust
    /// once I implement comprehensions, which will have to do this stuff anyway.



    /// TODO: no changes are necessary to the base `slice` specialization, since it
    /// will only apply when all of the indices are integers.

    /* An adaptor for a container that causes `range<impl::slice<C, Step>>` to iterate
    over only a subset of the container according to Python-style slicing semantics.
    A range of that form will be generated by calling the public `slice{}` helper
    directly, using it to index a supported container type, or by including it in a
    range comprehension.

    The `Step` parameter represents the integer type of the step size for the slice,
    which is used to optimize the iteration logic for the slices that are guaranteed
    to have a positive step size.  This is true for any unsigned integer type as well
    as an initial slice index of `None`, which translates to `size_t` in this context.
    If the step size is signed and the underlying iterator is not random access, then
    an extra branch will be added to the core loop to check whether the iterator must
    be incremented or decremented, depending on the sign of the step size.  This is a
    niche optimization for forward-only input ranges, but is completely transparent to
    the user, and ensures zero overhead in almost all cases. */
    template <slice_container C, slice_param<C> Start, slice_param<C> Stop, slice_param<C> Step>
    struct slice {
        using type = meta::remove_rvalue<C>;  // TODO: rvalues are removed before this point
        using start_type = meta::remove_rvalue<Start>;  // TODO: same ^
        using stop_type = meta::remove_rvalue<Stop>;
        using step_type = meta::remove_rvalue<Step>;
        using size_type = size_t;
        using index_type = ssize_t;

    private:
        [[no_unique_address]] impl::ref<type> ref;
        impl::slice_indices indices;
        ssize_t _size = indices.ssize();

        constexpr ssize_t container_size() const noexcept {
            if constexpr (meta::has_ssize<type>) {
                return std::ranges::ssize(value());
            } else {
                return ssize_t(meta::tuple_size<type>);
            }
        }

    public:
        template <meta::slice S>
        [[nodiscard]] constexpr slice(meta::forward<C> c, S&& s)
            requires (requires{
                { std::forward<S>(s).start } -> std::same_as<Start>;
                { std::forward<S>(s).stop } -> std::same_as<Stop>;
                { std::forward<S>(s).step } -> std::same_as<Step>;
            })
        :
            ref(std::forward<C>(c)),
            indices(s.normalize(container_size())),
            _size(indices.ssize())
        {
            if (indices.step == 0) {
                throw ValueError("step size cannot be zero");
            }
        }

        /* Perfectly forward the underlying container according to the slice's current
        cvref qualifications. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept {
            return (*std::forward<Self>(self).ref);
        }

        /* Indirectly access a member of the underlying container. */
        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{meta::to_arrow(value())} noexcept;})
            requires (requires{{meta::to_arrow(value())};})
        {
            return meta::to_arrow(value());
        }

        /* Indirectly access a member of the underlying container. */
        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{meta::to_arrow(value())} noexcept;})
            requires (requires{{meta::to_arrow(value())};})
        {
            return meta::to_arrow(value());
        }

        /* The normalized start index for the slice, as a signed integer.  This
        represents the first element that will be included in the slice, assuming it is
        not empty. */
        [[nodiscard]] constexpr ssize_t start() const noexcept { return indices.start; }

        /* The normalized stop index for the slice, as a signed integer.  Elements at
        or past this index will not be included in the slice, leading to a Python-style
        half-open interval. */
        [[nodiscard]] constexpr ssize_t stop() const noexcept { return indices.stop; }

        /* The normalized step size for the slice, as a signed integer.  This is always
        non-zero, and is positive for forward slices and negative for reverse slices.
        The last included index is given by `start + step * ssize`, assuming the slice
        is not empty. */
        [[nodiscard]] constexpr ssize_t step() const noexcept { return indices.step; }

        /* The total number of elements that will be included in the slice, as an
        unsigned integer. */
        [[nodiscard]] constexpr size_t size() const noexcept { return size_t(_size); }

        /* The total number of elements that will be included in the slice, as a signed
        integer. */
        [[nodiscard]] constexpr ssize_t ssize() const noexcept { return _size; }

        /* True if the slice contains no elements.  False otherwise. */
        [[nodiscard]] constexpr bool empty() const noexcept { return _size == 0; }

        /* Integer indexing operator.  Accepts a single signed integer and retrieves
        the corresponding element from the underlying container after multiplying by
        the step size and adding the start bias.  */
        template <typename Self>
        constexpr decltype(auto) operator[](this Self&& self, size_t i)
            noexcept (requires{{impl::range_subscript(
                std::forward<Self>(self).value(),
                size_t(ssize_t(i) * self.step() + self.start())
            )} noexcept;})
            requires (requires{{impl::range_subscript(
                std::forward<Self>(self).value(),
                size_t(ssize_t(i) * self.step() + self.start())
            )};})
        {
            return (impl::range_subscript(
                std::forward<Self>(self).value(),
                size_t(ssize_t(i) * self.step() + self.start())
            ));
        }

        /* Get an iterator to the start of the slice.  Incrementing the iterator will
        advance it by the given step size. */
        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{impl::slice_iterator<slice&>{*this}} noexcept;})
            requires (requires{{impl::slice_iterator<slice&>{*this}};})
        {
            return impl::slice_iterator<slice&>{*this};
        }

        /* Get an iterator to the start of the slice.  Incrementing the iterator will
        advance it by the given step size. */
        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{impl::slice_iterator<const slice&>{*this}} noexcept;})
            requires (requires{{impl::slice_iterator<const slice&>{*this}};})
        {
            return impl::slice_iterator<const slice&>{*this};
        }

        /* Return a sentinel representing the end of the slice. */
        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }
    };

    /* A specialization of `slice<...>` that is chosen if any of the start, stop, and
    step types are predicate functions rather than integer indices.  This prevents the
    slice from computing the indices ahead of time, and therefore yields an unsized
    range. */
    template <slice_container C, slice_param<C> Start, slice_param<C> Stop, slice_param<C> Step>
        requires (slice_predicate<Start, C> || slice_predicate<Stop, C> || slice_predicate<Step, C>)
    struct slice<C, Start, Stop, Step> {
        using type = meta::remove_rvalue<C>;
        using start_type = meta::remove_rvalue<Start>;
        using stop_type = meta::remove_rvalue<Stop>;
        using step_type = meta::remove_rvalue<Step>;
        using size_type = size_t;
        using index_type = ssize_t;

    private:
        [[no_unique_address]] impl::ref<type> ref;
        [[no_unique_address]] impl::ref<start_type> _start;
        [[no_unique_address]] impl::ref<stop_type> _stop;
        [[no_unique_address]] impl::ref<step_type> _step;

    public:
        template <meta::slice S>
        [[nodiscard]] constexpr slice(meta::forward<C> c, S&& s)
            requires (requires{
                { std::forward<S>(s).start } -> std::same_as<Start>;
                { std::forward<S>(s).stop } -> std::same_as<Stop>;
                { std::forward<S>(s).step } -> std::same_as<Step>;
            })
        :
            ref(std::forward<C>(c)),
            _start(std::forward<S>(s).start),
            _stop(std::forward<S>(s).stop),
            _step(std::forward<S>(s).step)
        {
            if constexpr (meta::integer<step_type>) {
                if (_step == 0) {
                    throw ValueError("slice step size cannot be zero");
                }
            }
        }


        /// TODO: this specialization would basically do all the same stuff, but would
        /// not have a definite size, and would evaluate the predicates lazily within
        /// the iterator.

    };

    template <slice_container C, meta::slice S>
    slice(C&&, S&& s) -> slice<
        meta::remove_rvalue<C>,
        decltype((std::forward<S>(s).start)),
        decltype((std::forward<S>(s).stop)),
        decltype((std::forward<S>(s).step))
    >;

    /// TODO: this will need specializations to account for predicate-based slices,
    /// which don't have predefined indices.
    /// -> If only the start index is a predicate, then I can continue using the faster
    /// size-based iterator, but just provide the start index in the constructor.
    /// -> If the stop index or step size are predicates, then an index-based
    /// approach will not work.  In that case, I will continue iterating until a
    /// predicate returns true or we reach the end of the range for a stop predicate.
    /// Step predicates cause us not to skip any elements, and only include those where
    /// the predicate returns true, until the stop index or predicate is reached.



    /* The overall slice iterator initializes to the start index of the slice, and
    maintains a pointer to the original slice object, whose indices it can access.
    Comparisons against other instances of the same type equate to comparisons between
    their current indices, and equality comparisons against the `None` sentinel bound
    the overall slice iteration.

    If the wrapped iterator is a random access iterator, then each increment of the
    slice iterator will equate to an `iter += step` operation on the wrapped iterator,
    which is expected to handle negative step sizes naturally.  Otherwise, a choice
    must be made between a series of `++iter` or `--iter` operations depending on the
    sign of the step size, which requires an extra branch in the core loop (unless it
    can be optimized out). */
    template <meta::lvalue Self>
    struct slice_iterator {
        using wrapped = impl::range_begin<decltype((std::declval<Self>().value()))>;
        using iterator_category = std::iterator_traits<wrapped>::iterator_category;
        using difference_type = std::iterator_traits<wrapped>::difference_type;
        using value_type = std::iterator_traits<wrapped>::value_type;
        using reference = std::iterator_traits<wrapped>::reference;
        using pointer = std::iterator_traits<wrapped>::pointer;

    private:
        using step_type = meta::unqualify<Self>::step_type;
        static constexpr bool unsigned_step =
            meta::None<step_type> || meta::unsigned_integer<step_type>;
        static constexpr bool bidirectional = meta::bidirectional_iterator<wrapped>;
        static constexpr bool random_access = meta::random_access_iterator<wrapped>;

        meta::as_pointer<Self> slice = nullptr;
        ssize_t size = 0;
        wrapped iter;

        [[nodiscard]] constexpr slice_iterator(
            meta::as_pointer<Self> slice,
            ssize_t size,
            wrapped&& iter
        )
            noexcept (meta::nothrow::movable<wrapped>)
        :
            slice(slice),
            size(size),
            iter(std::move(iter))
        {}

    public:
        [[nodiscard]] constexpr slice_iterator() = default;
        [[nodiscard]] constexpr slice_iterator(Self self)
            noexcept (
                (unsigned_step || bidirectional) &&
                requires{{impl::at(slice->value(), self.start())} noexcept;}
            )
        :
            slice(std::addressof(self)),
            size(self.ssize()),
            iter(impl::at(slice->value(), self.start()))
        {
            if constexpr (!unsigned_step && !bidirectional) {
                if (self.step() < 0) {
                    if consteval {
                        throw ValueError(
                            "cannot iterate over a forward-only range using a slice "
                            "with negative step size"
                        );
                    } else {
                        throw ValueError(
                            "cannot iterate over a forward-only range using a slice "
                            "with negative step size: " + std::to_string(self.step())
                        );
                    }
                }
            }
        }

        [[nodiscard]] constexpr decltype(auto) operator*()
            noexcept (meta::nothrow::has_dereference<wrapped&>)
            requires (meta::has_dereference<wrapped&>)
        {
            return (*iter);
        }

        [[nodiscard]] constexpr decltype(auto) operator*() const
            noexcept (meta::nothrow::has_dereference<const wrapped&>)
            requires (meta::has_dereference<const wrapped&>)
        {
            return (*iter);
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{meta::to_arrow(iter)} noexcept;})
            requires (requires{{meta::to_arrow(iter)};})
        {
            return meta::to_arrow(iter);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{meta::to_arrow(iter)} noexcept;})
            requires (requires{{meta::to_arrow(iter)};})
        {
            return meta::to_arrow(iter);
        }

        [[nodiscard]] constexpr decltype(auto) operator[](difference_type i)
            noexcept (requires{{iter[i * slice->step()]} noexcept;})
            requires (requires{{iter[i * slice->step()]};})
        {
            return (iter[i * slice->step()]);
        }

        [[nodiscard]] constexpr decltype(auto) operator[](difference_type i) const
            noexcept (requires{{iter[i * slice->step()]} noexcept;})
            requires (requires{{iter[i * slice->step()]};})
        {
            return (iter[i * slice->step()]);
        }

        constexpr slice_iterator& operator++()
            noexcept(random_access ?
                meta::nothrow::has_iadd<wrapped, ssize_t> :
                meta::nothrow::has_preincrement<wrapped> &&
                (unsigned_step || meta::nothrow::has_predecrement<wrapped>)
            )
            requires (random_access ?
                meta::has_iadd<wrapped, ssize_t> :
                meta::has_preincrement<wrapped>
            )
        {
            --size;
            if (size > 0) {
                ssize_t step = slice->step();
                if constexpr (random_access) {
                    iter += step;
                } else if constexpr (unsigned_step) {
                    for (ssize_t i = 0; i < step; ++i) {
                        ++iter;
                    }
                } else {
                    if (step < 0) {
                        /// NOTE: because we check on construction, we will never
                        /// enter this branch unless `--iter` is well-formed.
                        for (ssize_t i = 0; i > step; --i) {
                            --iter;
                        }
                    } else {
                        for (ssize_t i = 0; i < step; ++i) {
                            ++iter;
                        }
                    }
                }
            }
            return *this;
        }

        [[nodiscard]] constexpr slice_iterator operator++(int)
            noexcept(
                meta::nothrow::copyable<slice_iterator> &&
                meta::nothrow::has_preincrement<slice_iterator>
            )
            requires (meta::copyable<slice_iterator> && meta::has_preincrement<slice_iterator>)
        {
            slice_iterator copy = *this;
            ++*this;
            return copy;
        }

        [[nodiscard]] friend constexpr slice_iterator operator+(
            const slice_iterator& self,
            difference_type i
        )
            noexcept (requires{{self.iter + i * self.slice->step()} noexcept -> meta::is<wrapped>;})
            requires (requires{{self.iter + i * self.slice->step()} -> meta::is<wrapped>;})
        {
            return {self.slice, self.size - i, self.iter + i * self.slice->step()};
        }

        [[nodiscard]] friend constexpr slice_iterator operator+(
            difference_type i,
            const slice_iterator& self
        )
            noexcept (requires{{self.iter + i * self.slice->step()} noexcept -> meta::is<wrapped>;})
            requires (requires{{self.iter + i * self.slice->step()} -> meta::is<wrapped>;})
        {
            return {self.slice, self.size - i, self.iter + i * self.slice->step()};
        }

        constexpr slice_iterator& operator+=(difference_type i)
            noexcept (requires{{iter += i * slice->step()} noexcept;})
            requires (requires{{iter += i * slice->step()};})
        {
            size -= i;
            iter += i * slice->step();
            return *this;
        }

        constexpr slice_iterator& operator--()
            noexcept(random_access ?
                meta::nothrow::has_isub<wrapped, ssize_t> :
                meta::nothrow::has_predecrement<wrapped> &&
                (unsigned_step || meta::nothrow::has_preincrement<wrapped>)
            )
            requires (random_access ?
                meta::has_isub<wrapped, ssize_t> :
                meta::has_predecrement<wrapped>
            )
        {
            ++size;
            if (size <= slice->ssize()) {
                ssize_t step = slice->step();
                if constexpr (random_access) {
                    iter -= step;
                } else if constexpr (unsigned_step) {
                    for (ssize_t i = 0; i < step; ++i) {
                        --iter;
                    }
                } else {
                    if (step < 0) {
                        for (ssize_t i = 0; i > step; --i) {
                            ++iter;
                        }
                    } else {
                        for (ssize_t i = 0; i < step; ++i) {
                            --iter;
                        }
                    }
                }
            }
            return *this;
        }

        [[nodiscard]] constexpr slice_iterator operator--(int)
            noexcept(
                meta::nothrow::copyable<slice_iterator> &&
                meta::nothrow::has_predecrement<slice_iterator>
            )
            requires (meta::copyable<slice_iterator> && meta::has_predecrement<slice_iterator>)
        {
            slice_iterator copy = *this;
            --*this;
            return copy;
        }

        [[nodiscard]] constexpr slice_iterator operator-(difference_type i) const
            noexcept (requires{{iter - i * slice->step()} noexcept -> meta::is<wrapped>;})
            requires (requires{{iter - i * slice->step()} -> meta::is<wrapped>;})
        {
            return {slice, size + i, iter - i * slice->step()};
        }

        [[nodiscard]] constexpr difference_type operator-(const slice_iterator& other) const
            noexcept (requires{{
                (iter - other.iter) / slice->step()
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (requires{{
                (iter - other.iter) / slice->step()
            } -> meta::convertible_to<difference_type>;})
        {
            return (iter - other.iter) / slice->step();
        }

        constexpr slice_iterator& operator-=(difference_type i)
            noexcept (requires{{iter -= i * slice->step()} noexcept;})
            requires (requires{{iter -= i * slice->step()};})
        {
            size += i;
            iter -= i * slice->step();
            return *this;
        }

        [[nodiscard]] constexpr bool operator==(const slice_iterator& other) const noexcept {
            return size == other.size;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const slice_iterator& self,
            NoneType
        ) noexcept {
            return self.size <= 0;
        }

        [[nodiscard]] friend constexpr bool operator==(
            NoneType,
            const slice_iterator& self
        ) noexcept {
            return self.size <= 0;
        }

        [[nodiscard]] constexpr bool operator!=(const slice_iterator& other) const noexcept {
            return size != other.size;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const slice_iterator& self,
            NoneType
        ) noexcept {
            return self.size > 0;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            NoneType,
            const slice_iterator& self
        ) noexcept {
            return self.size > 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const slice_iterator& other) const noexcept {
            /// NOTE: higher size means the iterator is closer to the start, and should
            /// compare less than the other iterator
            return other.size <=> size;
        }
    };

    /* A specialization of `slice_iterator` for slices where `stop` or `step` is a
    predicate function, which prevents the indices from being computed ahead of time.

    Note that a `start` predicate is not considered, since once it has been resolved,
    the other indices can be computed normally. */
    template <meta::lvalue Self> requires (
        requires(Self self) {{ self.stop() } -> slice_predicate<decltype((self.value()))>;} ||
        requires(Self self) {{ self.step() } -> slice_predicate<decltype((self.value()))>;}
    )
    struct slice_iterator<Self> {

        /// TODO: muy complicado.  If stop is a predicate, then I would apply it when
        /// comparing against `None`, and would otherwise maintain or size, which
        /// counts to the end of the container.  The index/size would still allow
        /// ordered comparisons between iterators.

        /// TODO: if the step size is a predicate, then it removes the random access
        /// capabilities, since we're no longer jumping by a fixed size.

    };

}


/* A helper class that encapsulates the indices for a Python-style slice operator.

This class can be used in one of 3 ways, depending on the capabilities of the container
it is meant to slice:

    1.  Invoking the slice indices with a compatible container will promote them into a
        `range` subclass that implements generic slicing semantics via the container's
        standard iterator interface.  This allows slices to be constructed for
        arbitrary containers via `slice{start, stop, step}(container)` syntax, which
        serves as an entry point into the monadic `range` interface.
    2.  If the container implements an `operator[]` that accepts a `slice` object
        directly, then it must return a `range` subclass that implements the proper
        slicing semantics for that container.  Usually, this will simply return the
        same type as (1), invoking the `slice` with the parent container.  This allows
        for possible customization, as well as a more natural
        `container[slice{start, stop, step}]` syntax.
    3.  If the container uses the `->*` comprehension operator (as is the case for all
        Bertrand containers),  then the slice indices can be piped with it to form more
        complex expressions, such as `container ->* slice{start, stop, step}`.  This
        will use a custom `operator[]` from (2) if available or fall back to (1)
        otherwise.

Note that in all 3 cases, the `slice` object does not do any iteration directly, and
must be promoted to a proper range to complete the slicing operation.  See
`impl::slice` for more details on the behavior of these ranges, and how they relate to
the overall `range` interface. */
template <
    meta::not_void Start = NoneType,
    meta::not_void Stop = NoneType,
    meta::not_void Step = NoneType
>
struct slice {
    using start_type = meta::remove_rvalue<Start>;
    using stop_type = meta::remove_rvalue<Stop>;
    using step_type = meta::remove_rvalue<Step>;
    using index_type = ssize_t;
    using indices = impl::slice_indices;

    [[no_unique_address]] start_type start;
    [[no_unique_address]] stop_type stop;
    [[no_unique_address]] step_type step;

    /// TODO: this slice object should also be able to be piped with other view
    /// operators?

    /* Normalize the provided indices against a container of a given size, returning a
    4-tuple with members `start`, `stop`, `step`, and `length` in that order, and
    supporting structured bindings.  If either of the original `start` or `stop`
    indices were given as negative values or `nullopt`, they will be normalized
    according to the size, and will be truncated to the nearest end if they are out
    of bounds.  `length` stores the total number of elements that will be included in
    the slice */
    [[nodiscard]] constexpr indices normalize(index_type size) const noexcept
        requires (
            ((meta::integer<start_type> && meta::has_signed<start_type>) || meta::None<start_type>) &&
            ((meta::integer<stop_type> && meta::has_signed<stop_type>) || meta::None<stop_type>) &&
            ((meta::integer<step_type> && meta::has_signed<step_type>) || meta::None<step_type>)
        )
    {
        indices result {
            .start = 0,
            .stop = size,
            .step = 1
        };

        // if no step size is given, then we can exclude negative step sizes from the
        // normalization logic
        if constexpr (meta::None<step_type>) {
            // normalize and truncate start
            if constexpr (meta::integer<start_type>) {
                result.start = index_type(start);
                result.start += size * (result.start < 0);
                if (result.start < 0) {
                    result.start = 0;
                } else {
                    result.start = size;
                }
            }

            // normalize and truncate stop
            if constexpr (meta::integer<stop_type>) {
                result.stop = index_type(stop);
                result.stop += size * (result.stop < 0);
                if (result.stop < 0) {
                    result.stop = 0;
                } else if (result.stop >= size) {
                    result.stop = size;
                }
            }

        // otherwise, the step size may be negative, which requires extra logic to
        // handle the wraparound and truncation correctly
        } else {
            result.step = index_type(step);
            bool sign = result.step < 0;

            // normalize and truncate start
            if constexpr (meta::None<start_type>) {
                result.start = (size - 1) * sign;  // neg: size - 1 | pos: 0
            } else {
                result.start = index_type(start);
                result.start += size * (result.start < 0);
                if (result.start < 0) {
                    result.start = -sign;  // neg: -1 | pos: 0
                } else if (result.start >= size) {
                    result.start = size - sign;  // neg: size - 1 | pos: size
                }
            }

            // normalize and truncate stop
            if constexpr (meta::None<stop_type>) {
                result.stop = size * !sign - sign;  // neg: -1 | pos: size
            } else {
                result.stop = index_type(stop);
                result.stop += size * (result.stop < 0);
                if (result.stop < 0) {
                    result.stop = -sign;  // neg: -1 | pos: 0
                } else if (result.stop >= size) {
                    result.stop = size - sign;  // neg: size - 1 | pos: size
                }
            }
        }

        return result;
    }

    /// TODO: also, there should be a special case where `start` and/or `stop` are
    /// function predicates that return true at the first element, and last element
    /// respectively.  That gets covered by the range() method and fallback case for
    /// the call operator as well.  The trick is that all of the tricky business is
    /// done in the impl::slice constructor, so that will need to be accounted for.

    /// -> Maybe this forces the slice to eagerly evaluate a begin iterator, which
    /// can simply be copied when the slice's `begin()` method is called.  This would
    /// reduce overhead slightly, at least.


    /* Promote slice consisting of only integers and/or `None` into a proper range
    subclass.  Fails to compile if the slice contains at least one non-integer
    value.

    This is identical to the fallback case for `operator()`, but is provided
    as a separate method in order to simplify custom slice operators for user-defined
    classes.  A basic implementation of such an operator could look something like
    this:
    
        ```
        struct Foo {
            // ...

            template <typename Start, typename Stop, typename Step>
            constexpr auto operator[](const slice<Start, Stop, Step>& s) const {
                return s.range(*this);
            }

            // ...
        };
        ```

    Note that such an operator does not need to accept integer indices, and can
    implement arbitrary conversion logic by mapping the non-integer indices onto
    integer indices, and then calling this method to obtain a proper range.  Once
    defined, this class's call operator will automatically delegate to the custom
    slice operator, bypassing the usual fallback behavior. */
    template <typename C>
    [[nodiscard]] constexpr auto range(C&& container) const
        requires (
            ((meta::iterable<C> && meta::has_ssize<C>) || meta::tuple_like<C>) &&
            (meta::None<start_type> || meta::integer<start_type>) &&
            (meta::None<stop_type> || meta::integer<stop_type>) &&
            (meta::None<step_type> || meta::integer<step_type>)
        )
    {
        return bertrand::range(impl::slice(std::forward<C>(container), *this));
    }

    /* Forwarding call operator.  Searches for an `operator[]` overload that matches
    the given slice types and returns that result, allowing containers to customize
    the type slice behavior. */
    template <typename Self, typename C>
    [[nodiscard]] constexpr decltype(auto) operator()(this Self&& self, C&& container)
        noexcept (requires{{std::forward<C>(container)[std::forward<Self>(self)]} noexcept;})
        requires (requires{{std::forward<C>(container)[std::forward<Self>(self)]};})
    {
        return (std::forward<C>(container)[std::forward<Self>(self)]);
    }

    /* Fallback call operator, which is chosen if no `operator[]` overload can be
    found, all of the indices are integer-like or none, and the container has a
    definite `ssize()`. */
    template <typename C>
    [[nodiscard]] constexpr auto operator()(C&& container) const
        requires (
            ((meta::iterable<C> && meta::has_ssize<C>) || meta::tuple_like<C>) &&
            (meta::integer<start_type> || meta::None<start_type>) &&
            (meta::integer<stop_type> || meta::None<stop_type>) &&
            (meta::integer<step_type> || meta::None<step_type>)
        )
    {
        return range(std::forward<C>(container));
    }
};


template <typename Start = NoneType, typename Stop = NoneType, typename Step = NoneType>
slice(Start&& = {}, Stop&& = {}, Step&& = {}) -> slice<
    meta::remove_rvalue<Start>,
    meta::remove_rvalue<Stop>,
    meta::remove_rvalue<Step>
>;


/////////////////////
////    SPLIT    ////
/////////////////////




/// TODO: partition<N>{n/f}, which returns Optional([prev, sep, next])?
/// -> just handle this via the `split{}` customization interface, which includes
/// the following options:
///     - peek, which allows the groups to overlap
///     - until, which ignores groups after some index or predicate returns true.
///     - drop_sep, which omits the separator from the output.
/// These tags would be constexpr variables and can possibly be combined with the `|`
/// operator


//////////////////////
////    SAMPLE    ////
//////////////////////


/// TODO: `sample` extracts a random sample of elements from a range, and takes
/// a standardized random_device as an optional parameter (defaulting to a mersenne
/// twister).



///////////////////////
////    REVERSE    ////
///////////////////////


/// TODO: swap() operators for all ranges and all public range adaptors.


/// TODO: if the adapted container for reverse() is a sequence, then I may need to
/// emit has_size() and a size() method that can possibly throw.


namespace impl {

    /* An adaptor for a container that causes `range<impl::reversed<C>>` to reverse
    iterate over the container `C` instead of forward iterating.  This equates to
    swapping all of the `begin()` and `end()` methods with their reversed counterparts,
    and modifying the indexing logic to map index `i` to index `-i - 1`, which
    triggers Python-style wraparound. */
    template <meta::not_rvalue C> requires (meta::reverse_iterable<C> || meta::tuple_like<C>)
    struct reverse {
        using type = C;
        using size_type = size_t;
        using index_type = ssize_t;

    private:
        [[no_unique_address]] impl::ref<type> m_range;

    public:
        [[nodiscard]] constexpr reverse(meta::forward<type> range)
            noexcept (requires{{impl::ref<type>(std::forward<type>(range))} noexcept;})
            requires (requires{{impl::ref<type>(std::forward<type>(range))};})
        :
            m_range(std::forward<type>(range))
        {}

        /* Perfectly forward the underlying container according to the reversed range's
        current cvref qualifications. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_range);
        }

        /* Swap the underlying containers between two reversed ranges. */
        constexpr void swap(reverse& other)
            noexcept (requires{{m_range.swap(other.m_range)} noexcept;})
            requires (requires{{m_range.swap(other.m_range)};})
        {
            m_range.swap(other.m_range);
        }

        /* Indirectly access a member of the underlying container. */
        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{meta::to_arrow(value())} noexcept;})
            requires (requires{{meta::to_arrow(value())};})
        {
            return meta::to_arrow(value());
        }

        /* Indirectly access a member of the underlying container. */
        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{meta::to_arrow(value())} noexcept;})
            requires (requires{{meta::to_arrow(value())};})
        {
            return meta::to_arrow(value());
        }

        /* The total number of elements in the reversed range, as an unsigned
        integer. */
        [[nodiscard]] constexpr size_type size() const
            noexcept (meta::nothrow::size_returns<size_type, meta::as_const_ref<type>> || (
                !meta::size_returns<size_type, meta::as_const_ref<type>> &&
                meta::tuple_like<type>
            ))
            requires (
                meta::size_returns<size_type, meta::as_const_ref<type>> ||
                meta::tuple_like<type>
            )
        {
            if constexpr (meta::size_returns<size_type, meta::as_const_ref<type>>) {
                return std::ranges::size(value());
            } else {
                return meta::tuple_size<type>;
            }
        }

        /* The total number of elements in the reversed range, as a signed integer. */
        [[nodiscard]] constexpr index_type ssize() const
            noexcept (meta::nothrow::ssize_returns<index_type, meta::as_const_ref<type>> || (
                !meta::ssize_returns<index_type, meta::as_const_ref<type>> &&
                meta::tuple_like<type>
            ))
            requires (
                meta::ssize_returns<index_type, meta::as_const_ref<type>> ||
                meta::tuple_like<type>
            )
        {
            if constexpr (meta::ssize_returns<index_type, meta::as_const_ref<type>>) {
                return std::ranges::ssize(value());
            } else {
                return meta::to_signed(meta::tuple_size<type>);
            }
        }

        /* True if the reversed range contains zero elements.  False otherwise. */
        [[nodiscard]] constexpr bool empty() const
            noexcept (meta::nothrow::has_empty<meta::as_const_ref<type>> || (
                !meta::has_empty<meta::as_const_ref<type>> && meta::tuple_like<type>
            ))
            requires (meta::has_empty<meta::as_const_ref<type>> || meta::tuple_like<type>)
        {
            if constexpr (meta::has_empty<meta::as_const_ref<type>>) {
                return std::ranges::empty(value());
            } else {
                return meta::tuple_size<type> == 0;
            }
        }

        /* Allow tuple-like access and destructuring of the reversed range, with
        Python-style wraparound for negative indices.  This is identical to accessing
        the underlying container, but maps index `I` to `-I - 1` before applying
        wraparound. */
        template <index_type I, typename Self>
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{
                {meta::unpack_tuple<-I - 1>(std::forward<Self>(self).value())} noexcept;
            })
            requires (requires{
                {meta::unpack_tuple<-I - 1>(std::forward<Self>(self).value())};
            })
        {
            return (meta::unpack_tuple<-I - 1>(std::forward<Self>(self).value()));
        }

        /* Index operator for accessing elements in the reversed range, with
        Python-style wraparound for negative indices.  This is identical to indexing
        the underlying container, but maps index `i` to `-i - 1` before applying
        wraparound.  The actual index will always be forwarded as an unsigned
        `size_type` integer to maintain compatibility with as many container types as
        possible. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, index_type i)
            noexcept (requires{{std::forward<Self>(self).value()[
                size_type(impl::normalize_index(self.ssize(), -i - 1))]
            } noexcept;})
            requires (requires{{std::forward<Self>(self).value()[
                size_type(impl::normalize_index(self.ssize(), -i - 1))]
            };})
        {
            return (std::forward<Self>(self).value()[
                size_type(impl::normalize_index(self.ssize(), -i - 1))
            ]);
        }

        /* The reversed range maps `begin()` to the underlying container's `rbegin()`
        method. */
        [[nodiscard]] constexpr decltype(auto) begin()
            noexcept (requires{{impl::make_range_reversed{value()}.begin()} noexcept;})
            requires (requires{{impl::make_range_reversed{value()}.begin()};})
        {
            return (impl::make_range_reversed{value()}.begin());
        }

        /* The reversed range maps `begin()` to the underlying container's `rbegin()`
        method. */
        [[nodiscard]] constexpr decltype(auto) begin() const
            noexcept (requires{{impl::make_range_reversed{value()}.begin()} noexcept;})
            requires (requires{{impl::make_range_reversed{value()}.begin()};})
        {
            return (impl::make_range_reversed{value()}.begin());
        }

        /* The reversed range maps `end()` to the underlying container's `rend()`
        method. */
        [[nodiscard]] constexpr decltype(auto) end()
            noexcept (requires{{impl::make_range_reversed{value()}.end()} noexcept;})
            requires (requires{{impl::make_range_reversed{value()}.end()};})
        {
            return (impl::make_range_reversed{value()}.end());
        }

        /* The reversed range maps `end()` to the underlying container's `rend()`
        method. */
        [[nodiscard]] constexpr decltype(auto) end() const
            noexcept (requires{{impl::make_range_reversed{value()}.end()} noexcept;})
            requires (requires{{impl::make_range_reversed{value()}.end()};})
        {
            return (impl::make_range_reversed{value()}.end());
        }

        /* The reversed range maps `rbegin()` to the underlying container's `begin()`
        method. */
        [[nodiscard]] constexpr decltype(auto) rbegin()
            noexcept (requires{{impl::make_range_iterator{value()}.begin()} noexcept;})
            requires (requires{{impl::make_range_iterator{value()}.begin()};})
        {
            return (impl::make_range_iterator{value()}.begin());
        }

        /* The reversed range maps `rbegin()` to the underlying container's `begin()`
        method. */
        [[nodiscard]] constexpr decltype(auto) rbegin() const
            noexcept (requires{{impl::make_range_iterator{value()}.begin()} noexcept;})
            requires (requires{{impl::make_range_iterator{value()}.begin()};})
        {
            return (impl::make_range_iterator{value()}.begin());
        }

        /* The reversed range maps `rend()` to the underlying container's `end()`
        method. */
        [[nodiscard]] constexpr decltype(auto) rend()
            noexcept (requires{{impl::make_range_iterator{value()}.end()} noexcept;})
            requires  (requires{{impl::make_range_iterator{value()}.end()};})
        {
            return (impl::make_range_iterator{value()}.end());
        }

        /* The reversed range maps `rend()` to the underlying container's `end()`
        method. */
        [[nodiscard]] constexpr decltype(auto) rend() const
            noexcept (requires{{impl::make_range_iterator{value()}.end()} noexcept;})
            requires (requires{{impl::make_range_iterator{value()}.end()};})
        {
            return (impl::make_range_iterator{value()}.end());
        }
    };

}


/* A function object that reverses the order of iteration for a supported container.

The ranges that are produced by this object act just like normal ranges, but with the
forward and reverse iterators swapped, and the indexing logic modified to map index
`i` to index `-i - 1` before applying Python-style wraparound.

Note that the `reverse` class must be default-constructed, which standardizes it with
respect to other range adaptors and allows it to be easily chained together with other
operations to form more complex range-based algorithms. */
struct reverse {
private:
    template <typename C>
    using container = impl::reverse<meta::remove_rvalue<C>>;

    template <typename C>
    using range = bertrand::range<container<C>>;

public:
    template <typename C> requires (meta::reverse_iterable<C> || meta::tuple_like<C>)
    [[nodiscard]] static constexpr range<C> operator()(C&& c)
        noexcept (requires{{range<C>{container<C>{std::forward<C>(c)}}} noexcept;})
        requires (requires{{range<C>{container<C>{std::forward<C>(c)}}};})
    {
        return range<C>{container<C>{std::forward<C>(c)}};
    }
};


////////////////////
////    SORT    ////
////////////////////



//////////////////
////    AT    ////
//////////////////



//////////////////////
////    SELECT    ////
//////////////////////



////////////////////
////    FOLD    ////
////////////////////



/////////////////////////////////
////    MONADIC OPERATORS    ////
/////////////////////////////////


namespace meta {

    /// TODO: maybe I need a separate `match` metafunction that will take a single
    /// argument and apply the same logic as `visit`, but optimized for the single
    /// argument case, and applying tuple-like destructuring if applicable.  This would
    /// back the global `->*` operator, which would be enabled for all union and/or
    /// tuple types (NOT iterables).  Iterable comprehensions would be gated behind
    /// `range(container) ->*`, which would be defined only on the range type itself,
    /// which is what would also be returned to produce flattened comprehensions.
    /// It would just be an `impl::range<T>` type where `T` is either a direct container
    /// for `range(container)`, or a `std::subrange` if `range(begin, end)`, or a
    /// `std::views::iota` if `range(stop)` or `range(start, stop)`, possibly with a
    /// `std::views::stride_view<std::views::iota>` for `range(start, stop, step)`.

    // namespace detail {

    //     template <typename, typename>
    //     constexpr bool _match_tuple_alts = false;
    //     template <typename F, typename... As>
    //     constexpr bool _match_tuple_alts<F, meta::pack<As...>> = (meta::callable<F, As> && ...);
    //     template <typename, typename>
    //     constexpr bool _match_tuple = false;
    //     template <typename F, typename... Ts>
    //     constexpr bool _match_tuple<F, meta::pack<Ts...>> =
    //         (_match_tuple_alts<F, typename impl::visitable<Ts>::alternatives> && ...);
    //     template <typename F, typename T>
    //     concept match_tuple = meta::tuple_like<T> && _match_tuple<F, meta::tuple_types<T>>;

    //     template <typename, typename>
    //     constexpr bool _nothrow_match_tuple_alts = false;
    //     template <typename F, typename... As>
    //     constexpr bool _nothrow_match_tuple_alts<F, meta::pack<As...>> =
    //         (meta::nothrow::callable<F, As> && ...);
    //     template <typename, typename>
    //     constexpr bool _nothrow_match_tuple = false;
    //     template <typename F, typename... Ts>
    //     constexpr bool _nothrow_match_tuple<F, meta::pack<Ts...>> =
    //         (_nothrow_match_tuple_alts<F, typename impl::visitable<Ts>::alternatives> && ...);
    //     template <typename F, typename T>
    //     concept nothrow_match_tuple =
    //         meta::nothrow::tuple_like<T> && _nothrow_match_tuple<F, meta::nothrow::tuple_types<T>>;



    //     template <
    //         typename F,  // match visitor function
    //         typename returns,  // unique, non-void return types
    //         typename errors,  // expected error states
    //         bool has_void_,  // true if a void return type was encountered
    //         bool optional,  // true if an optional return type was encountered
    //         bool nothrow_,  // true if all permutations are noexcept
    //         typename alternatives  // alternatives for the matched object
    //     >
    //     struct _match {
    //         using type = visit_to_expected<
    //             typename visit_to_optional<
    //                 typename returns::template eval<visit_to_union>::type,
    //                 has_void_ || optional
    //             >::type,
    //             errors
    //         >::type;
    //         static constexpr bool enable = true;
    //         static constexpr bool ambiguous = false;
    //         static constexpr bool unmatched = false;
    //         static constexpr bool consistent =
    //             returns::size() == 0 || (returns::size() == 1 && !has_void_);

    //         /// TODO: nothrow has to account for nothrow conversions to type
    //         static constexpr bool nothrow = nothrow_;
    //     };
    //     template <
    //         typename F,
    //         typename curr,
    //         typename... next
    //     >
    //         requires (meta::callable<F, curr> && !match_tuple<F, curr>)
    //     struct _match<F, meta::pack<curr, next...>> {
    //         /// TODO: pass the tuple directly
    //     };
    //     template <
    //         typename F,
    //         typename curr,
    //         typename... next
    //     >
    //         requires (!meta::callable<F, curr> && match_tuple<F, curr>)
    //     struct _match<F, meta::pack<curr, next...>> {
    //         /// TODO: unpack tuple
    //     };
    //     template <
    //         typename F,
    //         typename curr,
    //         typename... next
    //     >
    //     struct _match<F, meta::pack<curr, next...>> {
    //         using type = void;
    //         static constexpr bool ambiguous = meta::callable<F, curr> && match_tuple<F, curr>;
    //         static constexpr bool unmatched = !meta::callable<F, curr> && !match_tuple<F, curr>;
    //         static constexpr bool consistent = false;
    //         static constexpr bool nothrow = false;
    //     };





    //     template <typename F, typename T>
    //     struct match : _match<F, typename impl::visitable<T>::alternatives> {};

    // }

}














/// auto x = async{f, scheduler}(1, 2, 3);
/// async f = [](int x) -> int {
///     return x + 1;
/// }
/// auto y = f(1);  // returns a Future<F, Args...>, where `F` represents the
///                 // forwarded underlying function type, and `Args...` is recorded
///                 // as a tuple.  The future monad itself acts just like the result,
///                 // but extends continuation functions in a way that is perfectly
///                 // analogous to expression templates, which are needed for ranges
///                 // anyway.  `range` can therefore be thought of as building
///                 // expressions across space, and `async` as building them across
///                 // time, with the two being perfectly composable.

/// Schedulers can also be used to customize the execution in some way, and defaults
/// to running as a coroutine on a separate thread.  It could possibly allow everything
/// up to remote execution on a different system altogether.  The scheduler can be
/// arbitrarily complex, and provides a good benchmarking surface via inversion of
/// control.  The only requirement imposed on the scheduler is that it returns a
/// standard future type, which exposes `co_await` and `co_yield` operators, and
/// works as a monad that automatically appends continuation functions

/// -> It seems that custom allocators can be used with C++ coroutines, it's just not
/// very simple.  Regardless, this can only be implemented after allocate.h, if I
/// want to use the same virtual memory pool as all other containers, and pin them
/// to the operative threads.


}


namespace std {

    /// TODO: remember to enable borrowed ranges for all ranges.  This requires some
    /// care to make sure the intended semantics are always observed.  Also make sure
    /// that these statuses even make sense.

    /* Specializing `std::ranges::enable_borrowed_range` ensures that iterators over
    slices are not tied to the lifetime of the slice itself, but rather to that of the
    underlying container. */
    template <bertrand::meta::slice T>
    constexpr bool ranges::enable_borrowed_range<T> = true;


    /// TODO: tuple_size and tuple_element for ranges where possible.

}


#endif