#ifndef BERTRAND_ITER_RANGE_H
#define BERTRAND_ITER_RANGE_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/shape.h"
#include "bertrand/math.h"
#include "bertrand/union.h"


namespace bertrand {


namespace meta {

    namespace detail {

        template <typename T>
        constexpr bool range_algorithm = false;

        template <typename T>
        constexpr bool range = false;

        template <typename T>
        constexpr bool range_transparent = false;

        template <typename T>
        constexpr bool scalar = false;

        template <typename T>
        constexpr bool iota = false;

        template <typename T>
        constexpr bool subrange = false;

    }

    /* Detect whether a function object type represents a range algorithm.  If this
    evaluates to `true`, then the `range ->* T` operator will attempt to call `T`
    directly with the input range, rather than zipping it for elementwise
    application. */
    template <typename T>
    concept range_algorithm = detail::range_algorithm<unqualify<T>>;

    /* Detect whether a type is a `range`.  If additional types are provided, then they
    equate to a convertibility check against the range's yield type.  If more than one
    type is provided, then the yield type must be tuple-like, and destructurable to the
    enumerated types.  Note that because ranges always yield other ranges when iterated
    over, the convertibility check will always take range conversion semantics into
    account.  See the `range` class for more details. */
    template <typename T, typename... Rs>
    concept range = detail::range<unqualify<T>> && (
        sizeof...(Rs) == 0 ||
        (sizeof...(Rs) == 1 && convertible_to<yield_type<T>, first_type<Rs...>>) ||
        structured_with<yield_type<T>, Rs...>
    );

    /* Detect whether the given type is considered to be transparent to a range's
    dereference operator.  If this is true, then dereferencing an equivalent range
    will recursively call `T`'s dereference operator, rendering it invisible to the
    user.  This is useful for implementing trivial adaptors, such as those that wrap
    non-iterable scalars or tuple-like types, where the adaptor itself is not usually
    what the user is interested in.  `meta::strip_range()` may be used to reveal
    transparent types for internal use. */
    template <typename T>
    concept range_transparent = detail::range_transparent<unqualify<T>>;

    /* Perfectly forward the argument or retrieve its underlying value if it is a
    range.  This is equivalent to conditionally compiling a dereference based on the
    state of the `meta::range<T>` concept, and the result will never be another
    range. */
    template <typename T>
    [[nodiscard]] constexpr decltype(auto) from_range(T&& t)
        noexcept (!meta::range<T> || requires{{*::std::forward<T>(t)} noexcept;})
    {
        if constexpr (meta::range<T>) {
            return (*::std::forward<T>(t));
        } else {
            return (::std::forward<T>(t));
        }
    }

    /* Get the type backing a range monad, assuming `T` satisfies `meta::range`.
    Otherwise, forward the original type unchanged.  This can never be another range,
    and is always equivalent to the return type of `meta::from_range(T)`.  The result
    may be used to specialize `iter::range<T>` to recover a range equivalent to `T`. */
    template <typename T>
    using remove_range = remove_rvalue<decltype((from_range(::std::declval<T>())))>;

    /* Perfectly forward the argument or retrieve its internal container if it is a
    range.  This is equivalent to `meta::from_range(T)` in every way, except that it
    may return an internal container type that would otherwise be transparent in the
    case of non-iterable scalars or tuples. */
    template <typename T>
    [[nodiscard]] constexpr decltype(auto) strip_range(T&& t)
        noexcept (!meta::range<T> || requires{{*::std::forward<T>(t)} noexcept;})
    {
        if constexpr (meta::range<T>) {
            return (*::std::forward<T>(t).__value);
        } else {
            return (::std::forward<T>(t));
        }
    }

    /* Returns `true` if `T` is a trivial range with provably zero elements, or `false`
    if it contains more than one, or if its size cannot be known until run time.
    Dereferencing the range reveals the inner container or empty range type. */
    template <typename T, typename... Rs>
    concept empty_range = range<T, Rs...> && tuple_like<T> && tuple_size<T> == 0;

    /* Returns `true` if `T` is a trivial range of just a single element, or `false`
    if it wraps an iterable or tuple-like container.  Dereferencing the scalar reveals
    the underlying value. */
    template <typename T, typename... Rs>
    concept scalar =
        range<T, Rs...> && detail::scalar<unqualify<decltype(*::std::declval<T>().__value)>>;

    /* A refinement of `meta::range<T, Rs...>` that only matches iota ranges (i.e.
    those of the form `[start, stop[, step]]`, where `start` is not an iterator).
    Dereferencing the range reveals the inner iota type. */
    template <typename T, typename... Rs>
    concept iota =
        range<T, Rs...> && detail::iota<unqualify<decltype(*::std::declval<T>().__value)>>;

    /* A refinement of `meta::range<T, Rs...>` that only matches subranges (i.e.
    those of the form `[start, stop[, step]]`, where `start` is an iterator type).
    Dereferencing the range reveals the inner subrange type. */
    template <typename T, typename... Rs>
    concept subrange =
        range<T, Rs...> && detail::subrange<unqualify<decltype(*::std::declval<T>().__value)>>;

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

        namespace member {

            template <typename K, typename A>
            concept has_contains = requires(
                meta::remove_range<meta::as_const_ref<K>> key,
                meta::remove_range<meta::as_const_ref<A>> arg
            ) {
                {arg.contains(key)} -> meta::convertible_to<bool>;
            };

            template <typename K, typename A>
            concept nothrow_contains = requires(
                meta::remove_range<meta::as_const_ref<K>> key,
                meta::remove_range<meta::as_const_ref<A>> arg
            ) {
                {arg.contains(key)} noexcept -> nothrow::convertible_to<bool>;
            };

            template <typename K, typename A>
            concept has_count = requires(
                meta::remove_range<meta::as_const_ref<K>> key,
                meta::remove_range<meta::as_const_ref<A>> arg
            ) {
                {arg.count(key)} -> meta::convertible_to<size_t>;
            };

            template <typename K, typename A>
            concept nothrow_count = requires(
                meta::remove_range<meta::as_const_ref<K>> key,
                meta::remove_range<meta::as_const_ref<A>> arg
            ) {
                {arg.count(key)} noexcept -> nothrow::convertible_to<size_t>;
            };

        }

        namespace adl {

            template <typename K, typename A>
            concept has_contains = requires(
                meta::remove_range<meta::as_const_ref<K>> key,
                meta::remove_range<meta::as_const_ref<A>> arg
            ) {
                {contains(arg, key)} -> meta::convertible_to<bool>;
            };

            template <typename K, typename A>
            concept nothrow_contains = requires(
                meta::remove_range<meta::as_const_ref<K>> key,
                meta::remove_range<meta::as_const_ref<A>> arg
            ) {
                {contains(arg, key)} noexcept -> nothrow::convertible_to<bool>;
            };

            template <typename K, typename A>
            concept has_count = requires(
                meta::remove_range<meta::as_const_ref<K>> key,
                meta::remove_range<meta::as_const_ref<A>> arg
            ) {
                {count(arg, key)} -> meta::convertible_to<size_t>;
            };

            template <typename K, typename A>
            concept nothrow_count = requires(
                meta::remove_range<meta::as_const_ref<K>> key,
                meta::remove_range<meta::as_const_ref<A>> arg
            ) {
                {count(arg, key)} noexcept -> nothrow::convertible_to<size_t>;
            };

        }

        template <typename K, typename A>
        constexpr bool invoke_contains(const K& key, const A& a)
            noexcept (member::nothrow_contains<K, A>)
            requires (member::has_contains<K, A>)
        {
            return meta::from_range(a).contains(meta::from_range(key));
        }

        template <typename K, typename A>
        constexpr bool invoke_contains(const K& key, const A& a)
            noexcept (adl::nothrow_contains<K, A>)
            requires (!member::has_contains<K, A> && adl::has_contains<K, A>)
        {
            return contains(meta::from_range(a), meta::from_range(key));
        }

        template <typename K, typename A>
        constexpr size_t invoke_count(const K& key, const A& a)
            noexcept (member::nothrow_count<K, A>)
            requires (member::has_count<K, A>)
        {
            return meta::from_range(a).count(meta::from_range(key));
        }

        template <typename K, typename A>
        constexpr size_t invoke_count(const K& key, const A& a)
            noexcept (adl::nothrow_count<K, A>)
            requires (!member::has_count<K, A> && adl::has_count<K, A>)
        {
            return count(meta::from_range(a), meta::from_range(key));
        }

    }

}


namespace impl {

    template <typename C>
    concept range_concept = meta::not_void<C> && meta::not_rvalue<C> && !meta::range<C>;

    /////////////////////
    ////    EMPTY    ////
    /////////////////////

    /* A trivial range with zero elements.  This is the default type for the `range`
    class template, allowing it to be default-constructed.  The template parameter sets
    the `reference` type returned by the iterator's dereference operators, which is
    useful for metaprogramming purposes.  No values will actually be yielded. */
    template <typename T = const NoneType&>
    struct empty_range {
        static constexpr void swap(empty_range&) noexcept {}
        [[nodiscard]] static constexpr size_t size() noexcept { return 0; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return 0; }
        [[nodiscard]] static constexpr impl::shape<0> shape() noexcept { return {}; }
        [[nodiscard]] static constexpr bool empty() noexcept { return true; }
        [[nodiscard]] constexpr auto begin() noexcept {
            return empty_iterator<T>{};
        }
        [[nodiscard]] constexpr auto begin() const noexcept {
            return empty_iterator<meta::as_const<T>>{};
        }
        [[nodiscard]] constexpr auto end() noexcept {
            return empty_iterator<T>{};
        }
        [[nodiscard]] constexpr auto end() const noexcept {
            return empty_iterator<meta::as_const<T>>{};
        }
        [[nodiscard]] constexpr auto rbegin() noexcept {
            return std::make_reverse_iterator(end());
        }
        [[nodiscard]] constexpr auto rbegin() const noexcept {
            return std::make_reverse_iterator(end());
        }
        [[nodiscard]] constexpr auto rend() noexcept {
            return std::make_reverse_iterator(begin());
        }
        [[nodiscard]] constexpr auto rend() const noexcept {
            return std::make_reverse_iterator(begin());
        }

        /* A call operator allows this container to be used as a range algorithm that
        effectively dereferences the deepest nested range, effectively reducing the
        argument's shape by one dimension.  This allows the following syntax:

            ```
            iter::range(r) ->* *iter::range{};
            ```
        
        Which is expression-equivalent to:

            ```
            iter::range(r) ->* [](auto&& val)
                requires (
                    meta::range<decltype(val)> &&
                    !meta::range<meta::yield_type<decltype(val)>>
                )
            {
                return (*std::forward<decltype(val)>(val));
            };
            ```
        */
        template <meta::range V> requires (!meta::range<meta::yield_type<V>>)
        [[nodiscard]] static constexpr decltype(auto) operator()(V&& val)
            noexcept (requires{{*std::forward<V>(val)} noexcept;})
            requires (requires{{*std::forward<V>(val)};})
        {
            return (*std::forward<V>(val));
        }
    };

    //////////////////////
    ////    SCALAR    ////
    //////////////////////

    /* A range over just a single scalar element.  Indexing the range perfectly
    forwards that element, and iterating over it is akin to taking its address.  A
    CTAD guide chooses this type when a single element is passed to the `range()`
    constructor. */
    template <range_concept T>
    struct scalar {
        [[no_unique_address]] impl::ref<T> __value;

        template <typename... A>
        [[nodiscard]] constexpr scalar(A&&... args)
            noexcept (meta::nothrow::constructible_from<T, A...>)
            requires (meta::constructible_from<T, A...>)
        :
            __value{T{std::forward<A>(args)...}}
        {}

        constexpr void swap(scalar& other)
            noexcept (requires{{meta::swap(__value, other.__value)} noexcept;})
            requires (requires{{meta::swap(__value, other.__value)};})
        {
            meta::swap(__value, other.__value);
        }

        [[nodiscard]] static constexpr size_t size() noexcept { return 1; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return 1; }
        [[nodiscard]] static constexpr bool empty() noexcept { return false; }
        [[nodiscard]] static constexpr impl::shape<1> shape() noexcept { return {1}; }

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

        [[nodiscard]] constexpr auto data()
            noexcept (requires{{std::addressof(*__value)} noexcept;})
            requires (requires{{std::addressof(*__value)};})
        {
            return std::addressof(*__value);
        }

        [[nodiscard]] constexpr auto data() const
            noexcept (requires{{std::addressof(*__value)} noexcept;})
            requires (requires{{std::addressof(*__value)};})
        {
            return std::addressof(*__value);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self)} noexcept;})
            requires (requires{{*std::forward<Self>(self)};})
        {
            return (*std::forward<Self>(self));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) back(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self)} noexcept;})
            requires (requires{{*std::forward<Self>(self)};})
        {
            return (*std::forward<Self>(self));
        }

        template <size_t I, typename Self> requires (I == 0)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self)} noexcept;})
            requires (requires{{*std::forward<Self>(self)};})
        {
            return (*std::forward<Self>(self));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, ssize_t i)
            noexcept (!DEBUG)
        {
            if constexpr (DEBUG) {
                i = impl::normalize_index(1, i);  // may throw
            }
            return (*std::forward<Self>(self));
        }

        template <typename Self>
        [[nodiscard]] constexpr auto begin(this Self&& self)
            noexcept (requires{{impl::trivial_iterator(*std::forward<Self>(self))} noexcept;})
            requires (requires{{impl::trivial_iterator(*std::forward<Self>(self))};})
        {
            return impl::trivial_iterator(*std::forward<Self>(self));
        }

        template <typename Self>
        [[nodiscard]] constexpr auto end(this Self&& self)
            noexcept (requires{{impl::trivial_iterator(*std::forward<Self>(self)) + 1} noexcept;})
            requires (requires{{impl::trivial_iterator(*std::forward<Self>(self)) + 1};})
        {
            return impl::trivial_iterator(*std::forward<Self>(self)) + 1;
        }

        template <typename Self>
        [[nodiscard]] constexpr auto rbegin(this Self&& self)
            noexcept (requires{
                {std::make_reverse_iterator(std::forward<Self>(self).end())} noexcept;
            })
            requires (requires{
                {std::make_reverse_iterator(std::forward<Self>(self).end())};
            })
        {
            return std::make_reverse_iterator(std::forward<Self>(self).end());
        }

        template <typename Self>
        [[nodiscard]] constexpr auto rend(this Self&& self)
            noexcept (requires{
                {std::make_reverse_iterator(std::forward<Self>(self).begin())} noexcept;
            })
            requires (requires{
                {std::make_reverse_iterator(std::forward<Self>(self).begin())};
            })
        {
            return std::make_reverse_iterator(std::forward<Self>(self).begin());
        }
    };

    template <typename T>
    scalar(T&&) -> scalar<meta::remove_rvalue<T>>;

    /////////////////////
    ////    TUPLE    ////
    /////////////////////

    /* A unique vtable has to be emitted for each observed qualification of the tuple
    type in order to perfectly forward the results. */
    template <meta::tuple_like C>
    struct tuple_vtable {
        using type = meta::tuple_types<C>::template eval<meta::make_union>;
        template <size_t I>
        struct fn {
            static constexpr type operator()(meta::forward<C> c)
                noexcept (requires{
                    {meta::get<I>(c)} noexcept -> meta::nothrow::convertible_to<type>;
                })
                requires (requires{{meta::get<I>(c)} -> meta::convertible_to<type>;})
            {
                return meta::get<I>(c);
            }
        };
        using dispatch = impl::basic_vtable<fn, meta::tuple_size<C>>;
    };

    /* A generic iterator over an arbitrary tuple type embedded in a `tuple_range<C>`
    wrapper.  The iterator works by traversing a vtable of function pointers that yield
    each value dynamically. */
    template <typename Self>
    struct tuple_iterator {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = ssize_t;
        using reference = tuple_vtable<decltype((*std::declval<Self>()))>::type;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<value_type>;

        meta::as_pointer<Self> self;
        difference_type index;

        [[nodiscard]] constexpr reference operator*() const
            noexcept (requires{{typename tuple_vtable<decltype((*std::declval<Self>()))>::dispatch{
                size_t(index)
            }(*std::forward<Self>(*self))} noexcept;})
            requires (requires{{typename tuple_vtable<decltype((*std::declval<Self>()))>::dispatch{
                size_t(index)
            }(*std::forward<Self>(*self))};})
        {
            return (typename tuple_vtable<decltype((*std::declval<Self>()))>::dispatch{
                size_t(index)
            }(*std::forward<Self>(*self)));
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow(**this)} noexcept;})
        {
            return impl::arrow(**this);
        }

        [[nodiscard]] constexpr decltype(auto) operator[](difference_type n) const
            noexcept (requires{{(*self)[size_t(index + n)]} noexcept;})
        {
            return ((*self)[size_t(index + n)]);
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
            return {self.self, self.index + n};
        }

        [[nodiscard]] friend constexpr tuple_iterator operator+(
            difference_type n,
            const tuple_iterator& self
        ) noexcept {
            return {self.self, self.index + n};
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
            return {self, index - n};
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

    /* A wrapper around a generic tuple that allows it to be indexed and iterated over
    at runtime via dynamic dispatch.  If the tuple consists of multiple types, then the
    subscript and yield types will be promoted to unions of all the possible results. */
    template <meta::tuple_like C>
    struct tuple_range {
    private:
        template <meta::is<tuple_range> Self>
        using vtable = tuple_vtable<decltype((*std::declval<Self>()))>::dispatch;

    public:
        [[no_unique_address]] impl::ref<C> __value;

        [[nodiscard]] static constexpr size_t size() noexcept { return meta::tuple_size<C>; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return ssize_t(size()); }
        [[nodiscard]] static constexpr bool empty() noexcept { return false; }

        template <typename... A>
        [[nodiscard]] constexpr tuple_range(A&&... args)
            noexcept (requires{{impl::ref<C>{C{std::forward<A>(args)...}}} noexcept;})
            requires (requires{{impl::ref<C>{C{std::forward<A>(args)...}}};})
        :
            __value{C{std::forward<A>(args)...}}
        {}

        constexpr void swap(tuple_range& other)
            noexcept (requires{{meta::swap(__value, other.__value)} noexcept;})
            requires (requires{{meta::swap(__value, other.__value)};})
        {
            meta::swap(__value, other.__value);
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

        template <ssize_t I, typename Self> requires (impl::valid_index<ssize(), I>)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{meta::get<impl::normalize_index<ssize(), I>()>(
                *std::forward<Self>(self)
            )} noexcept;})
            requires (requires{{meta::get<impl::normalize_index<ssize(), I>()>(
                *std::forward<Self>(self)
            )};})
        {
            return (meta::get<impl::normalize_index<ssize(), I>()>(*std::forward<Self>(self)));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, ssize_t i)
            noexcept (requires{{vtable<Self>{size_t(impl::normalize_index(ssize(), i))}(
                *std::forward<Self>(self)
            )} noexcept;})
            requires (requires{{vtable<Self>{size_t(impl::normalize_index(ssize(), i))}(
                *std::forward<Self>(self)
            )};})
        {
            return (vtable<Self>{size_t(impl::normalize_index(ssize(), i))}(
                *std::forward<Self>(self)
            ));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self)
            noexcept (requires{{vtable<Self>{0}(*std::forward<Self>(self))} noexcept;})
            requires (requires{{vtable<Self>{0}(*std::forward<Self>(self))};})
        {
            return (vtable<Self>{0}(*std::forward<Self>(self)));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) back(this Self&& self)
            noexcept (requires{{vtable<Self>{size() - 1}(*std::forward<Self>(self))} noexcept;})
            requires (requires{{vtable<Self>{size() - 1}(*std::forward<Self>(self))};})
        {
            return (vtable<Self>{size() - 1}(*std::forward<Self>(self)));
        }

        template <typename Self>
        [[nodiscard]] constexpr auto begin(this Self&& self) noexcept {
            return tuple_iterator<Self>{&self, 0};
        }

        template <typename Self>
        [[nodiscard]] constexpr auto end(this Self&& self) noexcept {
            return tuple_iterator<Self>{&self, ssize()};
        }

        template <typename Self>
        [[nodiscard]] constexpr auto rbegin(this Self&& self) noexcept {
            return std::make_reverse_iterator(std::forward<Self>(self).end());
        }

        template <typename Self>
        [[nodiscard]] constexpr auto rend(this Self&& self) noexcept {
            return std::make_reverse_iterator(std::forward<Self>(self).begin());
        }
    };
    template <meta::tuple_like C> requires (meta::tuple_size<C> == 0)
    struct tuple_range<C> {
        [[no_unique_address]] impl::ref<C> __value;

        [[nodiscard]] static constexpr size_t size() noexcept { return 0; }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept { return 0; }
        [[nodiscard]] static constexpr bool empty() noexcept { return true; }

        template <typename... A>
        [[nodiscard]] constexpr tuple_range(A&&... args)
            noexcept (requires{{impl::ref<C>{C{std::forward<A>(args)...}}} noexcept;})
            requires (requires{{impl::ref<C>{C{std::forward<A>(args)...}}};})
        :
            __value{C{std::forward<A>(args)...}}
        {}

        constexpr void swap(tuple_range& other)
            noexcept (requires{{meta::swap(__value, other.__value)} noexcept;})
            requires (requires{{meta::swap(__value, other.__value)};})
        {
            meta::swap(__value, other.__value);
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

        [[nodiscard]] static constexpr auto begin() noexcept {
            return empty_iterator<>{};
        }

        [[nodiscard]] static constexpr auto end() noexcept {
            return empty_iterator<>{};
        }

        [[nodiscard]] static constexpr auto rbegin() noexcept {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]] static constexpr auto rend() noexcept {
            return std::make_reverse_iterator(begin());
        }
    };

    template <typename T>
    tuple_range(T&&) -> tuple_range<meta::remove_rvalue<T>>;

    ////////////////////
    ////    IOTA    ////
    ////////////////////

    template <typename T>
    concept strictly_positive = meta::unsigned_integer<T> || !requires(meta::as_const_ref<T> t) {
        {t < 0} -> meta::truthy;
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
    concept iota_empty = meta::is<T, trivial>;

    template <typename Stop>
    concept iota_infinite = iota_empty<Stop>;

    template <typename Start, typename Stop, typename Step>
    concept iota_bounded =
        !iota_infinite<Stop> &&
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {start < stop} -> meta::truthy;
        } && (strictly_positive<Step> || requires(
            meta::as_const_ref<Start> start,
            meta::as_const_ref<Stop> stop
        ) {
            {start > stop} -> meta::truthy;
        });

    template <typename Start, typename Stop, typename Step>
    concept iota_conditional =
        !iota_infinite<Stop> &&
        !iota_bounded<Start, Stop, Step> &&
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {stop(start)} -> meta::truthy;
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

    /// TODO: reference/pointer/value_type need to reflect the actual type yielded by
    /// the iota's dereference operator, and they currently do not.  `reference` must
    /// match this type exactly.  The current dereference operator may return a
    /// prvalue if it supports random access, to satisfy iterator concepts.

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
    operator if possible.  If `start` is decrementable, then the iterators will model
    `std::bidirectional_iterator`, and possibly also `std::random_access_iterator` if
    it supports random-access addition and subtraction with the step size.  Since the
    `end()` iterator is an empty sentinel, the range will never model
    `std::common_range` (but the sentinel may model `std::sized_sentinel_for<Begin>` if
    `ssize()` is available).

    The indices are meant to reflect typical loop syntax in a variety of languages,
    and can effectively replace any C-style `for` or `while` loop without overhead.
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
    template <range_concept Start, range_concept Stop, range_concept Step>
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
    struct iota {
        using start_type = Start;
        using stop_type = Stop;
        using step_type = Step;
        using difference_type = iota_difference<Start, Stop>;
        using size_type = meta::as_unsigned<difference_type>;
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
        using value_type = meta::unqualify<Start>;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::address_type<reference>;

    private:
        using copy = iota<
            meta::unqualify<start_type>,
            meta::as_const_ref<stop_type>,
            meta::as_const_ref<step_type>
        >;
        using move = iota<meta::unqualify<start_type>, stop_type, step_type>;

        [[no_unique_address]] impl::ref<start_type> m_start {};
        [[no_unique_address]] impl::ref<stop_type> m_stop {};
        [[no_unique_address]] impl::ref<step_type> m_step {};

    public:
        [[nodiscard]] constexpr iota() = default;
        [[nodiscard]] constexpr iota(
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
                !iota_linear<Start, Step> ||
                !requires{{*m_step == 0} -> meta::truthy;}
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
            if constexpr (
                DEBUG &&
                iota_linear<Start, Step> &&
                requires{{*m_step == 0} -> meta::truthy;}
            ) {
                if (*m_step == 0) {
                    throw zero_step_error();
                }
            }
        }
        [[nodiscard]] constexpr iota(
            const meta::unqualify<Start>& start,
            const meta::unqualify<Stop>& stop,
            const meta::unqualify<Step>& step
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

        constexpr void swap(iota& other)
            noexcept (requires{
                {meta::swap(m_start, other.m_start)} noexcept;
                {meta::swap(m_stop, other.m_stop)} noexcept;
                {meta::swap(m_step, other.m_step)} noexcept;
            })
            requires (requires{
                {meta::swap(m_start, other.m_start)};
                {meta::swap(m_stop, other.m_stop)};
                {meta::swap(m_step, other.m_step)};
            })
        {
            meta::swap(m_start, other.m_start);
            meta::swap(m_stop, other.m_stop);
            meta::swap(m_step, other.m_step);
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
            noexcept (requires{{copy{start(), stop(), step()}} noexcept;})
            requires (requires{{copy{start(), stop(), step()}};})
        {
            return copy{start(), stop(), step()};
        }

        [[nodiscard]] constexpr move begin() &&
            noexcept (requires{{move{
                std::move(*this).start(),
                std::move(*this).stop(),
                std::move(*this).step()
            }} noexcept;})
            requires (requires{{move{
                std::move(*this).start(),
                std::move(*this).stop(),
                std::move(*this).step()
            }};})
        {
            return move{
                std::move(*this).start(),
                std::move(*this).stop(),
                std::move(*this).step()
            };
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
                {start() < stop()} noexcept -> meta::nothrow::truthy;
            } && (strictly_positive<Step> || requires{
                {step() < 0} noexcept -> meta::nothrow::truthy;
                {start() > stop()} noexcept -> meta::nothrow::truthy;
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
            noexcept (requires{{stop()(start())} noexcept -> meta::nothrow::truthy;})
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
            } noexcept -> meta::nothrow::truthy;})
            requires (requires{{
                (other.stop() - other.start()) == (stop() - start())
            } -> meta::truthy;})
        {
            return bool((other.stop() - other.start()) == (stop() - start()));
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
                {increment()} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {increment()};
            })
        {
            copy tmp = begin();
            increment();
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
                {decrement()} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {decrement()};
            })
        {
            copy tmp = begin();
            decrement();
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

    template <typename Start, typename Stop = trivial, typename Step = trivial>
    iota(Start&&, Stop&& stop, Step&& step = {}) -> iota<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >;

    ////////////////////////
    ////    SUBRANGE    ////
    ////////////////////////

    template <typename Start, typename Stop>
    using subrange_difference = iota_difference<Start, Stop>;

    template <typename T>
    concept subrange_empty = meta::is<T, trivial>;

    template <typename Stop>
    concept subrange_infinite = subrange_empty<Stop>;

    template <typename Start, typename Stop, typename Step>
    concept subrange_bounded =
        !subrange_infinite<Stop> &&
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {start < stop} -> meta::truthy;
        } && (strictly_positive<Step> || requires(
            meta::as_const_ref<Start> start,
            meta::as_const_ref<Stop> stop
        ) {
            {start > stop} -> meta::truthy;
        });

    template <typename Start, typename Stop, typename Step>
    concept subrange_equal =
        !subrange_infinite<Stop> &&
        !subrange_bounded<Start, Stop, Step> &&
        subrange_empty<Step> &&
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {start == stop} -> meta::truthy;
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
            {index >= subrange_difference<Start, Stop>(stop)} -> meta::truthy;
        };

    template <typename Start, typename Stop, typename Step>
    concept subrange_conditional =
        !subrange_infinite<Stop> &&
        !subrange_bounded<Start, Stop, Step> &&
        !subrange_equal<Start, Stop, Step> &&
        !subrange_counted<Start, Stop, Step> &&
        requires(meta::as_const_ref<Start> start, meta::as_const_ref<Stop> stop) {
            {stop(start)} -> meta::truthy;
        };

    template <typename Start, typename Step>
    concept subrange_simple =
        subrange_empty<Step> &&
        requires(meta::unqualify<Start>& start) {
            {++start};
        };

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
        range_concept<Start> &&
        range_concept<Stop> &&
        range_concept<Step> &&
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
    the subscript operator if the underlying iterator supports it.  If the underlying
    iterator is also bidirectional, then the range will model
    `std::bidirectional_range` as well.  If the iterator is random-access, then the
    range will model `std::random_access_range`, and if it is contiguous and no step
    size is given, then the range will model `std::contiguous_range` and provide a
    `data()` method as well.  Since the `end()` iterator is an empty sentinel, the
    range will never model `std::common_range` (but the sentinel may model
    `std::sized_sentinel_for<Begin>` if `ssize()` is available). */
    template <typename Start, typename Stop = trivial, typename Step = trivial>
        requires (subrange_concept<Start, Stop, Step>)
    struct subrange {
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
        using copy = subrange<
            meta::unqualify<start_type>,
            meta::as_const_ref<stop_type>,
            meta::as_const_ref<step_type>
        >;
        using move = subrange<meta::unqualify<start_type>, stop_type, step_type>;

        static constexpr subrange_check check =
            (subrange_infinite<Stop> || subrange_simple<Start, Step>) ?
                subrange_check::NEVER :
                (subrange_bounded<Start, Stop, Step> || subrange_counted<Start, Stop, Step>) &&
                (subrange_linear<Start, Stop, Step> || subrange_loop<Start, Stop, Step>) ?
                    subrange_check::CONSTEVAL :
                    subrange_check::ALWAYS;

        using overflow_type = std::conditional_t<
            check == subrange_check::NEVER,
            trivial,
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
                !requires{{this->step() == 0} -> meta::truthy;}
            ) && (
                !DEBUG ||
                !subrange_counted<Start, Stop, Step> ||
                !requires{{this->stop() < 0} -> meta::truthy;}
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
                    requires{{this->step() == 0} -> meta::truthy;}
                ) {
                    if (this->step() == 0) {
                        throw zero_step_error();
                    }
                }
                if constexpr (
                    subrange_counted<Start, Stop, Step> &&
                    requires{{this->stop() < 0} -> meta::truthy;}
                ) {
                    if (this->stop() < 0) {
                        throw negative_count_error();
                    }
                }
            }
        }
        [[nodiscard]] constexpr subrange(
            const meta::unqualify<start_type>& start,
            const meta::unqualify<stop_type>& stop,
            const meta::unqualify<step_type>& step,
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

        constexpr void swap(subrange& other)
            noexcept (requires{
                {meta::swap(m_start, other.m_start)} noexcept;
                {meta::swap(m_stop, other.m_stop)} noexcept;
                {meta::swap(m_step, other.m_step)} noexcept;
                {meta::swap(m_index, other.m_index)} noexcept;
                {meta::swap(m_overflow, other.m_overflow)} noexcept;
            })
            requires (requires{
                {meta::swap(m_start, other.m_start)};
                {meta::swap(m_stop, other.m_stop)};
                {meta::swap(m_step, other.m_step)};
                {meta::swap(m_index, other.m_index)};
                {meta::swap(m_overflow, other.m_overflow)};
            })
        {
            meta::swap(m_start, other.m_start);
            meta::swap(m_stop, other.m_stop);
            meta::swap(m_step, other.m_step);
            meta::swap(m_index, other.m_index);
            meta::swap(m_overflow, other.m_overflow);
        }

        [[nodiscard]] constexpr copy begin() const
            noexcept (requires{{copy{start(), stop(), step(), m_index, m_overflow}} noexcept;})
            requires (requires{{copy{start(), stop(), step(), m_index, m_overflow}};})
        {
            return copy{start(), stop(), step(), m_index, m_overflow};
        }

        [[nodiscard]] constexpr move begin() &&
            noexcept (requires{{move{
                std::move(*this).start(),
                std::move(*this).stop(),
                std::move(*this).step(),
                m_index,
                m_overflow
            }} noexcept;})
            requires (requires{{move{
                std::move(*this).start(),
                std::move(*this).stop(),
                std::move(*this).step(),
                m_index,
                m_overflow
            }};})
        {
            return move{
                std::move(*this).start(),
                std::move(*this).stop(),
                std::move(*this).step(),
                m_index,
                m_overflow
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
                {std::forward<Self>(self).start()[n * std::forward<Self>(self).step()]} noexcept;
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
                {start() < stop()} noexcept -> meta::nothrow::truthy;
            } && (strictly_positive<Step> || requires{
                {step() < 0} noexcept -> meta::nothrow::truthy;
                {start() > stop()} noexcept -> meta::nothrow::truthy;
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
            noexcept (requires{{start() == stop()} noexcept -> meta::nothrow::truthy;})
            requires (subrange_equal<Start, Stop, Step>)
        {
            return bool(start() == stop());
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{
                {index() >= difference_type(stop())} noexcept -> meta::nothrow::truthy;
            })
            requires (subrange_counted<Start, Stop, Step>)
        {
            return bool(index() >= difference_type(stop()));
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{stop()(start())} noexcept -> meta::nothrow::truthy;})
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
            noexcept (requires{{index() == other.index()} noexcept -> meta::nothrow::truthy;})
            requires (requires{{index() == other.index()} -> meta::truthy;})
        {
            return bool(index() == other.index());
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
                {increment()} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {increment()};
            })
        {
            copy tmp = begin();
            increment();
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
                {decrement()} noexcept;
            })
            requires (requires(copy tmp) {
                {begin()};
                {decrement()};
            })
        {
            copy tmp = begin();
            decrement();
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

    template <typename Start, typename Stop = trivial, typename Step = trivial>
    subrange(Start&&, Stop&& stop, Step&& step = {}) -> subrange<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >;

}


namespace iter {

    /* A generalized `swap()` operator that allows any type in the `bertrand::iter`
    namespace that exposes a `.swap()` member method to be used in conjunction with
    `meta::swap()`. */
    template <typename T>
    constexpr void swap(T& lhs, T& rhs)
        noexcept (requires{{lhs.swap(rhs)} noexcept;})
        requires (requires{{lhs.swap(rhs)};})
    {
        lhs.swap(rhs);
    }

    template <impl::range_concept C = impl::empty_range<>>
    struct range;

    template <typename C>
    range(C&&) -> range<meta::remove_rvalue<C>>;

    template <typename Start, typename Stop = impl::trivial, typename Step = impl::trivial>
        requires (!meta::iterator<Start>)
    range(Start&&, Stop&&, Step&& = {}) -> range<impl::iota<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >>;

    template <typename Start, typename Stop = impl::trivial, typename Step = impl::trivial>
        requires (meta::iterator<Start>)
    range(Start&&, Stop&&, Step&& = {}) -> range<impl::subrange<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >>;

    /* A special case of `range` that allows it to adapt to tuple-like types that are
    not otherwise iterable.  This works by dispatching to a reference array that gets
    populated when the range is constructed as long as the tuple contains only a single
    type, or a static vtable filled with function pointers that extract the
    corresponding value when called.  In the latter case, the return type may be
    promoted to a `Union` in order to model heterogenous tuples. */
    template <impl::range_concept C>
        requires (!meta::iterable<meta::as_lvalue<C>> && meta::tuple_like<C>)
    struct range<C> : range<impl::tuple_range<C>> {
        using range<impl::tuple_range<C>>::range;
        using range<impl::tuple_range<C>>::operator=;
    };

    /* A special case of `range` that contains only a single, non-iterable element.
    Default-constructing this range will create a range of zero elements instead. */
    template <impl::range_concept C>
        requires (!meta::iterable<meta::as_lvalue<C>> && !meta::tuple_like<C>)
    struct range<C> : range<impl::scalar<C>> {
        using range<impl::scalar<C>>::range;
        using range<impl::scalar<C>>::operator=;
    };

}


namespace meta {

    /* Perfectly forward the argument or wrap it in a range if it is not already one.
    This is equivalent to conditionally compiling an `iter::range()` constructor based
    on the state of the `meta::range<T>` concept, and always returns the same type as
    `meta::as_range<T>`.  It can be useful in generic algorithms that may accept ranges
    or other containers, but always want to normalize to ranges in order to standardize
    behavior.  It is used internally to implement various range adaptors. */
    template <typename T>
    [[nodiscard]] constexpr decltype(auto) to_range(T&& t)
        noexcept (meta::range<T> || requires{{iter::range{::std::forward<T>(t)}} noexcept;})
        requires (meta::range<T> || requires{{iter::range{::std::forward<T>(t)}};})
    {
        if constexpr (meta::range<T>) {
            return (::std::forward<T>(t));
        } else {
            return iter::range(::std::forward<T>(t));
        }
    }

    /* Convert the type to a range monad, assuming `T` does not already satisfy
    `meta::range`.  Otherwise, return the original type unchanged.  This can never
    create a nested range, and is equivalent to the type returned by the
    `iter::range()` constructor. */
    template <typename T>
    using as_range = remove_rvalue<decltype((to_range(::std::declval<T>())))>;

    /* Perfectly forward the argument or wrap it in a scalar range if it is not already
    a range.  This is equivalent to `meta::to_range()`, except that non-range arguments
    are always treated as ranges of a single element, regardless of whether they are
    iterable or tuple-like. */
    template <typename T>
    [[nodiscard]] constexpr decltype(auto) to_range_or_scalar(T&& t)
        noexcept (meta::range<T> || requires{{
            iter::range<impl::scalar<meta::remove_rvalue<T>>>{::std::forward<T>(t)}
        } noexcept;})
        requires (meta::range<T> || requires{{
            iter::range<impl::scalar<meta::remove_rvalue<T>>>{::std::forward<T>(t)}
        };})
    {
        if constexpr (meta::range<T>) {
            return (::std::forward<T>(t));
        } else {
            return iter::range<impl::scalar<meta::remove_rvalue<T>>>(::std::forward<T>(t));
        }
    }

    /* Convert the type to a scalar range monad, assuming `T` does not already satisfy
    `meta::range`.  Otherwise, return the original type unchanged.  This can never
    create a nested range, and is equivalent to the type returned by the
    `meta::to_range_or_scalar()` method. */
    template <typename T>
    using as_range_or_scalar = remove_rvalue<decltype((to_range_or_scalar(::std::declval<T>())))>;

    namespace detail {

        template <typename T>
        constexpr bool prefer_constructor<iter::range<T>> = true;

        template <typename T>
        constexpr bool range<iter::range<T>> = true;

        template <typename T>
        constexpr bool range_transparent<impl::scalar<T>> = true;
        template <typename T>
        constexpr bool range_transparent<impl::tuple_range<T>> = true;

        template <typename T>
        constexpr bool scalar<impl::scalar<T>> = true;

        template <typename Start, typename Stop, typename Step>
        constexpr bool iota<impl::iota<Start, Stop, Step>> = true;

        template <typename Start, typename Stop, typename Step>
        constexpr bool subrange<impl::subrange<Start, Stop, Step>> = true;

        // template <typename C>

    }

}


namespace impl {

    namespace range_compare {

        /* Lexicographic comparison functions are permitted as long as they accept 2
        arguments and produce an STL ordering tag. */
        template <typename F, typename L, typename R>
        concept func = requires(
            meta::as_const_ref<F> func,
            meta::remove_range<meta::as_const_ref<L>> lhs,
            meta::remove_range<meta::as_const_ref<R>> rhs
        ) {
            {func(lhs, rhs)} -> meta::std::ordering;
        };

        template <typename F, typename L, typename R> requires (func<F, L, R>)
        using type = meta::unqualify<meta::call_type<
            meta::as_const_ref<F>,
            meta::remove_range<meta::as_const_ref<L>>,
            meta::remove_range<meta::as_const_ref<R>>
        >>;

        /* If a lexicographic comparison is invoked with more than 2 arguments, then
        the function will be braodcasted over each logical pair, and must be separately
        invocable for each one. */
        template <typename F, typename L, typename R, typename... A>
        constexpr bool recursive = false;
        template <typename F, typename L, typename R>
        constexpr bool recursive<F, L, R> = func<F, L, R>;
        template <typename F, typename L, typename R, typename A, typename... As>
            requires (func<F, L, R>)
        constexpr bool recursive<F, L, R, A, As...> = recursive<F, R, A, As...>;

        /* If a lexicographic comparison is invoked with more than 2 arguments, then
        there must be a single common return type, which allows widening from stronger
        ordering constraints to weaker ones in case of inconsistency. */
        template <typename out, typename F, typename L, typename... T>
        struct _common_type { using type = out; };
        template <typename out, typename F, typename L, typename R, typename... T>
        struct _common_type<out, F, L, R, T...> :
            _common_type<std::common_comparison_category<out, type<F, L, R>>, F, R, T...>
        {};
        template <typename F, typename L, typename R, typename... T>
        struct common_type : _common_type<type<F, L, R>, F, R, T...> {};

        /* If a lexicographic comparison is invoked with more than 2 arguments, then
        the actual function object will recursively consume the remaining arguments
        until either the function returns a non-equal result or until there are only 2
        arguments left. */
        template <typename type, typename F, typename L, typename R, typename... A>
        struct fn {
            [[nodiscard]] static constexpr type operator()(
                const F& func,
                const L& lhs,
                const R& rhs,
                const A&... rest
            )
                noexcept (
                    meta::nothrow::call_returns<type, const F&, const L&, const R&> &&
                    meta::nothrow::call_returns<
                        type,
                        fn<type, F, R, A...>,
                        const F&,
                        const R&,
                        const A&...
                    >
                )
                requires (
                    meta::call_returns<type, const F&, const L&, const R&> &&
                    meta::call_returns<
                        type,
                        fn<type, F, R, A...>,
                        const F&,
                        const R&,
                        const A&...
                    >
                )
            {
                if (type result = func(lhs, rhs); result != 0) {
                    return result;
                }
                return fn<type, F, R, A...>{}(func, rhs, rest...);
            }
        };
        template <typename type, typename F, typename L, typename R>
        struct fn<type, F, L, R> {
            [[nodiscard]] static constexpr type operator()(
                const F& func,
                const L& lhs,
                const R& rhs
            )
                noexcept (meta::nothrow::call_returns<type, const F&, const L&, const R&>)
                requires (meta::call_returns<type, const F&, const L&, const R&>)
            {
                return func(lhs, rhs);
            }
        };

        /* The default comparison function will attempt to use the `<=>` operator if it
        is available or fall back to the `<`, `>`, and `==` operators, with a result of
        type `std::partial_ordering`. */
        struct trivial {
            template <typename L, typename R>
            [[nodiscard]] static constexpr auto operator()(const L& lhs, const R& rhs)
                noexcept (requires{{lhs <=> rhs} noexcept;})
                requires (requires{{lhs <=> rhs} -> meta::std::ordering;})
            {
                return lhs <=> rhs;
            }
            template <typename L, typename R>
            [[nodiscard]] static constexpr auto operator()(const L& lhs, const R& rhs)
                noexcept (requires{{lhs == rhs} noexcept -> meta::nothrow::truthy;} && (
                    !requires{{lhs < rhs} -> meta::truthy;} ||
                    requires{{lhs < rhs} noexcept -> meta::nothrow::truthy;}
                ) && (
                    !requires{{lhs > rhs} -> meta::truthy;} ||
                    requires{{lhs > rhs} noexcept -> meta::nothrow::truthy;}
                ))
                requires (
                    !requires{{lhs <=> rhs} -> meta::std::ordering;} &&
                    requires{{lhs == rhs} -> meta::truthy;}
                )
            {
                if (lhs == rhs) {
                    return std::partial_ordering::equivalent;
                }
                if constexpr (requires{{lhs < rhs} -> meta::truthy;}) {
                    if (lhs < rhs) {
                        return std::partial_ordering::less;
                    }
                }
                if constexpr (requires{{lhs > rhs} -> meta::truthy;}) {
                    if (lhs > rhs) {
                        return std::partial_ordering::greater;
                    }
                }
                return std::partial_ordering::unordered;
            }
        };

    }

    namespace range_contains {

        template <typename K, typename A>
        [[nodiscard]] constexpr bool scalar(const K& k, const A& a)
            noexcept (requires{{
                meta::from_range(k) == meta::from_range(a)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                meta::from_range(k) == meta::from_range(a)
            } -> meta::convertible_to<bool>;})
        {
            return meta::from_range(k) == meta::from_range(a);
        }

        template <typename K, typename A>
        [[nodiscard]] constexpr bool scalar(const K& k, const A& a)
            noexcept (requires{{
                meta::from_range(k)(meta::from_range(a))
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                meta::from_range(k)(meta::from_range(a))
            } -> meta::convertible_to<bool>;})
        {
            return meta::from_range(k)(meta::from_range(a));
        }

    }

    /* Given a pack of types `A...`, find all indices in `A...` that correspond to
    ranges, and generate a matching index sequence. */
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

    /* A helper tag that distinguishes forward iterators from reverse iterators for
    range algorithms that unify the two.  Such algorithms can accept either this or
    `range_reverse` as a template parameter to specialize accordingly.  Invoking the
    tag's `begin()` or `end()` methods with an iterable container will return a pair
    of forward or reverse iterators, respectively. */
    struct range_forward {
        template <typename T>
        [[nodiscard]] static constexpr decltype(auto) begin(T&& container)
            noexcept (requires{{meta::begin(std::forward<T>(container))} noexcept;})
            requires (requires{{meta::begin(std::forward<T>(container))};})
        {
            return (meta::begin(std::forward<T>(container)));
        }
        template <typename T>
        [[nodiscard]] static constexpr decltype(auto) end(T&& container)
            noexcept (requires{{meta::end(std::forward<T>(container))} noexcept;})
            requires (requires{{meta::end(std::forward<T>(container))};})
        {
            return (meta::end(std::forward<T>(container)));
        }
    };
    struct range_reverse {
        template <typename T>
        [[nodiscard]] static constexpr decltype(auto) begin(T&& container)
            noexcept (requires{{meta::rbegin(std::forward<T>(container))} noexcept;})
            requires (requires{{meta::rbegin(std::forward<T>(container))};})
        {
            return (meta::rbegin(std::forward<T>(container)));
        }
        template <typename T>
        [[nodiscard]] static constexpr decltype(auto) end(T&& container)
            noexcept (requires{{meta::rend(std::forward<T>(container))} noexcept;})
            requires (requires{{meta::rend(std::forward<T>(container))};})
        {
            return (meta::rend(std::forward<T>(container)));
        }
    };

    template <typename T>
    concept range_direction = std::same_as<T, range_forward> || std::same_as<T, range_reverse>;

    inline constexpr ValueError min_empty_error() noexcept {
        return ValueError{"empty range has no minimum value"};
    }

    inline constexpr ValueError max_empty_error() noexcept {
        return ValueError{"empty range has no maximum value"};
    }

    inline constexpr ValueError minmax_empty_error() noexcept {
        return ValueError{"empty range has no minimum or maximum value"};
    }

    /* `min{}`, `max{}`, and `minmax{}` return the common type between all arguments
    if one exists, or a union type otherwise. */
    template <typename... A>
    struct _range_extreme {
        using type = meta::make_union<meta::remove_rvalue<meta::remove_range<
            meta::yield_type<meta::as_range_or_scalar<A>>
        >>...>;
    };
    template <typename... A>
        requires (meta::has_common_type<meta::remove_rvalue<meta::remove_range<
            meta::yield_type<meta::as_range_or_scalar<A>>
        >>...>)
    struct _range_extreme<A...> {
        using type = meta::common_type<meta::remove_rvalue<meta::remove_range<
            meta::yield_type<meta::as_range_or_scalar<A>>
        >>...>;
    };
    template <typename... A>
    using range_extreme = _range_extreme<A...>::type;

}


namespace iter {

    /* Range-based logical conjunction operator.  Accepts any number of arguments that
    are explicitly convertible to `bool` and returns true if all of them evaluate to
    true.  A custom function predicate may be supplied as a constructor argument, which
    will be applied to each value.  If a range is given and the function is not
    immediately callable with its underlying value, then the function may be
    broadcasted over all its elements before advancing to the next argument.  If the
    elements are themselves ranges, then this process may recur until either the
    function becomes callable or a scalar value is reached. */
    template <meta::not_rvalue F = impl::ExplicitConvertTo<bool>>
    struct all {
        [[no_unique_address]] F func;

        template <typename A>
        [[nodiscard]] constexpr bool operator()(A&& a) const
            noexcept (requires{
                {func(meta::from_range(std::forward<A>(a)))} noexcept -> meta::nothrow::truthy;
            })
            requires (requires{
                {func(meta::from_range(std::forward<A>(a)))} -> meta::truthy;
            })
        {
            return bool(func(meta::from_range(std::forward<A>(a))));
        }

        template <meta::range A>
        [[nodiscard]] constexpr bool operator()(A&& a) const
            noexcept (
                meta::nothrow::iterable<meta::as_lvalue<A>> &&
                requires(meta::yield_type<meta::as_lvalue<A>> x) {
                    {!operator()(std::forward<decltype(x)>(x))} noexcept -> meta::nothrow::truthy;
                }
            )
            requires (
                !requires{{func(meta::from_range(std::forward<A>(a)))} -> meta::truthy;} &&
                !meta::scalar<A> &&
                meta::iterable<meta::as_lvalue<A>> &&
                requires(meta::yield_type<meta::as_lvalue<A>> x) {
                    {!operator()(std::forward<decltype(x)>(x))} -> meta::truthy;
                }
            )
        {
            for (auto&& x : a) {
                if (!operator()(std::forward<decltype(x)>(x))) {
                    return false;
                }
            }
            return true;
        }

        template <typename... A> requires (sizeof...(A) != 1)
        [[nodiscard]] constexpr bool operator()(A&&... a) const
            noexcept (requires{{(operator()(std::forward<A>(a)) && ...)} noexcept;})
            requires (requires{{(operator()(std::forward<A>(a)) && ...)};})
        {
            return (operator()(std::forward<A>(a)) && ...);
        }
    };
    template <typename F>
    all(F&&) -> all<meta::remove_rvalue<F>>;

    /* Range-based logical disjunction operator.  Accepts any number of arguments that
    are explicitly convertible to `bool` and returns true if at least one evaluates to
    true.  A custom function predicate may be supplied as a constructor argument, which
    will be applied to each value.  If a range is given and the function is not
    immediately callable with its underlying value, then the function may be
    broadcasted over all its elements before advancing to the next argument.  If the
    elements are themselves ranges, then this process may recur until either the
    predicate becomes callable or a scalar value is reached. */
    template <meta::not_rvalue F = impl::ExplicitConvertTo<bool>>
    struct any {
        [[no_unique_address]] F func;

        template <typename A>
        [[nodiscard]] constexpr bool operator()(A&& a) const
            noexcept (requires{
                {func(meta::from_range(std::forward<A>(a)))} noexcept -> meta::nothrow::truthy;
            })
            requires (requires{
                {func(meta::from_range(std::forward<A>(a)))} -> meta::truthy;
            })
        {
            return bool(func(meta::from_range(std::forward<A>(a))));
        }

        template <meta::range A>
        [[nodiscard]] constexpr bool operator()(A&& a) const
            noexcept (
                meta::nothrow::iterable<meta::as_lvalue<A>> &&
                requires(meta::yield_type<meta::as_lvalue<A>> x) {
                    {operator()(std::forward<decltype(x)>(x))} noexcept -> meta::nothrow::truthy;
                }
            )
            requires (
                !requires{{func(meta::from_range(std::forward<A>(a)))} -> meta::truthy;} &&
                !meta::scalar<A> &&
                meta::iterable<meta::as_lvalue<A>> &&
                requires(meta::yield_type<meta::as_lvalue<A>> x) {
                    {operator()(std::forward<decltype(x)>(x))} -> meta::truthy;
                }
            )
        {
            for (auto&& x : a) {
                if (operator()(std::forward<decltype(x)>(x))) {
                    return true;
                }
            }
            return false;
        }

        template <typename... A> requires (sizeof...(A) != 1)
        [[nodiscard]] constexpr bool operator()(A&&... a) const
            noexcept (requires{{(operator()(std::forward<A>(a)) || ...)} noexcept;})
            requires (requires{{(operator()(std::forward<A>(a)) || ...)};})
        {
            return (operator()(std::forward<A>(a)) || ...);
        }
    };
    template <typename F>
    any(F&&) -> any<meta::remove_rvalue<F>>;

    /* Range-based lexicographic comparison operator.

    This function object can be initialized using any comparison predicate that takes
    two immutable values representing the left and right sides of a `<=>` comparison
    and produces an STL ordering tag (`std::strong_ordering`, `std::weak_ordering`, or
    `std::partial_ordering`) indicating their relative order.  Calling the function
    object with two arguments will invoke the comparison over each pair of elements and
    halt at the first non-equivalent result, following lexicographic order.  For
    ranges where the predicate is not immediately callable with their underlying value,
    the comparison will be broadcasted over each pair of elements until either a
    non-equivalent result is found or one of the ranges is exhausted.  If one range is
    shorter than another, then it will be considered to be less than the longer one,
    following lexicographic order.  If a range happens to yield another range as its
    element type, then this process may recur until either the predicate becomes
    callable or scalar values are reached.  Non-range arguments will be implicitly
    broadcasted to match the length of the other argument if it is a range.

    This object can also be called with more than 2 arguments, in which case the
    lexicographic comparison will proceed pairwise, overlapping from left to right
    until a non-equivalent result is found or all arguments have been compared (similar
    to `a <=> b <=> c`, if that were valid).  In that case, the common ordering type
    between all comparisons will be returned, which may be weaker than the strongest
    individual comparison.

    The default comparison predicate is equivalent to the `<=>` operator if it is
    available.  Otherwise, it will fall back to a combination of the `<`, `>`, and `==`
    operators together with a `std::partial_ordering` result type. */
    template <meta::not_rvalue F = impl::range_compare::trivial>
    struct compare {
        [[no_unique_address]] F func;

        template <typename L, typename R> requires (impl::range_compare::func<F, L, R>)
        [[nodiscard]] constexpr auto operator()(const L& lhs, const R& rhs) const
            noexcept (meta::nothrow::call_returns<
                impl::range_compare::type<F, L, R>,
                meta::as_const_ref<F>,
                decltype((meta::from_range(lhs))),
                decltype((meta::from_range(rhs)))
            >)
        {
            return func(meta::from_range(lhs), meta::from_range(rhs));
        }

        template <typename L, typename R> requires (!impl::range_compare::func<F, L, R>)
        [[nodiscard]] constexpr auto operator()(const L& lhs, const R& rhs) const
            noexcept (
                meta::nothrow::iterable<const R&> &&
                requires(meta::begin_type<const R&> it) {{operator()(lhs, *it)} noexcept;}
            )
            requires (
                !meta::range<L> &&
                meta::range<R> &&
                meta::iterable<const R&> &&
                requires(meta::begin_type<const R&> it) {{operator()(lhs, *it)};}
            )
        {
            auto it = meta::begin(rhs);
            auto end = meta::end(rhs);
            while (it != end) {
                if (auto cmp = operator()(lhs, *it); cmp != 0) {
                    return cmp;
                }
                ++it;
            }
            return decltype(operator()(lhs, *it))::equivalent;
        }

        template <typename L, typename R> requires (!impl::range_compare::func<F, L, R>)
        [[nodiscard]] constexpr auto operator()(const L& lhs, const R& rhs) const
            noexcept (
                meta::nothrow::iterable<const L&> &&
                requires(meta::begin_type<const L&> it) {{operator()(*it, rhs)} noexcept;}
            )
            requires (
                meta::range<L> &&
                !meta::range<R> &&
                meta::iterable<const L&> &&
                requires(meta::begin_type<const L&> it) {{operator()(*it, rhs)};}
            )
        {
            auto it = meta::begin(lhs);
            auto end = meta::end(lhs);
            while (it != end) {
                if (auto cmp = operator()(*it, rhs); cmp != 0) {
                    return cmp;
                }
                ++it;
            }
            return decltype(operator()(lhs, *it))::equivalent;
        }

        template <typename L, typename R> requires (!impl::range_compare::func<F, L, R>)
        [[nodiscard]] constexpr auto operator()(const L& lhs, const R& rhs) const
            noexcept (
                meta::nothrow::iterable<const L&> &&
                meta::nothrow::iterable<const R&> &&
                requires(meta::begin_type<const L&> l_it, meta::begin_type<const R&> r_it) {
                    {operator()(*l_it, *r_it)} noexcept;
                }
            )
            requires (
                meta::range<L> &&
                meta::range<R> &&
                meta::iterable<const L&> &&
                meta::iterable<const R&> &&
                requires(meta::begin_type<const L&> l_it, meta::begin_type<const R&> r_it) {
                    {operator()(*l_it, *r_it)};
                }
            )
        {
            auto l_it = meta::begin(lhs);
            auto l_end = meta::end(lhs);
            auto r_it = meta::begin(rhs);
            auto r_end = meta::end(rhs);
            while (l_it != l_end && r_it != r_end) {
                if (auto cmp = operator()(*l_it, *r_it); cmp != 0) {
                    return cmp;
                }
                ++l_it;
                ++r_it;
            }
            if (l_it != l_end) {
                return decltype(operator()(*l_it, *r_it))::greater;
            }
            if (r_it != r_end) {
                return decltype(operator()(*l_it, *r_it))::less;
            }
            return decltype(operator()(*l_it, *r_it))::equivalent;
        }

        template <typename... A> requires (sizeof...(A) > 2)
        [[nodiscard]] constexpr auto operator()(const A&... a) const
            noexcept (requires{{impl::range_compare::fn<
                typename impl::range_compare::common_type<F, A...>::type,
                compare,
                A...
            >{}(*this, a...)} noexcept;})
            requires (
                impl::range_compare::recursive<F, A...> &&
                requires{{impl::range_compare::fn<
                    typename impl::range_compare::common_type<F, A...>::type,
                    compare,
                    A...
                >{}(*this, a...)};}
            )
        {
            return impl::range_compare::fn<
                typename impl::range_compare::common_type<F, A...>::type,
                compare,
                A...
            >{}(*this, a...);
        }
    };
    template <typename F>
    compare(F&&) -> compare<meta::remove_rvalue<F>>;

    /* Check to see whether a particular value or consecutive subsequence is present
    in the arguments.

    The value or subsequence to search for must be provided as a constructor argument
    used to initialize the function object, which can then be called with an arbitrary
    number of arguments, searching each one from left to right until a match is found.
    Each argument will first be converted into a range if it is not one already, and
    then iterated over to find the given key.

    If the key is given as a non-range or scalar value, then the search will consist of
    a simple sequence of `key == element` comparisons or `key(element)` invocations if
    comparisons are invalid and the key function produces a boolean result.  If the key
    is given as a non-empty or non-scalar range, then the search will attempt to match
    each value in the key to a consecutive element in the argument range using the same
    check as for scalars (possibly allowing for ranges of boolean predicates).  Empty
    ranges will never match, both for keys and arguments.

    Note that subsequence searches will never cross argument boundaries; that is, the
    end of one argument and the beginning of the next will not be treated as adjacent
    for the purpose of matching a subsequence. */
    template <meta::not_rvalue T>
    struct contains {
        using key_type = meta::as_range_or_scalar<T>;
        [[no_unique_address]] key_type key;

    private:
        template <meta::range A>
        constexpr bool subsequence(const A& a) const
            noexcept (requires(
                meta::begin_type<meta::as_const_ref<key_type>> key_begin,
                meta::begin_type<meta::as_const_ref<key_type>> key_it,
                meta::end_type<meta::as_const_ref<key_type>> key_end,
                meta::begin_type<const A&> arg_it,
                meta::end_type<const A&> arg_end
            ) {
                {key.begin()} noexcept -> meta::nothrow::copyable;
                {key.end()} noexcept;
                {key_it == key_end} noexcept -> meta::nothrow::truthy;
                {a.begin()} noexcept -> meta::nothrow::copyable;
                {a.end()} noexcept;
                {arg_it != arg_end} noexcept -> meta::nothrow::truthy;
                {impl::range_contains::scalar(*key_it, *arg_it)} noexcept;
                {++key_it} noexcept;
                {key_it = key_begin} noexcept;
                {++arg_it} noexcept;
            })
        {
            auto key_begin = key.begin();
            auto key_end = key.end();
            if (key_begin == key_end) {
                return false;  // empty range
            }
            auto key_it = key_begin;
            auto arg_it = a.begin();
            auto arg_end = a.end();
            while (arg_it != arg_end) {
                if (impl::range_contains::scalar(*key_it, *arg_it)) {
                    ++key_it;
                    if (key_it == key_end) {
                        return true;
                    }
                    ++arg_it;
                    auto tmp = arg_it;
                    while (tmp != arg_end && impl::range_contains::scalar(*key_it, *tmp)) {
                        ++key_it;
                        if (key_it == key_end) {
                            return true;
                        }
                        ++tmp;
                    }
                    key_it = key_begin;
                } else {
                    ++arg_it;
                }
            }
            return false;
        }

    public:
        template <typename A>
        [[nodiscard]] constexpr bool operator()(const A& a) const
            noexcept (requires{{subsequence(meta::to_range(a))} noexcept;})
            requires (requires(
                meta::begin_type<meta::as_const_ref<key_type>> key_begin,
                meta::begin_type<meta::as_const_ref<key_type>> key_it,
                meta::end_type<meta::as_const_ref<key_type>> key_end,
                meta::as_range<const A&> arg,
                meta::begin_type<meta::as_range<const A&>> arg_it,
                meta::end_type<meta::as_range<const A&>> arg_end
            ) {
                {key.begin()} -> meta::copyable;
                {key.end()};
                {key_it == key_end} -> meta::truthy;
                {arg.begin()} -> meta::copyable;
                {arg.end()};
                {arg_it != arg_end} -> meta::truthy;
                {impl::range_contains::scalar(*key_it, *arg_it)};
                {++key_it};
                {key_it = key_begin};
                {++arg_it};
            })
        {
            return subsequence(meta::to_range(a));
        }

        template <typename... A> requires (sizeof...(A) != 1)
        [[nodiscard]] constexpr bool operator()(const A&... a) const
            noexcept (requires{{(operator()(a) || ...)} noexcept;})
            requires (requires{{(operator()(a) || ...)};})
        {
            return (operator()(a) || ...);
        }
    };
    template <meta::not_rvalue T> requires (meta::scalar<meta::as_range_or_scalar<T>>)
    struct contains<T> {
        using key_type = meta::as_range_or_scalar<T>;
        [[no_unique_address]] key_type key;

        template <typename A>
        [[nodiscard]] constexpr bool operator()(const A& a) const
            noexcept (
                meta::nothrow::iterable<meta::as_range<const A&>> &&
                requires(meta::as_const_ref<meta::yield_type<meta::as_range<const A&>>> x) {
                    {impl::range_contains::scalar(key, x)};
                }
            )
            requires (meta::iterable<meta::as_range<const A&>> && (
                requires(meta::as_const_ref<meta::yield_type<meta::as_range<const A&>>> x) {
                    {*key == meta::from_range(x)} -> meta::convertible_to<bool>;
                } ||
                requires(meta::as_const_ref<meta::yield_type<meta::as_range<const A&>>> x) {
                    {(*key)(meta::from_range(x))} -> meta::convertible_to<bool>;
                }
            ))
        {
            for (const auto& x : meta::to_range(a)) {
                if (impl::range_contains::scalar(key, x)) {
                    return true;
                }
            }
            return false;
        }

        template <typename... A> requires (sizeof...(A) != 1)
        [[nodiscard]] constexpr bool operator()(const A&... a) const
            noexcept (requires{{(operator()(a) || ...)} noexcept;})
            requires (requires{{(operator()(a) || ...)};})
        {
            return (operator()(a) || ...);
        }
    };
    template <meta::not_rvalue T> requires (meta::empty_range<meta::as_range_or_scalar<T>>)
    struct contains<T> {
        using key_type = meta::as_range_or_scalar<T>;
        [[no_unique_address]] key_type key;

        template <typename... A>
        [[nodiscard]] static constexpr bool operator()(A&&...) noexcept { return false; }
    };
    template <typename T>
    contains(T&&) -> contains<meta::remove_rvalue<T>>;

    /* Count the number of occurrences of a particular value or consecutive subsequence
    in the arguments.

    This is a natural extension of `iter::contains<T>` which does not terminate upon
    finding the first match, but instead continues until all elements have been
    exhausted and then returns the total number of matches found.  All the same rules
    apply as for `iter::contains<T>` regarding how keys and arguments are treated.  The
    only difference is the addition of a default specialization for when no specific
    key is given, in which case the algorithm will simply report the total length of
    all arguments similar to a sum of `std::ranges::distance()` calls.  Such a size may
    be obtained in constant time if the argument range has a known size, or in linear
    time otherwise, possibly requiring a full traversal.

    Note that subsequences must be non-overlapping to be counted; that is, once a match
    is found, the search for the next match will continue from the element after the
    end of the previous match. */
    template <meta::not_rvalue T = void>
    struct count {
        using key_type = meta::as_range_or_scalar<T>;
        [[no_unique_address]] key_type key;

    private:
        template <meta::range A>
        constexpr size_t subsequence(const A& a) const
            noexcept (requires(
                meta::begin_type<meta::as_const_ref<key_type>> key_begin,
                meta::begin_type<meta::as_const_ref<key_type>> key_it,
                meta::end_type<meta::as_const_ref<key_type>> key_end,
                meta::begin_type<const A&> tmp,
                meta::begin_type<const A&> arg_it,
                meta::end_type<const A&> arg_end
            ) {
                {key.begin()} noexcept -> meta::nothrow::copyable;
                {key.end()} noexcept;
                {key_it == key_end} noexcept -> meta::nothrow::truthy;
                {a.begin()} noexcept -> meta::nothrow::copyable;
                {a.end()} noexcept;
                {arg_it != arg_end} noexcept -> meta::nothrow::truthy;
                {impl::range_contains::scalar(*key_it, *arg_it)} noexcept;
                {++key_it} noexcept;
                {key_it = key_begin} noexcept;
                {++arg_it} noexcept;
                {arg_it = tmp} noexcept;
            })
        {
            auto key_begin = key.begin();
            auto key_end = key.end();
            if (key_begin == key_end) {
                return 0;  // empty range
            }
            size_t total = 0;
            auto key_it = key_begin;
            auto arg_it = a.begin();
            auto arg_end = a.end();
            while (arg_it != arg_end) {
                if (impl::range_contains::scalar(*key_it, *arg_it)) {  // matches first key element
                    ++key_it;
                    ++arg_it;
                    if (key_it == key_end) {
                        ++total;
                        key_it = key_begin;  // reset key
                        continue;  // match is already non-overlapping
                    }
                    auto tmp = arg_it;
                    while (tmp != arg_end && impl::range_contains::scalar(*key_it, *tmp)) {
                        ++key_it;
                        ++tmp;
                        if (key_it == key_end) {
                            ++total;
                            key_it = key_begin;  // reset key
                            arg_it = tmp;  // force non-overlapping matches
                            continue;
                        }
                    }
                    key_it = key_begin;  // reset key
                } else {  // no match
                    ++arg_it;
                }
            }
            return total;
        }

    public:
        template <typename A>
        [[nodiscard]] constexpr size_t operator()(const A& a) const
            noexcept (requires{{subsequence(meta::to_range(a))} noexcept;})
            requires (requires(
                meta::begin_type<meta::as_const_ref<key_type>> key_begin,
                meta::begin_type<meta::as_const_ref<key_type>> key_it,
                meta::end_type<meta::as_const_ref<key_type>> key_end,
                meta::as_range<const A&> arg,
                meta::begin_type<meta::as_range<const A&>> tmp,
                meta::begin_type<meta::as_range<const A&>> arg_it,
                meta::end_type<meta::as_range<const A&>> arg_end
            ) {
                {key.begin()} -> meta::copyable;
                {key.end()};
                {key_it == key_end} -> meta::truthy;
                {arg.begin()} -> meta::copyable;
                {arg.end()};
                {arg_it != arg_end} -> meta::truthy;
                {impl::range_contains::scalar(*key_it, *arg_it)};
                {++key_it};
                {key_it = key_begin};
                {++arg_it};
                {arg_it = tmp};
            })
        {
            return subsequence(meta::to_range(a));
        }

        template <typename... A> requires (sizeof...(A) != 1)
        [[nodiscard]] constexpr size_t operator()(const A&... a) const
            noexcept (requires{{(operator()(a) + ... + 0)} noexcept;})
            requires (requires{{(operator()(a) + ... + 0)};})
        {
            return (operator()(a) + ... + 0);
        }
    };
    template <meta::not_rvalue T> requires (meta::scalar<meta::as_range_or_scalar<T>>)
    struct count<T> {
        using key_type = meta::as_range_or_scalar<T>;
        [[no_unique_address]] key_type key;

        template <typename A>
        [[nodiscard]] constexpr size_t operator()(const A& a) const
            noexcept (
                meta::nothrow::iterable<meta::as_range<const A&>> &&
                requires(meta::as_const_ref<meta::yield_type<meta::as_range<const A&>>> x) {
                    {impl::range_contains::scalar(key, x)};
                }
            )
            requires (meta::iterable<meta::as_range<const A&>> && (
                requires(meta::as_const_ref<meta::yield_type<meta::as_range<const A&>>> x) {
                    {*key == meta::from_range(x)} -> meta::convertible_to<bool>;
                } ||
                requires(meta::as_const_ref<meta::yield_type<meta::as_range<const A&>>> x) {
                    {(*key)(meta::from_range(x))} -> meta::convertible_to<bool>;
                }
            ))
        {
            size_t total = 0;
            for (const auto& x : meta::to_range(a)) {
                total += impl::range_contains::scalar(key, x);
            }
            return total;
        }

        template <typename... A> requires (sizeof...(A) != 1)
        [[nodiscard]] constexpr size_t operator()(const A&... a) const
            noexcept (requires{{(operator()(a) + ... + 0)} noexcept;})
            requires (requires{{(operator()(a) + ... + 0)};})
        {
            return (operator()(a) + ... + 0);
        }

    };
    template <meta::not_rvalue T> requires (meta::empty_range<meta::as_range_or_scalar<T>>)
    struct count<T> {
        using key_type = meta::as_range_or_scalar<T>;
        [[no_unique_address]] key_type key;

        template <typename... A>
        [[nodiscard]] static constexpr size_t operator()(A&&...) noexcept { return 0; }
    };
    template <>
    struct count<void> {
        template <typename A>
        [[nodiscard]] static constexpr size_t operator()(const A& a)
            noexcept (meta::nothrow::distance_returns<ssize_t, const A&>)
            requires (meta::distance_returns<ssize_t, const A&>)
        {
            ssize_t dist = meta::distance(a);
            return size_t(dist) * (dist > 0);
        }

        template <typename... A> requires (sizeof...(A) != 1)
        [[nodiscard]] constexpr size_t operator()(const A&... a) const
            noexcept (requires{{(operator()(a) + ... + 0)} noexcept;})
            requires (requires{{(operator()(a) + ... + 0)};})
        {
            return (operator()(a) + ... + 0);
        }
    };
    template <typename T>
    count(T&&) -> count<meta::remove_rvalue<T>>;

    /* A function object that forces evaluation for a range algorithm which operates
    by side effect, such as a modified assignment operator.  This emits a simple loop
    using the argument's perfectly-forwarded `begin()` and `end()` iterators, which
    evaluates and then immediately discards the result of begin iterator's dereference
    operator at each step. */
    struct exhaust {
        template <meta::range T>
        static constexpr decltype(auto) operator()(T&& r)
            noexcept (meta::nothrow::iterable<T>)
            requires (meta::iterable<T>)
        {
            auto it = meta::begin(std::forward<T>(r));
            auto end = meta::end(std::forward<T>(r));
            while (it != end) {
                std::ignore = *it;  // force evaluation and silence [[nodiscard]] warnings
                ++it;
            }
            return (std::forward<T>(r));
        }
    };

    /* Extract the maximum value from a set of one or more ranges or scalar values.

    This function object can be initialized using any comparison predicate that takes
    two immutable values representing the left and right sides of a `<=>` comparison
    and produces an STL ordering tag (`std::strong_ordering`, `std::weak_ordering`, or
    `std::partial_ordering`) indicating their relative order, just like
    `iter::compare` and `iter::sort`.

    Calling the function object with a single, non-range or scalar argument will simply
    return that argument as-is (after removing any rvalue reference).  If the argument
    is a non-scalar, non-empty range, then the maximum element will be found by
    iterating over each element and comparing them pairwise using the given predicate,
    with the left element being replaced whenever a greater right element is found.
    If the range is empty, then an exception will be thrown as a debug assertion.

    If more than one argument is supplied, then the maximum value will be found for
    each argument individually and then compared pairwise to find the overall maximum
    among them.  If the individual maxima have different types, then the overall
    return type will be the common type between all of them (if one exists), or a union
    of the possible types if not.  The left operand of the predicate function will
    always be supplied as this result type.

    If the predicate returns a `std::partial_ordering`, then unordered comparisons
    (such as those with `NaN` values) will be handled by checking if each operand is
    unordered with respect to itself, which identifies `NaN`s.  In that case, non-`NaN`
    values will be preferred over `NaN`s.  If both operands are `NaN`, then the left
    operand will be preferred.  This causes `NaN`s to be naturally excluded from
    consideration as long as there is at least one non-`NaN` element to return. */
    template <meta::not_rvalue F = impl::range_compare::trivial>
    struct max {
        [[no_unique_address]] F func;

        template <typename A> requires (!meta::range<A> || meta::scalar<A>)
        [[nodiscard]] constexpr meta::remove_rvalue<meta::remove_range<A>> operator()(A&& a) const
            noexcept (meta::nothrow::convertible_to<
                meta::remove_rvalue<meta::remove_range<A>>,
                meta::remove_rvalue<meta::remove_range<A>>
            >)
            requires (meta::convertible_to<
                meta::remove_rvalue<meta::remove_range<A>>,
                meta::remove_rvalue<meta::remove_range<A>>
            >)
        {
            return meta::from_range(std::forward<A>(a));
        }

        template <meta::range A> requires (!meta::scalar<A> && !meta::empty_range<A>)
        [[nodiscard]] constexpr meta::remove_rvalue<meta::yield_type<A>> operator()(A&& a) const
            noexcept (
                (!DEBUG || meta::tuple_like<A>) &&
                meta::nothrow::iterable<A> &&
                meta::nothrow::callable<
                    meta::as_const_ref<F>,
                    meta::remove_range<meta::as_lvalue<meta::yield_type<A>>>,
                    meta::remove_range<meta::as_lvalue<meta::yield_type<A>>>
                > && (
                    meta::lvalue<meta::yield_type<A>> ||
                    meta::nothrow::move_assignable<meta::remove_rvalue<meta::yield_type<A>>>
                )
            )
            requires (impl::range_compare::func<
                F,
                meta::yield_type<A>,
                meta::yield_type<A>
            > && (
                meta::lvalue<meta::yield_type<A>> ||
                meta::move_assignable<meta::remove_rvalue<meta::yield_type<A>>>
            ))
        {
            if constexpr (DEBUG && !meta::tuple_like<A>) {
                if (a.empty()) {
                    throw impl::max_empty_error();
                }
            }
            auto it = meta::begin(std::forward<A>(a));
            auto end = meta::end(std::forward<A>(a));
            impl::ref<meta::remove_rvalue<meta::yield_type<A>>> res {*it};
            ++it;
            while (it != end) {
                impl::ref<meta::remove_rvalue<meta::yield_type<A>>> x {*it};
                auto cmp = func(meta::from_range(*res), meta::from_range(*x));
                if (cmp < 0) {
                    res = std::move(x);  // rebinds lvalues, move-assigns rvalues
                }
                if constexpr (meta::std::partial_ordering<decltype(cmp)>) {
                    if (
                        !(cmp >= 0) &&  // unordered, possibly NaN
                        func(meta::from_range(*res), meta::from_range(*res)) != 0 &&  // res is NaN
                        func(meta::from_range(*x), meta::from_range(*x)) == 0  // x is not NaN
                    ) {
                        res = std::move(x);
                    }
                }
                ++it;
            }
            return *std::move(res);
        }

    private:
        template <typename Out, typename L, typename R>
        constexpr Out choose(L&& lhs, R&& rhs) const
            noexcept (
                meta::nothrow::callable<
                    meta::as_const_ref<F>,
                    meta::remove_range<meta::as_lvalue<L>>,
                    meta::remove_range<meta::as_lvalue<R>>
                > &&
                meta::nothrow::convertible_to<L, Out> &&
                meta::nothrow::convertible_to<R, Out>
            )
            requires (
                impl::range_compare::func<F, L, R> &&
                meta::convertible_to<L, Out> &&
                meta::convertible_to<R, Out>
            )
        {
            auto cmp = func(meta::from_range(lhs), meta::from_range(rhs));
            if (cmp < 0) {
                return std::forward<R>(rhs);
            }
            if constexpr (meta::std::partial_ordering<decltype(cmp)>) {
                if (
                    !(cmp >= 0) &&
                    func(meta::from_range(lhs), meta::from_range(lhs)) != 0 &&
                    func(meta::from_range(rhs), meta::from_range(rhs)) == 0
                ) {
                    return std::forward<R>(rhs);
                }
            }
            return std::forward<L>(lhs);
        }

        template <typename Out, typename Curr, typename... Rest>
        struct recur {
            static constexpr Out operator()(
                const max& self,
                Out out,
                meta::forward<Curr> curr,
                meta::forward<Rest>... rest
            )
                noexcept (requires{{recur<Out, Rest...>{}(
                    self,
                    self.template choose<Out>(
                        std::forward<Out>(out),
                        self(std::forward<Curr>(curr))
                    ),
                    std::forward<Rest>(rest)...
                )} noexcept;})
                requires (requires{{recur<Out, Rest...>{}(
                    self,
                    self.template choose<Out>(
                        std::forward<Out>(out),
                        self(std::forward<Curr>(curr))
                    ),
                    std::forward<Rest>(rest)...
                )};})
            {
                return recur<Out, Rest...>{}(
                    self,
                    self.template choose<Out>(
                        std::forward<Out>(out),
                        self(std::forward<Curr>(curr))
                    ),
                    std::forward<Rest>(rest)...
                );
            }
        };
        template <typename Out, typename Curr>
        struct recur<Out, Curr> {
            static constexpr Out operator()(
                const max& self,
                Out out,
                meta::forward<Curr> curr
            )
                noexcept (requires{{self.template choose<Out>(
                    std::forward<Out>(out),
                    self(std::forward<Curr>(curr))
                )} noexcept;})
                requires (requires{{self.template choose<Out>(
                    std::forward<Out>(out),
                    self(std::forward<Curr>(curr))
                )};})
            {
                return self.template choose<Out>(
                    std::forward<Out>(out),
                    self(std::forward<Curr>(curr))
                );
            }
        };

    public:
        template <typename A, typename... As> requires (sizeof...(As) > 0)
        [[nodiscard]] constexpr impl::range_extreme<A, As...> operator()(A&& a, As&&... as) const
            noexcept (requires{{recur<impl::range_extreme<A, As...>, As...>{}(
                *this,
                (*this)(std::forward<A>(a)),
                std::forward<As>(as)...
            )} noexcept;})
            requires (
                (meta::callable<const max&, A> && ... && meta::callable<const max&, As>) &&
                impl::range_compare::func<
                    F,
                    impl::range_extreme<A, As...>,
                    impl::range_extreme<A, As...>
                >
            )
        {
            return recur<impl::range_extreme<A, As...>, As...>{}(
                *this,
                (*this)(std::forward<A>(a)),
                std::forward<As>(as)...
            );
        }
    };
    template <typename F>
    max(F&&) -> max<meta::remove_rvalue<F>>;

    /* Extract the minimum value from a set of one or more ranges or scalar values.

    This function object can be initialized using any comparison predicate that takes
    two immutable values representing the left and right sides of a `<=>` comparison
    and produces an STL ordering tag (`std::strong_ordering`, `std::weak_ordering`, or
    `std::partial_ordering`) indicating their relative order, just like
    `iter::compare` and `iter::sort`.

    Calling the function object with a single, non-range or scalar argument will simply
    return that argument as-is (after removing any rvalue reference).  If the argument
    is a non-scalar, non-empty range, then the minimum element will be found by
    iterating over each element and comparing them pairwise using the given predicate,
    with the left element being replaced whenever a lesser right element is found.
    If the range is empty, then an exception will be thrown as a debug assertion.

    If more than one argument is supplied, then the minimum value will be found for
    each argument individually and then compared pairwise to find the overall minimum
    among them.  If the individual minima have different types, then the overall
    return type will be the common type between all of them (if one exists), or a union
    of the possible types if not.  The left operand of the predicate function will
    always be supplied as this result type.
    
    If the predicate returns a `std::partial_ordering`, then unordered comparisons
    (such as those with `NaN` values) will be handled by checking if each operand is
    unordered with respect to itself, which identifies `NaN`s.  In that case, non-`NaN`
    values will be preferred over `NaN`s.  If both operands are `NaN`, then the left
    operand will be preferred.  This causes `NaN`s to be naturally excluded from
    consideration as long as there is at least one non-`NaN` element to return. */
    template <meta::not_rvalue F = impl::range_compare::trivial>
    struct min {
        [[no_unique_address]] F func;

        template <typename A> requires (!meta::range<A> || meta::scalar<A>)
        [[nodiscard]] constexpr meta::remove_rvalue<meta::remove_range<A>> operator()(A&& a) const
            noexcept (meta::nothrow::convertible_to<
                meta::remove_rvalue<meta::remove_range<A>>,
                meta::remove_rvalue<meta::remove_range<A>>
            >)
            requires (meta::convertible_to<
                meta::remove_rvalue<meta::remove_range<A>>,
                meta::remove_rvalue<meta::remove_range<A>>
            >)
        {
            return meta::from_range(std::forward<A>(a));
        }

        template <meta::range A> requires (!meta::scalar<A> && !meta::empty_range<A>)
        [[nodiscard]] constexpr meta::remove_rvalue<meta::yield_type<A>> operator()(A&& a) const
            noexcept (
                (!DEBUG || meta::tuple_like<A>) &&
                meta::nothrow::iterable<A> &&
                meta::nothrow::callable<
                    meta::as_const_ref<F>,
                    meta::remove_range<meta::as_lvalue<meta::yield_type<A>>>,
                    meta::remove_range<meta::as_lvalue<meta::yield_type<A>>>
                > && (
                    meta::lvalue<meta::yield_type<A>> ||
                    meta::nothrow::move_assignable<meta::remove_rvalue<meta::yield_type<A>>>
                )
            )
            requires (impl::range_compare::func<
                F,
                meta::yield_type<A>,
                meta::yield_type<A>
            > && (
                meta::lvalue<meta::yield_type<A>> ||
                meta::move_assignable<meta::remove_rvalue<meta::yield_type<A>>>
            ))
        {
            if constexpr (DEBUG && !meta::tuple_like<A>) {
                if (a.empty()) {
                    throw impl::min_empty_error();
                }
            }
            auto it = meta::begin(std::forward<A>(a));
            auto end = meta::end(std::forward<A>(a));
            impl::ref<meta::remove_rvalue<meta::yield_type<A>>> res {*it};
            ++it;
            while (it != end) {
                impl::ref<meta::remove_rvalue<meta::yield_type<A>>> x {*it};
                auto cmp = func(meta::from_range(*res), meta::from_range(*x));
                if (cmp > 0) {
                    res = std::move(x);  // rebinds lvalues, move-assigns rvalues
                }
                if constexpr (meta::std::partial_ordering<decltype(cmp)>) {
                    if (
                        !(cmp <= 0) &&  // unordered, possibly NaN
                        func(meta::from_range(*res), meta::from_range(*res)) != 0 &&  // res is NaN
                        func(meta::from_range(*x), meta::from_range(*x)) == 0  // curr is not NaN
                    ) {
                        res = std::move(x);
                    }
                }
                ++it;
            }
            return *std::move(res);
        }

    private:
        template <typename Out, typename L, typename R>
        constexpr Out choose(L&& lhs, R&& rhs) const
            noexcept (
                meta::nothrow::callable<
                    meta::as_const_ref<F>,
                    meta::remove_range<meta::as_lvalue<L>>,
                    meta::remove_range<meta::as_lvalue<R>>
                > &&
                meta::nothrow::convertible_to<L, Out> &&
                meta::nothrow::convertible_to<R, Out>
            )
            requires (
                impl::range_compare::func<F, L, R> &&
                meta::convertible_to<L, Out> &&
                meta::convertible_to<R, Out>
            )
        {
            auto cmp = func(meta::from_range(lhs), meta::from_range(rhs));
            if (cmp > 0) {
                return std::forward<R>(rhs);
            }
            if constexpr (meta::std::partial_ordering<decltype(cmp)>) {
                if (
                    !(cmp <= 0) &&
                    func(meta::from_range(lhs), meta::from_range(lhs)) != 0 &&
                    func(meta::from_range(rhs), meta::from_range(rhs)) == 0
                ) {
                    return std::forward<R>(rhs);
                }
            }
            return std::forward<L>(lhs);
        }

        template <typename Out, typename Curr, typename... Rest>
        struct recur {
            static constexpr Out operator()(
                const min& self,
                Out out,
                meta::forward<Curr> curr,
                meta::forward<Rest>... rest
            )
                noexcept (requires{{recur<Out, Rest...>{}(
                    self,
                    self.template choose<Out>(
                        std::forward<Out>(out),
                        self(std::forward<Curr>(curr))
                    ),
                    std::forward<Rest>(rest)...
                )} noexcept;})
                requires (requires{{recur<Out, Rest...>{}(
                    self,
                    self.template choose<Out>(
                        std::forward<Out>(out),
                        self(std::forward<Curr>(curr))
                    ),
                    std::forward<Rest>(rest)...
                )};})
            {
                return recur<Out, Rest...>{}(
                    self,
                    self.template choose<Out>(
                        std::forward<Out>(out),
                        self(std::forward<Curr>(curr))
                    ),
                    std::forward<Rest>(rest)...
                );
            }
        };
        template <typename Out, typename Curr>
        struct recur<Out, Curr> {
            static constexpr Out operator()(
                const min& self,
                Out out,
                meta::forward<Curr> curr
            )
                noexcept (requires{{self.template choose<Out>(
                    std::forward<Out>(out),
                    self(std::forward<Curr>(curr))
                )} noexcept;})
                requires (requires{{self.template choose<Out>(
                    std::forward<Out>(out),
                    self(std::forward<Curr>(curr))
                )};})
            {
                return self.template choose<Out>(
                    std::forward<Out>(out),
                    self(std::forward<Curr>(curr))
                );
            }
        };

    public:
        template <typename A, typename... As> requires (sizeof...(As) > 0)
        [[nodiscard]] constexpr impl::range_extreme<A, As...> operator()(A&& a, As&&... as) const
            noexcept (requires{{recur<impl::range_extreme<A, As...>, As...>{}(
                *this,
                (*this)(std::forward<A>(a)),
                std::forward<As>(as)...
            )} noexcept;})
            requires (
                (meta::callable<const min&, A> && ... && meta::callable<const min&, As>) &&
                impl::range_compare::func<
                    F,
                    impl::range_extreme<A, As...>,
                    impl::range_extreme<A, As...>
                >
            )
        {
            return recur<impl::range_extreme<A, As...>, As...>{}(
                *this,
                (*this)(std::forward<A>(a)),
                std::forward<As>(as)...
            );
        }
    };
    template <typename F>
    min(F&&) -> min<meta::remove_rvalue<F>>;

    /* Simultaneously extract both the minimum and maximum values from a set of one or
    more ranges or scalar values.

    This function object combines the behaviors of `iter::min<F>` and `iter::max<F>`
    into a single call that returns both results as a trivial aggregate with `.min` and
    `.max` members (in that order) of the same type.  The result will always support
    structured bindings, and both values will be computed in a single pass.  Note that
    this will always require a copy to produce both initial values.  See `iter::min<F>`
    and `iter::max<F>` for all other details. */
    template <meta::not_rvalue F = impl::range_compare::trivial>
    struct minmax {
        [[no_unique_address]] F func;

        template <typename T>
        struct result {
            T min;
            T max;
        };

        template <typename A> requires (!meta::range<A> || meta::scalar<A>)
        [[nodiscard]] constexpr result<meta::remove_rvalue<meta::remove_range<A>>> operator()(
            A&& a
        ) const
            noexcept (meta::nothrow::convertible_to<
                meta::remove_rvalue<meta::remove_range<A>>,
                meta::remove_rvalue<meta::remove_range<A>>
            > && meta::nothrow::convertible_to<
                meta::remove_rvalue<meta::remove_range<meta::as_lvalue<A>>>,
                meta::remove_rvalue<meta::remove_range<A>>
            >)
            requires (meta::convertible_to<
                meta::remove_rvalue<meta::remove_range<A>>,
                meta::remove_rvalue<meta::remove_range<A>>
            > && meta::convertible_to<
                meta::remove_rvalue<meta::remove_range<meta::as_lvalue<A>>>,
                meta::remove_rvalue<meta::remove_range<A>>
            >)
        {
            return {
                .min = meta::from_range(a),  // possibly copies
                .max = meta::from_range(std::forward<A>(a))
            };
        }

        template <meta::range A> requires (!meta::scalar<A> && !meta::empty_range<A>)
        [[nodiscard]] constexpr result<meta::remove_rvalue<meta::yield_type<A>>> operator()(
            A&& a
        ) const
            noexcept (
                (!DEBUG || meta::tuple_like<A>) &&
                meta::nothrow::iterable<A> &&
                meta::nothrow::callable<
                    meta::as_const_ref<F>,
                    meta::remove_range<meta::as_lvalue<meta::yield_type<A>>>,
                    meta::remove_range<meta::as_lvalue<meta::yield_type<A>>>
                > && (
                    meta::lvalue<meta::yield_type<A>> ||
                    meta::nothrow::move_assignable<meta::remove_rvalue<meta::yield_type<A>>>
                )
            )
            requires (impl::range_compare::func<
                F,
                meta::yield_type<A>,
                meta::yield_type<A>
            > && (
                meta::lvalue<meta::yield_type<A>> ||
                meta::move_assignable<meta::remove_rvalue<meta::yield_type<A>>>
            ))
        {
            if constexpr (DEBUG && !meta::tuple_like<A>) {
                if (a.empty()) {
                    throw impl::min_empty_error();
                }
            }
            auto it = meta::begin(std::forward<A>(a));
            auto end = meta::end(std::forward<A>(a));
            impl::ref<meta::remove_rvalue<meta::yield_type<A>>> min {*it};
            impl::ref<meta::remove_rvalue<meta::yield_type<A>>> max {*min};
            ++it;
            while (it != end) {
                impl::ref<meta::remove_rvalue<meta::yield_type<A>>> x {*it};
                auto cmp = func(meta::from_range(*min), meta::from_range(*x));
                if (cmp > 0) {
                    min = std::move(x);  // rebinds lvalues, move-assigns rvalues
                }
                if constexpr (meta::std::partial_ordering<decltype(cmp)>) {
                    if (
                        !(cmp <= 0) &&  // unordered, possibly NaN
                        func(meta::from_range(*min), meta::from_range(*min)) != 0 &&  // min is NaN
                        func(meta::from_range(*x), meta::from_range(*x)) == 0  // curr is not NaN
                    ) {
                        min = std::move(x);
                    }
                }
                cmp = func(meta::from_range(*max), meta::from_range(*x));
                if (cmp < 0) {
                    max = std::move(x);  // rebinds lvalues, move-assigns rvalues
                }
                if constexpr (meta::std::partial_ordering<decltype(cmp)>) {
                    if (
                        !(cmp >= 0) &&  // unordered, possibly NaN
                        func(meta::from_range(*max), meta::from_range(*max)) != 0 &&  // max is NaN
                        func(meta::from_range(*x), meta::from_range(*x)) == 0  // curr is not NaN
                    ) {
                        max = std::move(x);
                    }
                }
                ++it;
            }
            return {
                .min = *std::move(min),
                .max = *std::move(max)
            };
        }

    private:
        template <typename Out, typename L, typename R>
        constexpr Out choose_min(L&& lhs, R&& rhs) const
            noexcept (
                meta::nothrow::callable<
                    meta::as_const_ref<F>,
                    meta::remove_range<meta::as_lvalue<L>>,
                    meta::remove_range<meta::as_lvalue<R>>
                > &&
                meta::nothrow::convertible_to<L, Out> &&
                meta::nothrow::convertible_to<R, Out>
            )
        {
            auto cmp = func(meta::from_range(lhs), meta::from_range(rhs));
            if (cmp > 0) {
                return std::forward<R>(rhs);
            }
            if constexpr (meta::std::partial_ordering<decltype(cmp)>) {
                if (
                    !(cmp <= 0) &&
                    func(meta::from_range(lhs), meta::from_range(lhs)) != 0 &&
                    func(meta::from_range(rhs), meta::from_range(rhs)) == 0
                ) {
                    return std::forward<R>(rhs);
                }
            }
            return std::forward<L>(lhs);
        }

        template <typename Out, typename L, typename R>
        constexpr Out choose_max(L&& lhs, R&& rhs) const
            noexcept (
                meta::nothrow::callable<
                    meta::as_const_ref<F>,
                    meta::remove_range<meta::as_lvalue<L>>,
                    meta::remove_range<meta::as_lvalue<R>>
                > &&
                meta::nothrow::convertible_to<L, Out> &&
                meta::nothrow::convertible_to<R, Out>
            )
        {
            auto cmp = func(meta::from_range(lhs), meta::from_range(rhs));
            if (cmp < 0) {
                return std::forward<R>(rhs);
            }
            if constexpr (meta::std::partial_ordering<decltype(cmp)>) {
                if (
                    !(cmp >= 0) &&
                    func(meta::from_range(lhs), meta::from_range(lhs)) != 0 &&
                    func(meta::from_range(rhs), meta::from_range(rhs)) == 0
                ) {
                    return std::forward<R>(rhs);
                }
            }
            return std::forward<L>(lhs);
        }

        template <typename Out, typename L, typename R>
        constexpr result<Out> choose(result<L>&& lhs, result<R>&& rhs) const
            noexcept (
                meta::nothrow::callable<
                    meta::as_const_ref<F>,
                    meta::remove_range<meta::as_lvalue<L>>,
                    meta::remove_range<meta::as_lvalue<R>>
                > &&
                meta::nothrow::convertible_to<L, Out> &&
                meta::nothrow::convertible_to<R, Out>
            )
            requires (
                impl::range_compare::func<F, L, R> &&
                meta::convertible_to<L, Out> &&
                meta::convertible_to<R, Out>
            )
        {
            return {
                .min = choose_min<Out>(
                    std::forward<L>(lhs.min),
                    std::forward<R>(rhs.min)
                ),
                .max = choose_max<Out>(
                    std::forward<L>(lhs.max),
                    std::forward<R>(rhs.max)
                )
            };
        }

        template <typename Out, typename Curr, typename... Rest>
        struct recur {
            static constexpr result<Out> operator()(
                const minmax& self,
                result<Out>&& out,
                meta::forward<Curr> curr,
                meta::forward<Rest>... rest
            )
                noexcept (requires{{recur<Out, Rest...>{}(
                    self,
                    self.template choose<Out>(
                        std::move(out),
                        self(std::forward<Curr>(curr))
                    ),
                    std::forward<Rest>(rest)...
                )} noexcept;})
                requires (requires{{recur<Out, Rest...>{}(
                    self,
                    self.template choose<Out>(
                        std::move(out),
                        self(std::forward<Curr>(curr))
                    ),
                    std::forward<Rest>(rest)...
                )};})
            {
                return recur<Out, Rest...>{}(
                    self,
                    self.template choose<Out>(
                        std::move(out),
                        self(std::forward<Curr>(curr))
                    ),
                    std::forward<Rest>(rest)...
                );
            }
        };
        template <typename Out, typename Curr>
        struct recur<Out, Curr> {
            static constexpr result<Out> operator()(
                const minmax& self,
                result<Out>&& out,
                meta::forward<Curr> curr
            )
                noexcept (requires{{self.template choose<Out>(
                    std::move(out),
                    self(std::forward<Curr>(curr))
                )} noexcept;})
                requires (requires{{self.template choose<Out>(
                    std::move(out),
                    self(std::forward<Curr>(curr))
                )};})
            {
                return self.template choose<Out>(
                    std::move(out),
                    self(std::forward<Curr>(curr))
                );
            }
        };

    public:
        template <typename A, typename... As> requires (sizeof...(As) > 0)
        [[nodiscard]] constexpr result<impl::range_extreme<A, As...>> operator()(
            A&& a,
            As&&... as
        ) const
            noexcept (requires{{recur<impl::range_extreme<A, As...>, As...>{}(
                *this,
                (*this)(std::forward<A>(a)),
                std::forward<As>(as)...
            )} noexcept;})
            requires (
                (meta::callable<const minmax&, A> && ... && meta::callable<const minmax&, As>) &&
                impl::range_compare::func<
                    F,
                    impl::range_extreme<A, As...>,
                    impl::range_extreme<A, As...>
                >
            )
        {
            return recur<impl::range_extreme<A, As...>, As...>{}(
                *this,
                (*this)(std::forward<A>(a)),
                std::forward<As>(as)...
            );
        }
    };
    template <typename F>
    minmax(F&&) -> minmax<meta::remove_rvalue<F>>;

}


namespace impl {

    namespace range_index {

        template <typename C>
        using vtable = impl::tuple_vtable<C>::dispatch;

        template <typename... Ts>
        concept runtime = (meta::std::type_identity<Ts> && ...);

        template <typename... Ts>
        concept comptime = (!meta::std::type_identity<Ts> && ...);

        template <typename... Ts>
        concept valid = runtime<Ts...> || comptime<Ts...>;

        template <typename C, typename A>
        concept offset_subscript =
            (!meta::tuple_like<C> || meta::tuple_size<C> > 0) &&
            requires(C c, A a) {
                {meta::begin(std::forward<C>(c))[std::forward<A>(a)]};
            } && (
                !meta::reverse_iterable<C> ||
                meta::unsigned_integer<A> ||
                requires(C c, A a) {
                    {meta::rbegin(std::forward<C>(c))[-std::forward<A>(a) - 1]} -> std::same_as<
                        decltype((meta::begin(std::forward<C>(c))[std::forward<A>(a)]))
                    >;
                }
            );

        template <typename C, typename A>
        concept offset_random_access =
            (!meta::tuple_like<C> || meta::tuple_size<C> > 0) &&
            requires(C c, A a, meta::begin_type<C> it) {
                {meta::begin(std::forward<C>(c))};
                {it += std::forward<A>(a)};
                {*it};
            } && (
                !meta::reverse_iterable<C> ||
                meta::unsigned_integer<A> ||
                requires(C c, A a, meta::begin_type<C> it, meta::rbegin_type<C> rit) {
                    {meta::rbegin(std::forward<C>(c))};
                    {rit += -std::forward<A>(a) - 1};
                    {*rit} -> std::same_as<decltype((*it))>;
                }
            );

        template <typename C, typename A>
        concept offset_linear =
            (!meta::tuple_like<C> || meta::tuple_size<C> > 0) &&
            requires(C c, A a, meta::begin_type<C> it) {
                {meta::begin(std::forward<C>(c))};
                {a > 0} -> meta::truthy;
                {++it};
                {--a};
                {*it};
            } && (
                !meta::reverse_iterable<C> ||
                meta::unsigned_integer<A> ||
                requires(C c, A a, meta::begin_type<C> it, meta::rbegin_type<C> rit) {
                    {meta::rbegin(std::forward<C>(c))};
                    {a < 0} -> meta::truthy;
                    {++rit};
                    {++a};
                    {*rit} -> std::same_as<decltype((*it))>;
                }
            );

        template <meta::iterable C, meta::integer A>
        constexpr decltype(auto) offset(C&& c, A&& a)
            noexcept (requires{{meta::begin(std::forward<C>(c))[std::forward<A>(a)]} noexcept;} && (
                !meta::reverse_iterable<C> ||
                meta::unsigned_integer<A> ||
                requires{
                    {a < 0} noexcept -> meta::nothrow::truthy;
                    {meta::rbegin(std::forward<C>(c))[-std::forward<A>(a) - 1]} noexcept;
                }
            ))
            requires (offset_subscript<C, A>)
        {
            if constexpr (meta::reverse_iterable<C> && !meta::unsigned_integer<A>) {
                if (a < 0) {
                    return (meta::rbegin(std::forward<C>(c))[-std::forward<A>(a) - 1]);
                }
            }
            return (meta::begin(std::forward<C>(c))[std::forward<A>(a)]);
        }

        template <meta::iterable C, meta::integer A>
        constexpr decltype(auto) offset(C&& c, A&& a)
            noexcept (requires(meta::begin_type<C> it) {
                {meta::begin(std::forward<C>(c))} noexcept;
                {it += std::forward<A>(a)} noexcept;
                {*it} noexcept;
            } && (
                !meta::reverse_iterable<C> ||
                meta::unsigned_integer<A> ||
                requires(meta::rbegin_type<C> it) {
                    {a < 0} noexcept -> meta::nothrow::truthy;
                    {meta::rbegin(std::forward<C>(c))} noexcept;
                    {it += -std::forward<A>(a) - 1} noexcept;
                    {*it} noexcept;
                }
            ))
            requires (
                !offset_subscript<C, A> &&
                offset_random_access<C, A>
            )
        {
            if constexpr (meta::reverse_iterable<C> && !meta::unsigned_integer<A>) {
                if (a < 0) {
                    auto it = meta::rbegin(std::forward<C>(c));
                    it += -a - 1;
                    return (*it);
                }
            }
            auto it = meta::begin(std::forward<C>(c));
            it += std::forward<A>(a);
            return (*it);
        }

        template <meta::iterable C, meta::integer A>
        constexpr decltype(auto) offset(C&& c, A&& a)
            noexcept (requires(meta::begin_type<C> it) {
                {meta::begin(std::forward<C>(c))} noexcept;
                {a > 0} noexcept -> meta::nothrow::truthy;
                {++it} noexcept;
                {--a} noexcept;
                {*it} noexcept;
            } && (
                !meta::reverse_iterable<C> ||
                meta::unsigned_integer<A> ||
                requires(meta::rbegin_type<C> it) {
                    {a < 0} noexcept -> meta::nothrow::truthy;
                    {++it} noexcept;
                    {++a} noexcept;
                    {*it} noexcept;
                }
            ))
            requires (
                !offset_subscript<C, A> &&
                !offset_random_access<C, A> &&
                offset_linear<C, A>
            )
        {
            if constexpr (meta::reverse_iterable<C> && !meta::unsigned_integer<A>) {
                if (a < 0) {
                    ++a;  // Adjust for zero-based indexing
                    auto it = meta::rbegin(std::forward<C>(c));
                    while (a < 0) {
                        ++it;
                        ++a;
                    }
                    return (*it);
                }
            }
            auto it = meta::begin(std::forward<C>(c));
            while (a > 0) {
                ++it;
                --a;
            }
            return (*it);
        }

    }

    namespace range_convert {

        template <typename Self, typename to>
        concept direct = requires(Self self) {
            {*std::forward<Self>(self)} -> meta::convertible_to<to>;
        };

        template <typename to, typename Self>
        [[nodiscard]] constexpr to convert(Self&& self)
            noexcept (requires{
                {*std::forward<Self>(self)} noexcept -> meta::nothrow::convertible_to<to>;
            })
            requires (direct<Self, to>)
        {
            return *std::forward<Self>(self);
        }

        template <typename Self, typename to>
        concept construct = requires(Self self) {
            {to(std::from_range, std::forward<Self>(self))};
        };

        template <typename to, typename Self>
        [[nodiscard]] constexpr to convert(Self&& self)
            noexcept (requires{
                {to(std::from_range, std::forward<Self>(self))} noexcept;
            })
            requires (
                !direct<Self, to> &&
                construct<Self, to>
            )
        {
            return to(std::from_range, std::forward<Self>(self));
        }

        template <typename Self, typename to>
        concept traverse = requires(Self self) {{to(
            std::forward<Self>(self).begin(),
            std::forward<Self>(self).end()
        )};};

        template <typename to, typename Self>
        [[nodiscard]] constexpr to convert(Self&& self)
            noexcept (requires{{to(
                std::forward<Self>(self).begin(),
                std::forward<Self>(self).end()
            )} noexcept;})
            requires (
                !direct<Self, to> &&
                !construct<Self, to> &&
                traverse<Self, to>
            )
        {
            return to(std::forward<Self>(self).begin(), std::forward<Self>(self).end());
        }

        template <typename to, typename R, size_t... Is>
        constexpr to unpack_tuple_impl(R&& r, std::index_sequence<Is...>)
            noexcept (requires{{to{std::forward<R>(r).template get<Is>()...}} noexcept;})
            requires (requires{{to{std::forward<R>(r).template get<Is>()...}};})
        {
            return to{std::forward<R>(r).template get<Is>()...};
        }

        template <typename Self, typename to>
        concept unpack_tuple =
            meta::tuple_like<Self> &&
            meta::tuple_like<to> &&
            meta::tuple_size<to> == meta::tuple_size<Self> &&
            requires(Self self) {{unpack_tuple_impl<to>(
                std::forward<Self>(self),
                std::make_index_sequence<meta::tuple_size<to>>{}
            )};};

        template <typename to, typename Self>
        [[nodiscard]] constexpr to convert(Self&& self)
            noexcept (requires{{unpack_tuple_impl<to>(
                std::forward<Self>(self),
                std::make_index_sequence<meta::tuple_size<to>>{}
            )} noexcept;})
            requires (
                !direct<Self, to> &&
                !construct<Self, to> &&
                !traverse<Self, to> &&
                unpack_tuple<Self, to>
            )
        {
            return unpack_tuple_impl<to>(
                std::forward<Self>(self),
                std::make_index_sequence<meta::tuple_size<to>>{}
            );
        }

        /// TODO: backfill these errors when I've defined `repr()` and integer <->
        /// string conversions.

        template <size_t Expected>
        constexpr TypeError wrong_size(size_t actual) noexcept;

        template <size_t I, size_t N>
        constexpr TypeError too_small() noexcept;

        template <size_t N>
        constexpr TypeError too_big() noexcept;

        template <meta::iterator Iter>
        constexpr decltype(auto) _unpack_iter_impl(Iter& it)
            noexcept (meta::nothrow::iterator<Iter>)
        {
            struct G {
                Iter& it;
                constexpr ~G() noexcept (meta::nothrow::iterator<Iter>) { ++it; }
            } guard {it};
            return (*it);  // destructor runs after return
        }

        template <size_t I, size_t N, meta::iterator Iter, meta::sentinel_for<Iter> End>
        constexpr decltype(auto) _unpack_iter_impl(Iter& it, End& end)
            noexcept (!DEBUG && requires{{_unpack_iter_impl(it)} noexcept;})
        {
            if constexpr (DEBUG) {
                if (it == end) {
                    throw too_small<I, N>();
                }
            }
            return (_unpack_iter_impl(it));
        }

        template <meta::tuple_like to, meta::range Self, size_t... Is>
            requires (!meta::tuple_like<Self> && sizeof...(Is) == meta::tuple_size<to>)
        constexpr to unpack_iter_impl(Self&& self, std::index_sequence<Is...>)
            noexcept (!DEBUG && requires(meta::begin_type<Self> it) {
                {self.size() != sizeof...(Is)} noexcept -> meta::nothrow::truthy;
                {std::forward<Self>(self).begin()} noexcept;
                {to{(void(Is), _unpack_iter_impl(it))...}} noexcept;
            })
            requires (requires(meta::begin_type<Self> it){
                {self.size() != sizeof...(Is)} -> meta::truthy;
                {std::forward<Self>(self).begin()};
                {to{(void(Is), _unpack_iter_impl(it))...}};
            })
        {
            if constexpr (DEBUG) {
                if (size_t size = self.size(); size != sizeof...(Is)) {
                    throw wrong_size<sizeof...(Is)>(size);
                }
            }
            auto it = std::forward<Self>(self).begin();
            return to{(void(Is), _unpack_iter_impl(it))...};
        }

        template <meta::tuple_like to, meta::range Self, size_t... Is>
            requires (!meta::tuple_like<Self> && sizeof...(Is) == meta::tuple_size<to>)
        constexpr to unpack_iter_impl(Self&& self, std::index_sequence<Is...>)
            noexcept (!DEBUG && requires(meta::begin_type<Self> it, meta::end_type<Self> end) {
                {std::forward<Self>(self).begin()} noexcept;
                {std::forward<Self>(self).end()} noexcept;
                {to{_unpack_iter_impl<Is, sizeof...(Is)>(it, end)...}} noexcept;
            })
            requires (
                !requires{{self.size() != sizeof...(Is)} -> meta::truthy;} && 
                requires(meta::begin_type<Self> it, meta::end_type<Self> end) {
                    {std::forward<Self>(self).begin()};
                    {std::forward<Self>(self).end()};
                    {to{_unpack_iter_impl<Is, sizeof...(Is)>(it, end)...}};
                }
            )
        {
            auto it = std::forward<Self>(self).begin();
            auto end = std::forward<Self>(self).end();
            to result {_unpack_iter_impl<Is, sizeof...(Is)>(it, end)...};
            if constexpr (DEBUG) {
                if (!bool(it == end)) {
                    throw too_big<sizeof...(Is)>();
                }
            }
            return result;
        }

        template <typename Self, typename to>
        concept unpack_iter =
            !meta::tuple_like<Self> &&
            meta::tuple_like<to> &&
            requires(Self self) {{unpack_iter_impl<to>(
                std::forward<Self>(self),
                std::make_index_sequence<meta::tuple_size<to>>{}
            )};};

        template <typename to, typename Self>
        [[nodiscard]] constexpr to convert(Self&& self)
            noexcept (requires{{unpack_iter_impl<to>(
                std::forward<Self>(self),
                std::make_index_sequence<meta::tuple_size<to>>{}
            )} noexcept;})
            requires (
                !direct<Self, to> &&
                !construct<Self, to> &&
                !traverse<Self, to> &&
                !unpack_tuple<Self, to> &&
                unpack_iter<Self, to>
            )
        {
            return unpack_iter_impl<to>(
                std::forward<Self>(self),
                std::make_index_sequence<meta::tuple_size<to>>{}
            );
        }

        template <meta::not_reference to> requires (meta::iterable<meta::as_lvalue<to>>)
        struct recur {
            using type = meta::remove_reference<meta::yield_type<meta::as_lvalue<to>>>;
            template <typename T>
            static constexpr type operator()(T&& t)
                noexcept (requires{{convert<type>(iter::range(std::forward<T>(t)))} noexcept;})
                requires (requires{{convert<type>(iter::range(std::forward<T>(t)))};})
            {
                return convert<type>(iter::range(std::forward<T>(t)));
            }
        };

        template <typename Self, typename to>
        concept recursive = meta::iterable<meta::as_lvalue<to>> && requires(Self self) {
            {convert<to>(iter::range(
                transform_iterator{std::forward<Self>(self).begin(), recur<to>{}},
                transform_iterator{std::forward<Self>(self).end(), recur<to>{}}
            ))};
        };

        template <typename to, typename Self>
        [[nodiscard]] constexpr to convert(Self&& self)
            requires (
                !direct<Self, to> &&
                !construct<Self, to> &&
                !traverse<Self, to> &&
                !unpack_tuple<Self, to> &&
                !unpack_iter<Self, to> &&
                recursive<Self, to>
            )
        {
            return convert<to>(iter::range(
                transform_iterator{std::forward<Self>(self).begin(), recur<to>{}},
                transform_iterator{std::forward<Self>(self).end(), recur<to>{}}
            ));
        }

    }

    namespace range_assign {

        template <typename LHS, typename RHS>
        concept direct = requires(LHS lhs, RHS rhs) {
            {*lhs = meta::from_range(std::forward<RHS>(rhs))};
        };

        template <typename LHS, typename RHS>
        constexpr void assign(LHS& lhs, RHS&& rhs)
            noexcept (requires{
                {*lhs = meta::from_range(std::forward<RHS>(rhs))} noexcept;
            })
            requires (direct<LHS, RHS>)
        {
            *lhs = meta::from_range(std::forward<RHS>(rhs));
        }

        template <typename LHS, typename RHS>
        concept scalar =
            (!meta::range<RHS> || meta::scalar<RHS>) &&
            requires(LHS lhs, meta::as_const_ref<RHS> rhs, decltype(lhs.begin()) it) {
                {*it = meta::from_range(rhs)};
            };

        template <typename LHS, typename RHS>
        constexpr void assign(LHS& lhs, const RHS& rhs)
            noexcept (requires(decltype(lhs.begin()) it, decltype(lhs.end()) end) {
                {lhs.begin()} noexcept;
                {lhs.end()} noexcept;
                {it != end} noexcept -> meta::nothrow::truthy;
                {*it = meta::from_range(rhs)} noexcept;
                {++it} noexcept;
            })
            requires (
                !direct<LHS, RHS> &&
                scalar<LHS, RHS>
            )
        {
            auto it = lhs.begin();
            auto end = lhs.end();
            while (it != end) {
                *it = meta::from_range(rhs);
                ++it;
            }
        }

        /// TODO: backfill these errors when I've defined `repr()` and integer <->
        /// string conversions.

        inline constexpr TypeError wrong_size(size_t expected, size_t actual) noexcept;

        inline constexpr TypeError too_small(size_t n) noexcept;

        inline constexpr TypeError too_big(size_t n) noexcept;

        template <typename LHS, typename RHS>
        concept iter_from_iter = meta::range<RHS> && !meta::scalar<RHS> && (
            !meta::tuple_like<LHS> ||
            !meta::tuple_like<RHS> ||
            meta::tuple_size<LHS> == meta::tuple_size<RHS>
        ) && requires(meta::begin_type<LHS> l_it, meta::begin_type<RHS> r_it) {
            {iter::range{*l_it} = iter::range{*r_it}};
        };

        template <typename LHS, typename RHS>
        constexpr void assign(LHS& lhs, RHS&& rhs)
            noexcept ((!DEBUG || (meta::tuple_like<LHS> && meta::tuple_like<RHS>)) && requires(
                decltype(lhs.begin()) l_it,
                decltype(lhs.end()) l_end,
                decltype(rhs.begin()) r_it,
                decltype(rhs.end()) r_end
            ) {
                {lhs.begin()} noexcept;
                {lhs.end()} noexcept;
                {rhs.begin()} noexcept;
                {rhs.end()} noexcept;
                {l_it != l_end} noexcept -> meta::nothrow::truthy;
                {r_it != r_end} noexcept -> meta::nothrow::truthy;
                {iter::range{*l_it} = iter::range{*r_it}} noexcept;
                {++l_it} noexcept;
                {++r_it} noexcept;
            })
            requires (
                !direct<LHS, RHS> &&
                !scalar<LHS, RHS> &&
                iter_from_iter<LHS, RHS>
            )
        {
            static constexpr bool statically_sized = meta::tuple_like<LHS> && meta::tuple_like<RHS>;
            static constexpr bool sized = requires{{lhs.size()};} && requires{{rhs.size()};};
            if constexpr (DEBUG && !statically_sized && sized) {
                if (lhs.size() != rhs.size()) {
                    throw wrong_size(lhs.size(), rhs.size());
                }
            }
            auto l_it = lhs.begin();
            auto l_end = lhs.end();
            auto r_it = rhs.begin();
            auto r_end = rhs.end();
            if constexpr (DEBUG && !statically_sized && !sized) {
                size_t n = 0;
                while (l_it != l_end && r_it != r_end) {
                    iter::range{*l_it} = iter::range{*r_it};
                    ++l_it;
                    ++r_it;
                    ++n;
                }
                if (l_it != l_end) {
                    throw impl::range_assign::too_small(n);
                }
                if (r_it != r_end) {
                    throw impl::range_assign::too_big(n);
                }
            } else {
                while (l_it != l_end && r_it != r_end) {
                    iter::range{*l_it} = iter::range{*r_it};
                    ++l_it;
                    ++r_it;
                }
            }
        }

        template <typename LHS, typename RHS, size_t... Is>
        constexpr void tuple_from_tuple_impl(LHS& lhs, RHS&& rhs, std::index_sequence<Is...>)
            noexcept (requires{{
                ((iter::range{lhs.template get<Is>()} = iter::range{
                    std::forward<RHS>(rhs).template get<Is>()
                }), ...)
            } noexcept;})
            requires (requires{{
                ((iter::range{lhs.template get<Is>()} = iter::range{
                    std::forward<RHS>(rhs).template get<Is>()
                }), ...)
            };})
        {
            ((iter::range{lhs.template get<Is>()} = iter::range{
                std::forward<RHS>(rhs).template get<Is>()
            }), ...);
        }

        template <typename LHS, typename RHS>
        concept tuple_from_tuple =
            meta::range<RHS> &&
            !meta::scalar<RHS> && 
            meta::tuple_like<LHS> &&
            meta::tuple_like<RHS> &&
            meta::tuple_size<LHS> == meta::tuple_size<RHS> &&
            requires(LHS lhs, RHS rhs) {{tuple_from_tuple_impl(
                lhs,
                std::forward<RHS>(rhs),
                std::make_index_sequence<meta::tuple_size<LHS>>{}
            )};};

        template <typename LHS, typename RHS>
        constexpr void assign(LHS& lhs, RHS&& rhs)
            noexcept (requires{{tuple_from_tuple_impl(
                lhs,
                std::forward<RHS>(rhs),
                std::make_index_sequence<meta::tuple_size<LHS>>{}
            )} noexcept;})
            requires (
                !direct<LHS, RHS> &&
                !scalar<LHS, RHS> &&
                !iter_from_iter<LHS, RHS> &&
                tuple_from_tuple<LHS, RHS>
            )
        {
            tuple_from_tuple_impl(
                lhs,
                std::forward<RHS>(rhs),
                std::make_index_sequence<meta::tuple_size<LHS>>{}
            );
        }

        template <size_t I, typename RHS, typename Iter>
        constexpr void _iter_from_tuple_impl(RHS&& rhs, Iter& it)
            noexcept (requires{
                {iter::range{*it} = iter::range{std::forward<RHS>(rhs).template get<I>()}} noexcept;
                {++it} noexcept;
            })
            requires (requires{
                {iter::range{*it} = iter::range{std::forward<RHS>(rhs).template get<I>()}};
                {++it};
            })
        {
            iter::range{*it} = iter::range{std::forward<RHS>(rhs).template get<I>()};
            ++it;
        }

        template <size_t I, typename RHS, typename Iter, typename End>
        constexpr void _iter_from_tuple_impl(RHS&& rhs, Iter& it, End& end)
            noexcept (!DEBUG && requires{
                {_iter_from_tuple_impl<I>(std::forward<RHS>(rhs), it)} noexcept;
            })
            requires (requires{
                {it == end} -> meta::truthy;
                {_iter_from_tuple_impl<I>(std::forward<RHS>(rhs), it)};
            })
        {
            if constexpr (DEBUG) {
                if (it == end) {
                    throw too_big(I);
                }
            }
            _iter_from_tuple_impl<I>(std::forward<RHS>(rhs), it);
        }

        template <typename LHS, typename RHS, size_t... Is>
        constexpr void iter_from_tuple_impl(LHS& lhs, RHS&& rhs, std::index_sequence<Is...>)
            noexcept (!DEBUG && requires(decltype(lhs.begin()) it) {
                {lhs.begin()} noexcept;
                {(_iter_from_tuple_impl<Is>(std::forward<RHS>(rhs), it), ...) } noexcept;
            })
            requires ((!DEBUG || requires{
                {lhs.size() != sizeof...(Is)} -> meta::truthy;
            }) && requires(decltype(lhs.begin()) it) {
                {lhs.begin()};
                {(_iter_from_tuple_impl<Is>(std::forward<RHS>(rhs), it), ...)};
            })
        {
            if constexpr (DEBUG) {
                if (lhs.size() != sizeof...(Is)) {
                    throw wrong_size(lhs.size(), sizeof...(Is));
                }
            }
            auto it = lhs.begin();
            (_iter_from_tuple_impl<Is>(std::forward<RHS>(rhs), it), ...);
        }

        template <typename LHS, typename RHS, size_t... Is>
        constexpr void iter_from_tuple_impl(LHS& lhs, RHS&& rhs, std::index_sequence<Is...>)
            noexcept (!DEBUG && requires(decltype(lhs.begin()) it, decltype(lhs.end()) end) {
                {lhs.begin()} noexcept;
                {lhs.end()} noexcept;
                {(_iter_from_tuple_impl<Is>(std::forward<RHS>(rhs), it, end), ...)} noexcept;
            })
            requires (
                !requires{{lhs.size()};} &&
                requires(decltype(lhs.begin()) it, decltype(lhs.end()) end) {
                    {lhs.begin()};
                    {lhs.end()};
                    {(_iter_from_tuple_impl<Is>(std::forward<RHS>(rhs), it, end), ...)};
                }
            )
        {
            auto it = lhs.begin();
            auto end = lhs.end();
            (_iter_from_tuple_impl<Is>(std::forward<RHS>(rhs), it, end), ...);
            if constexpr (DEBUG) {
                if (!bool(it == end)) {
                    throw too_small(sizeof...(Is));
                }
            }
        }

        template <typename LHS, typename RHS>
        concept iter_from_tuple =
            meta::range<RHS> &&
            !meta::scalar<RHS> &&
            !meta::tuple_like<LHS> &&
            meta::tuple_like<RHS> &&
            requires(LHS lhs, RHS rhs) {{iter_from_tuple_impl(
                lhs,
                std::forward<RHS>(rhs),
                std::make_index_sequence<meta::tuple_size<RHS>>{}
            )};};

        template <typename LHS, typename RHS>
        constexpr void assign(LHS& lhs, RHS&& rhs)
            noexcept (requires{{iter_from_tuple_impl(
                lhs,
                std::forward<RHS>(rhs),
                std::make_index_sequence<meta::tuple_size<RHS>>{}
            )} noexcept;})
            requires (
                !direct<LHS, RHS> &&
                !scalar<LHS, RHS> &&
                !iter_from_iter<LHS, RHS> &&
                !tuple_from_tuple<LHS, RHS> &&
                iter_from_tuple<LHS, RHS>
            )
        {
            iter_from_tuple_impl(
                lhs,
                std::forward<RHS>(rhs),
                std::make_index_sequence<meta::tuple_size<RHS>>{}
            );
        }

        template <size_t I, typename LHS, typename Iter>
        constexpr void _tuple_from_iter_impl(LHS& lhs, Iter& it)
            noexcept (requires{
                {iter::range{lhs.template get<I>()} = iter::range{*it}} noexcept;
                {++it} noexcept;
            })
            requires (requires{
                {iter::range{lhs.template get<I>()} = iter::range{*it}};
                {++it};
            })
        {
            iter::range{lhs.template get<I>()} = iter::range{*it};
            ++it;
        }

        template <size_t I, typename LHS, typename Iter, typename End>
        constexpr void _tuple_from_iter_impl(LHS& lhs, Iter& it, End& end)
            noexcept (!DEBUG && requires{
                {_tuple_from_iter_impl<I>(lhs, it)} noexcept;
            })
            requires (requires{
                {it == end} -> meta::truthy;
                {_tuple_from_iter_impl<I>(lhs, it)};
            })
        {
            if constexpr (DEBUG) {
                if (it == end) {
                    throw too_small(I);
                }
            }
            _tuple_from_iter_impl<I>(lhs, it);
        }

        template <typename LHS, typename RHS, size_t... Is>
        constexpr void tuple_from_iter_impl(LHS& lhs, RHS& rhs, std::index_sequence<Is...>)
            noexcept (!DEBUG && requires(decltype(rhs.begin()) it) {
                {rhs.begin()} noexcept;
                {(_tuple_from_iter_impl<Is>(lhs, it), ...)} noexcept;
            })
            requires ((!DEBUG || requires{
                {rhs.size() != sizeof...(Is)} -> meta::truthy;
            }) && requires(decltype(rhs.begin()) it) {
                {rhs.begin()};
                {(_tuple_from_iter_impl<Is>(lhs, it), ...)};
            })
        {
            if constexpr (DEBUG) {
                if (sizeof...(Is) != rhs.size()) {
                    throw wrong_size(sizeof...(Is), rhs.size());
                }
            }
            auto it = rhs.begin();
            (_tuple_from_iter_impl<Is>(lhs, it), ...);
        }

        template <typename LHS, typename RHS, size_t... Is>
        constexpr void tuple_from_iter_impl(LHS& lhs, RHS& rhs, std::index_sequence<Is...>)
            noexcept (!DEBUG && requires(decltype(rhs.begin()) it, decltype(rhs.end()) end) {
                {rhs.begin()} noexcept;
                {rhs.end()} noexcept;
                {(_tuple_from_iter_impl<Is>(lhs, it, end), ...)} noexcept;
            })
            requires (
                !requires{{rhs.size()};} &&
                requires(decltype(rhs.begin()) it, decltype(rhs.end()) end) {
                    {rhs.begin()};
                    {rhs.end()};
                    {(_tuple_from_iter_impl<Is>(lhs, it, end), ...)};
                }
            )
        {
            auto it = rhs.begin();
            auto end = rhs.end();
            (_tuple_from_iter_impl<Is>(lhs, it, end), ...);
            if constexpr (DEBUG) {
                if (!bool(it == end)) {
                    throw too_big(sizeof...(Is));
                }
            }
        }

        template <typename LHS, typename RHS>
        concept tuple_from_iter =
            meta::range<RHS> &&
            !meta::scalar<RHS> &&
            meta::tuple_like<LHS> &&
            !meta::tuple_like<RHS> &&
            requires(LHS lhs, RHS rhs) {{tuple_from_iter_impl(
                lhs,
                rhs,
                std::make_index_sequence<meta::tuple_size<LHS>>{}
            )};};

        template <typename LHS, typename RHS>
        constexpr void assign(LHS& lhs, RHS&& rhs)
            noexcept (requires{{tuple_from_iter_impl(
                lhs,
                rhs,
                std::make_index_sequence<meta::tuple_size<LHS>>{}
            )} noexcept;})
            requires (
                !direct<LHS, RHS> &&
                !scalar<LHS, RHS> &&
                !iter_from_iter<LHS, RHS> &&
                !tuple_from_tuple<LHS, RHS> &&
                !iter_from_tuple<LHS, RHS> &&
                tuple_from_iter<LHS, RHS>
            )
        {
            tuple_from_iter_impl(lhs, rhs, std::make_index_sequence<meta::tuple_size<LHS>>{});
        }

    }

    /* A wrapper around a generic container that reverses the semantics of `front()`,
    `back()`, and the forward and reverse iteration methods, as well as mapping integer
    indices to `-i - 1` before triggering Python-style wraparound.  `T` may be one of
    the internal wrappers listed above, in order to simplify reversals of scalars,
    tuples, etc. */
    template <meta::not_rvalue T>
        requires (meta::range<T> && meta::reverse_iterable<meta::as_lvalue<T>>)
    struct reverse {
    private:
        [[no_unique_address]] impl::ref<T> m_value;

    public:
        [[nodiscard]] constexpr reverse(meta::forward<T> r)
            noexcept (requires{{impl::ref<T>{T{std::forward<T>(r)}}} noexcept;})
            requires (requires{{impl::ref<T>{T{std::forward<T>(r)}}};})
        :
            m_value{T{std::forward<T>(r)}}
        {}

        constexpr void swap(reverse& other)
            noexcept (requires{{m_value.swap(other.m_value)} noexcept;})
            requires (requires{{m_value.swap(other.m_value)};})
        {
            m_value.swap(other.m_value);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{**std::forward<Self>(self).m_value} noexcept;})
            requires (requires{{**std::forward<Self>(self).m_value};})
        {
            return (**std::forward<Self>(self).m_value);
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{**m_value} noexcept;})
            requires (requires{{**m_value};})
        {
            return std::addressof(**m_value);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{**m_value} noexcept;})
            requires (requires{{**m_value};})
        {
            return std::addressof(**m_value);
        }

        [[nodiscard]] constexpr auto shape() const
            noexcept (requires{{m_value->shape()} noexcept;})
            requires (requires{{m_value->shape()};})
        {
            return m_value->shape();
        }

        [[nodiscard]] constexpr decltype(auto) size() const
            noexcept (requires{{m_value->size()} noexcept;})
            requires (requires{{m_value->size()};})
        {
            return (m_value->size());
        }

        [[nodiscard]] constexpr decltype(auto) ssize() const
            noexcept (requires{{m_value->ssize()} noexcept;})
            requires (requires{{m_value->ssize()};})
        {
            return (m_value->ssize());
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{m_value->empty()} noexcept;})
            requires (requires{{m_value->empty()};})
        {
            return m_value->empty();
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self)
            noexcept (requires{{(*std::forward<Self>(self).m_value).back()} noexcept;})
            requires (requires{{(*std::forward<Self>(self).m_value).back()};})
        {
            return ((*std::forward<Self>(self).m_value).back());
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) back(this Self&& self)
            noexcept (requires{{(*std::forward<Self>(self).m_value).front()} noexcept;})
            requires (requires{{(*std::forward<Self>(self).m_value).front()};})
        {
            return ((*std::forward<Self>(self).m_value).front());
        }

        template <ssize_t I, typename Self>
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{(*std::forward<Self>(self).m_value).template get<
                meta::to_unsigned(impl::normalize_index<meta::tuple_size<T>, -I - 1>())
            >()} noexcept;})
            requires (requires{{(*std::forward<Self>(self).m_value).template get<
                meta::to_unsigned(impl::normalize_index<meta::tuple_size<T>, -I - 1>())
            >()};})
        {
            return ((*std::forward<Self>(self).m_value).template get<
                meta::to_unsigned(impl::normalize_index<meta::tuple_size<T>, -I - 1>())
            >());
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, ssize_t i)
            noexcept (requires{{(*std::forward<Self>(self).m_value)[
                meta::to_unsigned(impl::normalize_index(self.ssize(), -i - 1))]
            } noexcept;})
            requires (requires{{(*std::forward<Self>(self).m_value)[
                meta::to_unsigned(impl::normalize_index(self.ssize(), -i - 1))]
            };})
        {
            return ((*std::forward<Self>(self).m_value)[
                meta::to_unsigned(impl::normalize_index(self.ssize(), -i - 1))
            ]);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto begin(this Self&& self)
            noexcept (requires{{(*std::forward<Self>(self).m_value).begin()} noexcept;})
            requires (requires{{(*std::forward<Self>(self).m_value).begin()};})
        {
            return (*std::forward<Self>(self).m_value).begin();
        }

        template <typename Self>
        [[nodiscard]] constexpr auto end(this Self&& self)
            noexcept (requires{{(*std::forward<Self>(self).m_value).rend()} noexcept;})
            requires (requires{{(*std::forward<Self>(self).m_value).rend()};})
        {
            return (*std::forward<Self>(self).m_value).rend();
        }

        template <typename Self>
        [[nodiscard]] constexpr auto rbegin(this Self&& self)
            noexcept (requires{{(*std::forward<Self>(self).m_value).begin()} noexcept;})
            requires (requires{{(*std::forward<Self>(self).m_value).begin()};})
        {
            return ((*std::forward<Self>(self).m_value).begin());
        }

        template <typename Self>
        [[nodiscard]] constexpr auto rend(this Self&& self)
            noexcept (requires{{(*std::forward<Self>(self).m_value).end()} noexcept;})
            requires (requires{{(*std::forward<Self>(self).m_value).end()};})
        {
            return ((*std::forward<Self>(self).m_value).end());
        }
    };

    /* Non-range arguments are forwarded as lvalues, while ranges are yielded from. */
    template <typename T>
    struct _zip_arg { using type = meta::as_lvalue<T>; };
    template <meta::range T>
    struct _zip_arg<T> { using type = meta::yield_type<T>; };
    template <typename T>
    using zip_arg = _zip_arg<meta::remove_rvalue<T>>::type;

    /* The yield type must be determined using an index sequence over the parent
    container, which ensures the function is callable with the broadcasted
    arguments. */
    template <typename, typename>
    struct zip_yield { static constexpr bool enable = false; };
    template <typename Self, size_t... Is>
        requires (meta::callable<
            decltype((std::declval<Self>().func())),
            zip_arg<decltype((std::declval<Self>().template arg<Is>()))>...
        >)
    struct zip_yield<Self, std::index_sequence<Is...>> {
        static constexpr bool enable = true;
        using type = meta::remove_rvalue<decltype(std::declval<Self>().func()(std::declval<
            zip_arg<decltype((std::declval<Self>().template arg<Is>()))>
        >()...))>;
    };

    /* Zip iterators store individual iterators over just the range operands, in order
    to save space.  Non-range values are referenced indirectly via a pointer to the
    parent zipped range.  Upon dereference, an index sequence over all operands is
    translated into a corresponding index into the iterator tuple or the parent range
    depending on its status as a range.  If it is not, then this index will evaluate
    to the size of the range-only index sequence. */
    template <size_t I, typename>
    constexpr size_t zip_index = 0;
    template <size_t I, size_t J, size_t... Js> requires (I != J)
    constexpr size_t zip_index<I, std::index_sequence<J, Js...>> =
        zip_index<I, std::index_sequence<Js...>> + 1;

    /* Zip iterators work by storing a pointer to the zipped range and a tuple of
    backing iterators for each range input.  The transformation function and
    broadcasted scalars will be accessed indirectly via the pointer, and the iterator
    interface is delegated to the backing iterators via fold expressions.
    Dereferencing the iterator equates to calling the transformation function with the
    scalar values or dereference types of the iterator tuple, in the same order as the
    original argument list. */
    template <typename Self, meta::not_rvalue... Iters>
        requires (zip_yield<Self, typename meta::unqualify<Self>::args>::enable)
    struct zip_iterator {
    private:
        using args = meta::unqualify<Self>::args;
        using ranges = meta::unqualify<Self>::ranges;

    public:
        using iterator_category = meta::common_type<meta::iterator_category<Iters>...>;
        using difference_type = meta::common_type<meta::iterator_difference<Iters>...>;
        using reference = zip_yield<Self, typename meta::unqualify<Self>::args>::type;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<reference>;

        meta::as_pointer<Self> ptr = nullptr;
        impl::basic_tuple<Iters...> iters {};

        [[nodiscard]] constexpr zip_iterator() = default;
        [[nodiscard]] constexpr zip_iterator(
            meta::forward<Self> self,
            meta::forward<Iters>... its
        )
            noexcept (requires{
                {impl::basic_tuple<Iters...>{std::forward<Iters>(its)...}} noexcept;
            })
            requires (requires{
                {impl::basic_tuple<Iters...>{std::forward<Iters>(its)...}};
            })
        :
            ptr{std::addressof(self)},
            iters{std::forward<Iters>(its)...}
        {}

    private:
        template <size_t A>
        constexpr decltype(auto) _deref() const
            noexcept (zip_index<A, ranges> == ranges::size() || requires{
                {*iters.template get<zip_index<A, ranges>>()} noexcept;
            })
            requires (zip_index<A, ranges> == ranges::size() || requires{
                {*iters.template get<zip_index<A, ranges>>()};
            })
        {
            if constexpr (zip_index<A, ranges> == ranges::size()) {
                return (ptr->template arg<A>());
            } else {
                return (*iters.template get<zip_index<A, ranges>>());
            }
        }

        template <size_t... As>
        constexpr decltype(auto) deref(std::index_sequence<As...>) const
            noexcept (requires{{ptr->func()(_deref<As>()...)} noexcept;})
            requires (requires{{ptr->func()(_deref<As>()...)};})
        {
            return (ptr->func()(_deref<As>()...));
        }

        template <size_t A>
        constexpr decltype(auto) _subscript(difference_type i) const
            noexcept (zip_index<A, ranges> == ranges::size() || requires{
                {iters.template get<zip_index<A, ranges>>()[i]} noexcept;
            })
            requires (zip_index<A, ranges> == ranges::size() || requires{
                {iters.template get<zip_index<A, ranges>>()[i]};
            })
        {
            if constexpr (zip_index<A, ranges> == ranges::size()) {
                return (ptr->template arg<A>());
            } else {
                return (iters.template get<zip_index<A, ranges>>()[i]);
            }
        }

        template <size_t... As> requires (sizeof...(As) == args::size())
        constexpr decltype(auto) subscript(std::index_sequence<As...>, difference_type i) const
            noexcept (requires{{ptr->func()(_subscript<As>(i)...)} noexcept;})
            requires (requires{{ptr->func()(_subscript<As>(i)...)};})
        {
            return (ptr->func()(_subscript<As>(i)...));
        }

        template <size_t... Rs>
        constexpr void increment(std::index_sequence<Rs...>)
            noexcept (requires{{((++iters.template get<Rs>()), ...)} noexcept;})
            requires (requires{{((++iters.template get<Rs>()), ...)};})
        {
            ((++iters.template get<Rs>()), ...);
        }

        template <size_t... Rs>
        constexpr void advance(std::index_sequence<Rs...>, difference_type i)
            noexcept (requires{{((iters.template get<Rs>() += i), ...)} noexcept;})
            requires (requires{{((iters.template get<Rs>() += i), ...)};})
        {
            ((iters.template get<Rs>() += i), ...);
        }

        template <size_t... Rs>
        constexpr void decrement(std::index_sequence<Rs...>)
            noexcept (requires{{((--iters.template get<Rs>()), ...)} noexcept;})
            requires (requires{{((--iters.template get<Rs>()), ...)};})
        {
            ((--iters.template get<Rs>()), ...);
        }

        template <size_t... Rs>
        constexpr void retreat(std::index_sequence<Rs...>, difference_type i)
            noexcept (requires{{((iters.template get<Rs>() -= i), ...)} noexcept;})
            requires (requires{{((iters.template get<Rs>() -= i), ...)};})
        {
            ((iters.template get<Rs>() -= i), ...);
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
            } else if (d < min) {
                min = d;
            }
        }

        template <typename... Ts, size_t R, size_t... Rs>
        constexpr difference_type distance(
            std::index_sequence<R, Rs...>,
            const zip_iterator<Self, Ts...>& other
        ) const
            noexcept (requires(difference_type min) {
                {
                    iters.template get<R>() - other.iters.template get<R>()
                } noexcept -> meta::nothrow::convertible_to<difference_type>;
                {(_distance(
                    iters.template get<Rs>(),
                    other.iters.template get<Rs>(),
                    min
                ), ...)} noexcept;
            })
            requires (requires(difference_type min) {
                {
                    iters.template get<R>() - other.iters.template get<R>()
                } -> meta::convertible_to<difference_type>;
                {(_distance(
                    iters.template get<Rs>(),
                    other.iters.template get<Rs>(),
                    min
                ), ...)};
            })
        {
            difference_type min = iters.template get<R>() - other.iters.template get<R>();
            (_distance(
                iters.template get<Rs>(),
                other.iters.template get<Rs>(),
                min
            ), ...);
            return min;
        }

        template <typename... Ts, size_t... Rs>
        constexpr bool lt(std::index_sequence<Rs...>, const zip_iterator<Self, Ts...>& other) const
            noexcept (requires{{
                ((iters.template get<Rs>() < other.iters.template get<Rs>()) && ...)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                ((iters.template get<Rs>() < other.iters.template get<Rs>()) && ...)
            } -> meta::convertible_to<bool>;})
        {
            return ((iters.template get<Rs>() < other.iters.template get<Rs>()) && ...);
        }

        template <typename... Ts, size_t... Rs>
        constexpr bool le(std::index_sequence<Rs...>, const zip_iterator<Self, Ts...>& other) const
            noexcept (requires{{
                ((iters.template get<Rs>() <= other.iters.template get<Rs>()) && ...)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                ((iters.template get<Rs>() <= other.iters.template get<Rs>()) && ...)
            } -> meta::convertible_to<bool>;})
        {
            return ((iters.template get<Rs>() <= other.iters.template get<Rs>()) && ...);
        }

        template <typename... Ts, size_t... Rs>
        constexpr bool eq(std::index_sequence<Rs...>, const zip_iterator<Self, Ts...>& other) const
            noexcept (requires{{
                ((iters.template get<Rs>() == other.iters.template get<Rs>()) || ...)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                ((iters.template get<Rs>() == other.iters.template get<Rs>()) || ...)
            } -> meta::convertible_to<bool>;})
        {
            return ((iters.template get<Rs>() == other.iters.template get<Rs>()) || ...);
        }

        template <typename... Ts, size_t... Rs>
        constexpr bool ne(std::index_sequence<Rs...>, const zip_iterator<Self, Ts...>& other) const
            noexcept (requires{{
                ((iters.template get<Rs>() != other.iters.template get<Rs>()) && ...)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                ((iters.template get<Rs>() != other.iters.template get<Rs>()) && ...)
            } -> meta::convertible_to<bool>;})
        {
            return ((iters.template get<Rs>() != other.iters.template get<Rs>()) && ...);
        }

        template <typename... Ts, size_t... Rs>
        constexpr bool ge(std::index_sequence<Rs...>, const zip_iterator<Self, Ts...>& other) const
            noexcept (requires{{
                ((iters.template get<Rs>() >= other.iters.template get<Rs>()) && ...)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                ((iters.template get<Rs>() >= other.iters.template get<Rs>()) && ...)
            } -> meta::convertible_to<bool>;})
        {
            return ((iters.template get<Rs>() >= other.iters.template get<Rs>()) && ...);
        }

        template <typename... Ts, size_t... Rs>
        constexpr bool gt(std::index_sequence<Rs...>, const zip_iterator<Self, Ts...>& other) const
            noexcept (requires{{
                ((iters.template get<Rs>() > other.iters.template get<Rs>()) && ...)
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                ((iters.template get<Rs>() > other.iters.template get<Rs>()) && ...)
            } -> meta::convertible_to<bool>;})
        {
            return ((iters.template get<Rs>() > other.iters.template get<Rs>()) && ...);
        }

    public:
        [[nodiscard]] constexpr decltype(auto) operator*() const
            noexcept (requires{{deref(args{})} noexcept;})
            requires (requires{{deref(args{})};})
        {
            return (deref(args{}));
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow{**this}} noexcept;})
            requires (requires{{impl::arrow{**this}};})
        {
            return impl::arrow{**this};
        }

        [[nodiscard]] constexpr decltype(auto) operator[](difference_type i) const
            noexcept (requires{{subscript(args{}, i)} noexcept;})
            requires (requires{{subscript(args{}, i)};})
        {
            return (subscript(args{}, i));
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

        constexpr zip_iterator& operator+=(difference_type i)
            noexcept (requires{{advance(ranges{}, i)} noexcept;})
            requires (requires{{advance(ranges{}, i)};})
        {
            advance(ranges{}, i);
            return *this;
        }

        [[nodiscard]] friend constexpr zip_iterator operator+(
            const zip_iterator& self,
            difference_type i
        )
            noexcept (
                meta::nothrow::copyable<zip_iterator> &&
                meta::nothrow::has_iadd<zip_iterator&, difference_type>
            )
            requires (
                meta::copyable<zip_iterator> &&
                meta::has_iadd<zip_iterator&, difference_type>
            )
        {
            zip_iterator copy = self;
            copy += i;
            return copy;
        }

        [[nodiscard]] friend constexpr zip_iterator operator+(
            difference_type i,
            const zip_iterator& self
        )
            noexcept (
                meta::nothrow::copyable<zip_iterator> &&
                meta::nothrow::has_iadd<zip_iterator&, difference_type>
            )
            requires (
                meta::copyable<zip_iterator> &&
                meta::has_iadd<zip_iterator&, difference_type>
            )
        {
            zip_iterator copy = self;
            copy += i;
            return copy;
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

        constexpr zip_iterator& operator-=(difference_type i)
            noexcept (requires{{retreat(ranges{}, i)} noexcept;})
            requires (requires{{retreat(ranges{}, i)};})
        {
            retreat(ranges{}, i);
            return *this;
        }

        [[nodiscard]] constexpr zip_iterator operator-(difference_type i) const
            noexcept (
                meta::nothrow::copyable<zip_iterator> &&
                meta::nothrow::has_isub<zip_iterator&, difference_type>
            )
            requires (
                meta::copyable<zip_iterator> &&
                meta::has_isub<zip_iterator&, difference_type>
            )
        {
            zip_iterator copy = *this;
            copy -= i;
            return copy;
        }

        template <typename... Ts>
        [[nodiscard]] constexpr difference_type operator-(
            const zip_iterator<Self, Ts...>& other
        ) const
            noexcept (requires{{distance(ranges{}, other)} noexcept;})
            requires (requires{{distance(ranges{}, other)};})
        {
            return distance(ranges{}, other);
        }

        template <typename... Ts>
        [[nodiscard]] constexpr bool operator<(const zip_iterator<Self, Ts...>& other) const
            noexcept (requires{{lt(ranges{}, other)} noexcept;})
            requires (requires{{lt(ranges{}, other)};})
        {
            return lt(ranges{}, other);
        }

        template <typename... Ts>
        [[nodiscard]] constexpr bool operator<=(const zip_iterator<Self, Ts...>& other) const
            noexcept (requires{{le(ranges{}, other)} noexcept;})
            requires (requires{{le(ranges{}, other)};})
        {
            return le(ranges{}, other);
        }

        template <typename... Ts>
        [[nodiscard]] constexpr bool operator==(const zip_iterator<Self, Ts...>& other) const
            noexcept (requires{{eq(ranges{}, other)} noexcept;})
            requires (requires{{eq(ranges{}, other)};})
        {
            return eq(ranges{}, other);
        }

        template <typename... Ts>
        [[nodiscard]] constexpr bool operator!=(const zip_iterator<Self, Ts...>& other) const
            noexcept (requires{{ne(ranges{}, other)} noexcept;})
            requires (requires{{ne(ranges{}, other)};})
        {
            return ne(ranges{}, other);
        }

        template <typename... Ts>
        [[nodiscard]] constexpr bool operator>=(const zip_iterator<Self, Ts...>& other) const
            noexcept (requires{{ge(ranges{}, other)} noexcept;})
            requires (requires{{ge(ranges{}, other)};})
        {
            return ge(ranges{}, other);
        }

        template <typename... Ts>
        [[nodiscard]] constexpr bool operator>(const zip_iterator<Self, Ts...>& other) const
            noexcept (requires{{gt(ranges{}, other)} noexcept;})
            requires (requires{{gt(ranges{}, other)};})
        {
            return gt(ranges{}, other);
        }
    };
    template <typename Self, typename... Iters>
    zip_iterator(Self&&, Iters&&...) -> zip_iterator<
        meta::remove_rvalue<Self>,
        meta::remove_rvalue<Iters>...
    >;

    template <size_t min, typename...>
    constexpr size_t _zip_tuple_size = min;
    template <size_t min, typename T, typename... Ts>
    constexpr size_t _zip_tuple_size<min, T, Ts...> = _zip_tuple_size<min, Ts...>;
    template <size_t min, meta::range T, typename... Ts>
        requires (meta::tuple_like<T> && meta::tuple_size<T> < min)
    constexpr size_t _zip_tuple_size<min, T, Ts...> = _zip_tuple_size<meta::tuple_size<T>, Ts...>;
    template <typename... Ts>
    constexpr size_t zip_tuple_size = _zip_tuple_size<std::numeric_limits<size_t>::max(), Ts...>;

    template <typename F, typename... A>
    concept zip_concept =
        sizeof...(A) > 0 &&
        (meta::range<A> || ...) &&
        meta::callable<meta::as_lvalue<F>, zip_arg<A>...> &&
        meta::not_void<meta::call_type<meta::as_lvalue<F>, zip_arg<A>...>>;

    /* Zipped ranges store an arbitrary set of arguments as well as a function to apply
    over them or their elements, if any are ranges.  The overall length of the zipped
    range is always equal to the smallest of the range operands, and the function must
    always be callable with the unpacked values. */
    template <meta::not_rvalue F, meta::not_rvalue... A> requires (zip_concept<F, A...>)
    struct zip {
        using function_type = F;
        using argument_types = meta::pack<A...>;
        using args = std::index_sequence_for<A...>;
        using ranges = range_indices<A...>;

        [[no_unique_address]] impl::ref<F> m_func;
        [[no_unique_address]] impl::basic_tuple<A...> m_args;

        [[nodiscard]] constexpr zip(meta::forward<F> func, meta::forward<A>... args)
            noexcept (requires{
                {impl::ref<F>{F{std::forward<F>(func)}}} noexcept;
                {impl::basic_tuple<A...>{std::forward<A>(args)...}} noexcept;
            })
            requires (requires{
                {impl::ref<F>{F{std::forward<F>(func)}}};
                {impl::basic_tuple<A...>{std::forward<A>(args)...}};
            })
        :
            m_func{F{std::forward<F>(func)}},
            m_args{std::forward<A>(args)...}
        {}

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) func(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_func);
        }

        template <ssize_t I, typename Self> requires (impl::valid_index<sizeof...(A), I>)
        [[nodiscard]] constexpr decltype(auto) arg(this Self&& self) noexcept {
            return (std::forward<Self>(self).m_args.template get<
                impl::normalize_index<sizeof...(A), I>()
            >());
        }

    private:
        template <size_t... Is> requires (sizeof...(Is) == ranges::size())
        constexpr size_t _size(std::index_sequence<Is...>) const
            noexcept (requires{{iter::min{}(size_t(arg<Is>().size())...)} noexcept;})
            requires (requires{{iter::min{}(size_t(arg<Is>().size())...)};})
        {
            return iter::min{}(size_t(arg<Is>().size())...);
        }

        template <size_t... Is> requires (sizeof...(Is) == ranges::size())
        constexpr ssize_t _ssize(std::index_sequence<Is...>) const
            noexcept (requires{{iter::min{}(ssize_t(arg<Is>().ssize())...)} noexcept;})
            requires (requires{{iter::min{}(ssize_t(arg<Is>().ssize())...)};})
        {
            return iter::min{}(ssize_t(arg<Is>().ssize())...);
        }

        template <size_t... Is> requires (sizeof...(Is) == ranges::size())
        constexpr bool _empty(std::index_sequence<Is...>) const
            noexcept (requires{{(arg<Is>().empty() || ...)} noexcept;})
            requires (requires{{(arg<Is>().empty() || ...)};})
        {
            return (arg<Is>().empty() || ...);
        }

        template <size_t I, typename Self>
        constexpr decltype(auto) _front_impl(this Self&& self)
            noexcept (
                !meta::range<decltype((std::forward<Self>(self).template arg<I>()))> ||
                requires{{std::forward<Self>(self).template arg<I>().front()} noexcept;}
            )
            requires (
                !meta::range<decltype((std::forward<Self>(self).template arg<I>()))> ||
                meta::has_front<decltype((std::forward<Self>(self).template arg<I>()))>
            )
        {
            if constexpr (meta::range<decltype((std::forward<Self>(self).template arg<I>()))>) {
                return (std::forward<Self>(self).template arg<I>().front());
            } else {
                return (std::forward<Self>(self).template arg<I>());
            }
        }

        template <typename Self, size_t... Is> requires (sizeof...(Is) == sizeof...(A))
        constexpr decltype(auto) _front(this Self&& self, std::index_sequence<Is...>)
            noexcept (requires{{std::forward<Self>(self).func()(
                std::forward<Self>(self).template _front_impl<Is>()...
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).func()(
                std::forward<Self>(self).template _front_impl<Is>()...
            )};})
        {
            return (std::forward<Self>(self).func()(
                std::forward<Self>(self).template _front_impl<Is>()...
            ));
        }

        template <size_t I, typename Self>
        constexpr decltype(auto) _subscript_impl(this Self&& self, size_t i)
            noexcept (
                !meta::range<decltype((std::forward<Self>(self).template arg<I>()))> ||
                requires{{std::forward<Self>(self).template arg<I>()[i]} noexcept;}
            )
            requires (
                !meta::range<decltype((std::forward<Self>(self).template arg<I>()))> ||
                meta::has_subscript<decltype((std::forward<Self>(self).template arg<I>())), size_t>
            )
        {
            if constexpr (meta::range<decltype((std::forward<Self>(self).template arg<I>()))>) {
                return (std::forward<Self>(self).template arg<I>()[i]);
            } else {
                return (std::forward<Self>(self).template arg<I>());
            }
        }

        template <typename Self, size_t... Is> requires (sizeof...(Is) == sizeof...(A))
        constexpr decltype(auto) _subscript(
            this Self&& self,
            std::index_sequence<Is...>,
            size_t i
        )
            noexcept (requires{{std::forward<Self>(self).func()(
                std::forward<Self>(self).template _subscript_impl<Is>(i)...
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).func()(
                std::forward<Self>(self).template _subscript_impl<Is>(i)...
            )};})
        {
            return (std::forward<Self>(self).func()(
                std::forward<Self>(self).template _subscript_impl<Is>(i)...
            ));
        }

        template <size_t J, size_t I, typename Self>
        constexpr decltype(auto) _get_impl(this Self&& self)
            noexcept (
                !meta::range<decltype((std::forward<Self>(self).template arg<I>()))> ||
                requires{{std::forward<Self>(self).template arg<I>().template get<J>()} noexcept;}
            )
            requires (
                !meta::range<decltype((std::forward<Self>(self).template arg<I>()))> ||
                meta::has_get<decltype((std::forward<Self>(self).template arg<I>())), J>
            )
        {
            if constexpr (meta::range<decltype((std::forward<Self>(self).template arg<I>()))>) {
                return (std::forward<Self>(self).template arg<I>().template get<J>());
            } else {
                return (std::forward<Self>(self).template arg<I>());
            }
        }

        template <size_t J, typename Self, size_t... Is> requires (sizeof...(Is) == sizeof...(A))
        constexpr decltype(auto) _get(this Self&& self, std::index_sequence<Is...>)
            noexcept (requires{{std::forward<Self>(self).func()(
                std::forward<Self>(self).template _get_impl<J, Is>()...
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).func()(
                std::forward<Self>(self).template _get_impl<J, Is>()...
            )};})
        {
            return (std::forward<Self>(self).func()(
                std::forward<Self>(self).template _get_impl<J, Is>()...
            ));
        }

        template <typename Self, size_t... Rs>
        constexpr auto _begin(this Self&& self, std::index_sequence<Rs...>)
            noexcept (requires{{zip_iterator{
                std::forward<Self>(self),
                meta::begin(std::forward<Self>(self).template arg<Rs>())...
            }} noexcept;})
            requires (requires{{zip_iterator{
                std::forward<Self>(self),
                meta::begin(std::forward<Self>(self).template arg<Rs>())...
            }};})
        {
            return zip_iterator{
                std::forward<Self>(self),
                meta::begin(std::forward<Self>(self).template arg<Rs>())...
            };
        }

        template <typename Self, size_t... Rs>
        constexpr auto _end(this Self&& self, std::index_sequence<Rs...>)
            noexcept (requires{{zip_iterator{
                std::forward<Self>(self),
                meta::end(std::forward<Self>(self).template arg<Rs>())...
            }} noexcept;})
            requires (requires{{zip_iterator{
                std::forward<Self>(self),
                meta::end(std::forward<Self>(self).template arg<Rs>())...
            }};})
        {
            return zip_iterator{
                std::forward<Self>(self),
                meta::end(std::forward<Self>(self).template arg<Rs>())...
            };
        }

        template <size_t R, typename Self>
        constexpr auto _rbegin_impl(this Self&& self, size_t size)
            noexcept (requires(meta::rbegin_type<decltype((
                std::forward<Self>(self).template arg<R>()
            ))> it) {
                {self.template arg<R>().size()} noexcept -> meta::nothrow::convertible_to<size_t>;
                {meta::rbegin(std::forward<Self>(self).template arg<R>())} noexcept;
                {it += size} noexcept;
            })
            requires (requires(meta::rbegin_type<decltype((
                std::forward<Self>(self).template arg<R>()
            ))> it) {
                {self.template arg<R>().size()} -> meta::convertible_to<size_t>;
                {meta::rbegin(std::forward<Self>(self).template arg<R>())};
                {it += size};
            })
        {
            size = size_t(self.template arg<R>().size()) - size;
            auto it = meta::rbegin(std::forward<Self>(self).template arg<R>());
            it += size;
            return it;
        }

        template <typename Self, size_t... Rs>
        constexpr auto _rbegin(this Self&& self, std::index_sequence<Rs...>, size_t size)
            noexcept (requires{{zip_iterator{
                std::forward<Self>(self),
                std::forward<Self>(self).template _rbegin_impl<Rs>(size)...
            }} noexcept;})
            requires (requires{{zip_iterator{
                std::forward<Self>(self),
                std::forward<Self>(self).template _rbegin_impl<Rs>(size)...
            }};})
        {
            return zip_iterator{
                std::forward<Self>(self),
                std::forward<Self>(self).template _rbegin_impl<Rs>(size)...
            };
        }

        template <typename Self, size_t... Rs>
        constexpr auto _rend(this Self&& self, std::index_sequence<Rs...>)
            noexcept (requires{{zip_iterator{
                std::forward<Self>(self),
                meta::rend(std::forward<Self>(self).template arg<Rs>())...
            }} noexcept;})
            requires (requires{{zip_iterator{
                std::forward<Self>(self),
                meta::rend(std::forward<Self>(self).template arg<Rs>())...
            }};})
        {
            return zip_iterator{
                std::forward<Self>(self),
                meta::rend(std::forward<Self>(self).template arg<Rs>())...
            };
        }

    public:
        [[nodiscard]] constexpr size_t size() const
            noexcept (
                ((!meta::range<A> || meta::tuple_like<A>) && ...) ||
                requires{{_size(ranges{})} noexcept;}
            )
            requires ((!meta::range<A> || meta::has_size<A>) && ...)
        {
            if constexpr (((!meta::range<A> || meta::tuple_like<A>) && ...)) {
                return zip_tuple_size<A...>;
            } else {
                return _size(ranges{});
            }
        }

        [[nodiscard]] constexpr ssize_t ssize() const
            noexcept (
                ((!meta::range<A> || meta::tuple_like<A>) && ...) ||
                requires{{_ssize(ranges{})} noexcept;}
            )
            requires ((!meta::range<A> || meta::has_size<A>) && ...)
        {
            if constexpr (((!meta::range<A> || meta::tuple_like<A>) && ...)) {
                return zip_tuple_size<A...>;
            } else {
                return _ssize(ranges{});
            }
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (
                ((!meta::range<A> || meta::tuple_like<A>) && ...) ||
                requires{{_empty(ranges{})} noexcept;}
            )
            requires ((!meta::range<A> || meta::has_empty<A>) && ...)
        {
            if constexpr (((!meta::range<A> || meta::tuple_like<A>) && ...)) {
                return zip_tuple_size<A...> == 0;
            } else {
                return _empty(ranges{});
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self)
            noexcept (requires{
                {std::forward<Self>(self)._front(args{})} noexcept;
            })
            requires (requires{
                {std::forward<Self>(self)._front(args{})};
            })
        {
            return (std::forward<Self>(self)._front(args{}));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) back(this Self&& self)
            noexcept (requires{
                {std::forward<Self>(self)._subscript(args{}, self.size() - 1)} noexcept;
            })
            requires (requires{
                {std::forward<Self>(self)._subscript(args{}, self.size() - 1)};
            })
        {
            return (std::forward<Self>(self)._subscript(args{}, self.size() - 1));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, ssize_t i)
            noexcept (requires{{std::forward<Self>(self)._subscript(
                args{},
                size_t(impl::normalize_index(self.ssize(), i))
            )} noexcept;})
            requires (requires{{std::forward<Self>(self)._subscript(
                args{},
                size_t(impl::normalize_index(self.ssize(), i))
            )};})
        {
            return (std::forward<Self>(self)._subscript(
                args{},
                size_t(impl::normalize_index(self.ssize(), i))
            ));
        }

        template <ssize_t I, typename Self>
            requires (
                ((!meta::range<A> || meta::tuple_like<A>) && ...) &&
                impl::valid_index<zip_tuple_size<A...>, I>
            )
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).template _get<impl::normalize_index<
                zip_tuple_size<A...>,
                I
            >()>(args{})} noexcept;})
            requires (requires{{std::forward<Self>(self).template _get<impl::normalize_index<
                zip_tuple_size<A...>,
                I
            >()>(args{})};})
        {
            return (std::forward<Self>(self).template _get<impl::normalize_index<
                zip_tuple_size<A...>,
                I
            >()>(args{}));
        }


        template <typename Self>
        [[nodiscard]] constexpr auto begin(this Self&& self)
            noexcept (requires{{std::forward<Self>(self)._begin(ranges{})} noexcept;})
            requires (requires{{std::forward<Self>(self)._begin(ranges{})};})
        {
            return std::forward<Self>(self)._begin(ranges{});
        }

        template <typename Self>
        [[nodiscard]] constexpr auto end(this Self&& self)
            noexcept (requires{{std::forward<Self>(self)._end(ranges{})} noexcept;})
            requires (requires{{std::forward<Self>(self)._end(ranges{})};})
        {
            return std::forward<Self>(self)._end(ranges{});
        }

        template <typename Self>
        [[nodiscard]] constexpr auto rbegin(this Self&& self)
            noexcept (requires{
                {std::forward<Self>(self)._rbegin(ranges{}, self.size())} noexcept;
            })
            requires (requires{
                {std::forward<Self>(self)._rbegin(ranges{}, self.size())};
            })
        {
            return std::forward<Self>(self)._rbegin(ranges{}, self.size());
        }

        template <typename Self>
        [[nodiscard]] constexpr auto rend(this Self&& self)
            noexcept (requires{{std::forward<Self>(self)._rend(ranges{})} noexcept;})
            requires (requires{{std::forward<Self>(self)._rend(ranges{})};})
        {
            return std::forward<Self>(self)._rend(ranges{});
        }
    };
    template <typename F, typename... A>
    zip(F&&, A&&...) -> zip<meta::remove_rvalue<F>, meta::remove_rvalue<A>...>;

    /// TODO: fill in zip_tuple once `Tuple` is in scope

    /* If no transformation function is provided, then `zip{}` will default to
    returning each value as a `Tuple`, similar to `std::views::zip()` or `zip()` in
    Python. */
    struct zip_tuple {
        template <typename... A>
        [[nodiscard]] constexpr auto operator()(A&&... args);
        //     noexcept (requires{{Tuple{std::forward<A>(args)...}} noexcept;})
        //     requires (requires{{Tuple{std::forward<A>(args)...}};})
        // {
        //     return Tuple{std::forward<A>(args)...};
        // }
    };

    /* If any of the arguments to `iter::at{}` (and therefore the range subscript
    operator) are ranges, then they will be zipped into a fancy index, which otherwise
    preserves shape and all the other normalizations of that algorithm. */
    template <meta::not_rvalue T>
    struct zip_subscript;
    template <typename T>
    zip_subscript(T&&) -> zip_subscript<meta::remove_rvalue<T>>;

    /* The range call operator simply inserts the range elements as an additional
    argument, and then invokes it with the remaining arguments.  Zip handles the
    rest. */
    struct zip_call {
        template <typename F, typename... A>
        constexpr decltype(auto) operator()(F&& func, A&&... args)
            noexcept (requires{{std::forward<F>(func)(std::forward<A>(args)...)} noexcept;})
            requires (requires{{std::forward<F>(func)(std::forward<A>(args)...)};})
        {
            return (std::forward<F>(func)(std::forward<A>(args)...));
        }
    };

    /* The range visit operator `->*` attempts to destructure tuple inputs if the
    visitor is not immediately callable with the yield type and that yield type is a
    tuple. */
    template <meta::not_rvalue F>
    struct zip_visit {
        [[no_unique_address]] impl::ref<F> func;

        [[nodiscard]] constexpr zip_visit(meta::forward<F> f)
            noexcept (requires{{impl::ref<F>{F{std::forward<F>(f)}}} noexcept;})
            requires (requires{{impl::ref<F>{F{std::forward<F>(f)}}};})
        :
            func{F{std::forward<F>(f)}}
        {}

        template <typename T>
        [[nodiscard]] constexpr decltype(auto) operator()(T&& val)
            noexcept (requires{{(*func)(std::forward<T>(val))} noexcept;})
            requires (requires{{(*func)(std::forward<T>(val))};})
        {
            return (*func)(std::forward<T>(val));
        }

        template <typename T>
        [[nodiscard]] constexpr decltype(auto) operator()(T&& val) const
            noexcept (requires{{(*func)(std::forward<T>(val))} noexcept;})
            requires (requires{{(*func)(std::forward<T>(val))};})
        {
            return ((*func)(std::forward<T>(val)));
        }

    private:
        template <meta::tuple_like T, size_t... Is>
        constexpr decltype(auto) tuple(T&& val, std::index_sequence<Is...>)
            noexcept (requires{{(*func)(meta::get<Is>(std::forward<T>(val))...)} noexcept;})
            requires (requires{{(*func)(meta::get<Is>(std::forward<T>(val))...)};})
        {
            return ((*func)(meta::get<Is>(std::forward<T>(val))...));
        }

        template <meta::tuple_like T, size_t... Is>
        constexpr decltype(auto) tuple(T&& val, std::index_sequence<Is...>) const
            noexcept (requires{{(*func)(meta::get<Is>(std::forward<T>(val))...)} noexcept;})
            requires (requires{{(*func)(meta::get<Is>(std::forward<T>(val))...)};})
        {
            return ((*func)(meta::get<Is>(std::forward<T>(val))...));
        }

    public:
        template <meta::tuple_like T>
        [[nodiscard]] constexpr decltype(auto) operator()(T&& val)
            noexcept (requires{{tuple(
                std::forward<T>(val),
                std::make_index_sequence<meta::tuple_size<T>>{}
            )} noexcept;})
            requires (
                !requires{{func(std::forward<T>(val))};} &&
                requires{{tuple(
                    std::forward<T>(val),
                    std::make_index_sequence<meta::tuple_size<T>>{}
                )};}
            )
        {
            return tuple(
                std::forward<T>(val),
                std::make_index_sequence<meta::tuple_size<T>>{}
            );
        }

        template <meta::tuple_like T>
        [[nodiscard]] constexpr decltype(auto) operator()(T&& val) const
            noexcept (requires{{tuple(
                std::forward<T>(val),
                std::make_index_sequence<meta::tuple_size<T>>{}
            )} noexcept;})
            requires (
                !requires{{func(std::forward<T>(val))};} &&
                requires{{tuple(
                    std::forward<T>(val),
                    std::make_index_sequence<meta::tuple_size<T>>{}
                )};}
            )
        {
            return tuple(
                std::forward<T>(val),
                std::make_index_sequence<meta::tuple_size<T>>{}
            );
        }
    };
    template <typename F>
    zip_visit(F&&) -> zip_visit<meta::remove_rvalue<F>>;

    template <typename L, typename R>
    concept zip_visitable =
        meta::callable<zip_visit<meta::remove_rvalue<R>>, zip_arg<L>> &&
        meta::not_void<meta::call_type<zip_visit<meta::remove_rvalue<R>>, zip_arg<L>>>;

    /* Using an empty range as a range algorithm produces a comprehension that converts
    the first non-range yield type into a range, effectively increasing its rank by
    1. */
    struct zip_nest {
        template <typename T> requires (!meta::range<T>)
        [[nodiscard]] static constexpr auto operator()(T&& val)
            noexcept (requires{
                {iter::range<meta::remove_rvalue<T>>{std::forward<T>(val)}} noexcept;
            })
            requires (requires{
                {iter::range<meta::remove_rvalue<T>>{std::forward<T>(val)}};
            })
        {
            return iter::range<meta::remove_rvalue<T>>{std::forward<T>(val)};
        }
    };

}


namespace iter {

    /* Range-based multidimensional indexing operator, with support for both
    compile-time (tuple-like) and runtime (subscript) fancy indexing.

    This operator accepts any number of values as initializers, which may be given as
    either non-type template parameters (for compile-time indices) or constructor
    arguments (for runtime indices - supported by CTAD), but not both.  The resulting
    function object can be invoked with a generic container to execute the indexing
    operation.  The precise behavior for each index is as follows (in order of
    precedence):

        1.  Direct indexing via `get<index>(container)` (or an equivalent
            `container.get<index>()` member method) or `container[index]`.  The tuple
            unpacking operator is preferred if all inputs are known at compile time,
            while the subscript operator is preferred for runtime indices.  The other
            operator will be used as a fallback.  Runtime subscripting of tuples is
            implemented as a dynamic dispatch, which may return a union if the tuple is
            heterogenous, and will only consider a single integer index with
            Python-style wraparound for negative values.
        2.  Integer values that do not satisfy (1).  These will be interpreted as
            offsets from the `begin()` iterator of the container, and will be applied
            using iterator arithmetic, including a linear traversal if necessary.  If
            the container also supports reverse iteration and the index is negative,
            then the offset will be applied from the `rbegin()` iterator instead,
            effectively applying Python-style wraparound (but not bounds checking) for
            negative indices.
        3.  Predicate functions that take the container as an argument and return an
            arbitrary value.  This includes both user-defined functions as well as
            range adaptors within the `iter::` namespace, such as `iter::slice{}`,
            `iter::where{}`, etc.  This allows arbitrary indexing logic, assuming
            neither of the first two cases apply.

    If more than one index is given, they will be forwarded to the container's indexing
    operator directly where possible (e.g. `container[i, j, k...]`) and sequentially
    where not (e.g. `container[i, j][k]...`), which can occur in any combination as
    long as the underlying types support it.  Practically speaking, this means that the
    `at{}` operator behaves symmetrically for both multidimensional containers as well
    as nested "container of container" structures simultaneously.
    
    Additionally, if any of the indices are ranges, then they will be broadcasted to
    the same length using `iter::zip{}` and combined into a fancy index, which yields
    another range where each element is the result of indexing with the corresponding
    zipped row of indices.  This process may recur as long as there are ranges present
    in the index list, preserving the overall shape of the input ranges.  For example:

        ```
        range r = std::array{1, 2, 3};
        auto result = r[range{std::array{
            range{std::array{1, 0}},
            range{std::array{0, 1}},
        }}];
        assert(result[0][0] == 2);
        assert(result[0][1] == 1);
        assert(result[1][0] == 1);
        assert(result[1][1] == 2);
        ```

    Note that this form of advanced indexing can change the shape of the resulting
    data, similar to NumPy fancy indexing.  It also does not interfere with any of the
    other normalizations that are applied by this algorithm, which will be applied
    individually for each zipped row of indices.  The only difference from numpy is
    that boolean masks are not handled specially here, and are treated as normal ranges
    of inputs to `at{}`.  Filter-based indexing can be achieved using the
    `iter::where{}` range algorithm instead, which is more generic. */
    template <auto... K> requires (impl::range_index::valid<decltype(K)...>)
    struct at {
    private:
        template <size_t N, typename>
        struct _recur;
        template <size_t N, size_t... I>
        struct _recur<N, std::index_sequence<I...>> {
            using type = at<meta::unpack_value<N + I, K...>...>;
        };
        template <size_t N> requires (N < sizeof...(K))
        using recur = _recur<N, std::make_index_sequence<sizeof...(K) - N>>::type;

        template <auto... A, typename C> requires (sizeof...(A) == sizeof...(K))
        static constexpr decltype(auto) get(C&& c)
            noexcept (meta::nothrow::has_get<C, A...>)
            requires (meta::has_get<C, A...>)
        {
            return (meta::get<A...>(std::forward<C>(c)));
        }

        template <auto... A, typename C> requires (sizeof...(A) < sizeof...(K))
        static constexpr decltype(auto) get(C&& c)
            noexcept (requires{
                {get<A..., meta::unpack_value<sizeof...(A), K...>>(std::forward<C>(c))} noexcept;
            })
            requires (
                (
                    !meta::tuple_like<C> ||
                    !meta::integer<decltype(meta::unpack_value<sizeof...(A), K...>)> || (
                        meta::unpack_value<sizeof...(A), K...> <
                        meta::to_signed(meta::tuple_size<C>)
                    )
                ) &&
                meta::has_get<C, A..., meta::unpack_value<sizeof...(A), K...>> &&
                requires{
                    {get<A..., meta::unpack_value<sizeof...(A), K...>>(std::forward<C>(c))};
                }
            )
        {
            return (get<A..., meta::unpack_value<sizeof...(A), K...>>(std::forward<C>(c)));
        }

        template <auto... A, typename C> requires (sizeof...(A) < sizeof...(K))
        static constexpr decltype(auto) get(C&& c)
            noexcept (requires{
                {recur<sizeof...(A)>{}(meta::get<A...>(std::forward<C>(c)))} noexcept;
            })
            requires (
                (
                    (
                        meta::tuple_like<C> &&
                        meta::integer<decltype(meta::unpack_value<sizeof...(A), K...>)> && (
                            meta::unpack_value<sizeof...(A), K...> >=
                            meta::to_signed(meta::tuple_size<C>)
                        )
                    ) ||
                    !meta::has_get<C, A..., meta::unpack_value<sizeof...(A), K...>>
                ) &&
                requires{
                    {recur<sizeof...(A)>{}(meta::get<A...>(std::forward<C>(c)))};
                }
            )
        {
            return (recur<sizeof...(A)>{}(meta::get<A...>(std::forward<C>(c))));
        }

        template <auto... A, typename C> requires (sizeof...(A) == sizeof...(K))
        static constexpr decltype(auto) subscript(C&& c)
            noexcept (requires{{std::forward<C>(c)[A...]} noexcept;})
            requires (requires{{std::forward<C>(c)[A...]};})
        {
            return (std::forward<C>(c)[A...]);
        }

        template <auto... A, typename C> requires (sizeof...(A) < sizeof...(K))
        static constexpr decltype(auto) subscript(C&& c)
            noexcept (requires{
                {subscript<A..., meta::unpack_value<sizeof...(A), K...>>(std::forward<C>(c))} noexcept;
            })
            requires (requires{
                {std::forward<C>(c)[A..., meta::unpack_value<sizeof...(A)>]};
                {subscript<A..., meta::unpack_value<sizeof...(A), K...>>(std::forward<C>(c))};
            })
        {
            return (subscript<A..., meta::unpack_value<sizeof...(A), K...>>(std::forward<C>(c)));
        }

        template <auto... A, typename C> requires (sizeof...(A) < sizeof...(K))
        static constexpr decltype(auto) subscript(C&& c)
            noexcept (requires{{recur<sizeof...(A)>{}(std::forward<C>(c)[A...])} noexcept;})
            requires (
                !requires{{std::forward<C>(c)[A..., meta::unpack_value<sizeof...(A)>]};} &&
                requires{{recur<sizeof...(A)>{}(std::forward<C>(c)[A...])};}
            )
        {
            return (recur<sizeof...(A)>{}(std::forward<C>(c)[A...]));
        }

        template <typename C>
        static constexpr decltype(auto) offset(C&& c)
            noexcept (requires{{impl::range_index::offset(
                std::forward<C>(c),
                meta::unpack_value<0, K...>
            )} noexcept;})
            requires (sizeof...(K) == 1 && requires{{impl::range_index::offset(
                std::forward<C>(c),
                meta::unpack_value<0, K...>
            )};})
        {
            return (impl::range_index::offset(
                std::forward<C>(c),
                meta::unpack_value<0, K...>
            ));
        }

        template <typename C>
        static constexpr decltype(auto) offset(C&& c)
            noexcept (requires{{recur<1>{}(impl::range_index::offset(
                std::forward<C>(c),
                meta::unpack_value<0, K...>
            ))} noexcept;})
            requires (sizeof...(K) > 1 && requires{{recur<1>{}(impl::range_index::offset(
                std::forward<C>(c),
                meta::unpack_value<0, K...>
            ))};})
        {
            return (recur<1>{}(impl::range_index::offset(
                std::forward<C>(c),
                meta::unpack_value<0, K...>
            )));
        }

        template <typename C> requires (sizeof...(K) == 1)
        static constexpr decltype(auto) invoke(C&& c)
            noexcept (requires{{meta::unpack_value<0, K...>(std::forward<C>(c))} noexcept;})
            requires (requires{
                {meta::unpack_value<0, K...>(std::forward<C>(c))} -> meta::not_void;
            })
        {
            return (meta::unpack_value<0, K...>(std::forward<C>(c)));
        }

        template <typename C> requires (sizeof...(K) > 1)
        static constexpr decltype(auto) invoke(C&& c)
            noexcept (requires{{recur<1>{}(
                meta::unpack_value<0, K...>(std::forward<C>(c))
            )} noexcept;})
            requires (requires{{recur<1>{}(
                meta::unpack_value<0, K...>(std::forward<C>(c))
            )} -> meta::not_void;})
        {
            return (recur<1>{}(meta::unpack_value<0, K...>(std::forward<C>(c))));
        }

    public:
        template <typename C> requires (!meta::range<decltype(K)> && ...)
        [[nodiscard]] static constexpr decltype(auto) operator()(C&& c)
            noexcept (
                requires{{get(meta::strip_range(std::forward<C>(c)))};} ?
                requires{{get(meta::strip_range(std::forward<C>(c)))} noexcept;} : (
                    requires{{subscript(meta::strip_range(std::forward<C>(c)))};} ?
                    requires{{subscript(meta::strip_range(std::forward<C>(c)))} noexcept;} : (
                        requires{{offset(meta::strip_range(std::forward<C>(c)))};} ?
                        requires{{offset(meta::strip_range(std::forward<C>(c)))} noexcept;} :
                        requires{{invoke(meta::from_range(std::forward<C>(c)))} noexcept;}
                    )
                )
            )
            requires (
                requires{{get(meta::strip_range(std::forward<C>(c)))};} ||
                requires{{subscript(meta::strip_range(std::forward<C>(c)))};} ||
                requires{{offset(meta::strip_range(std::forward<C>(c)))};} ||
                requires{{invoke(meta::from_range(std::forward<C>(c)))};}
            )
        {
            if constexpr (requires{{get(meta::strip_range(std::forward<C>(c)))};}) {
                return (get(meta::strip_range(std::forward<C>(c))));
            } else if constexpr (requires{{subscript(meta::strip_range(std::forward<C>(c)))};}) {
                return (subscript(meta::strip_range(std::forward<C>(c))));
            } else if constexpr (requires{{offset(meta::strip_range(std::forward<C>(c)))};}) {
                return (offset(meta::strip_range(std::forward<C>(c))));
            } else {
                return (invoke(meta::from_range(std::forward<C>(c))));
            }
        }

        template <typename C> requires (meta::range<decltype(K)> || ...)
        [[nodiscard]] static constexpr auto operator()(C&& c)
            noexcept (requires{{range<impl::zip<
                impl::zip_subscript<meta::remove_rvalue<C>>,
                meta::remove_rvalue<decltype((K))>...
            >>{
                impl::zip_subscript{std::forward<C>(c)},
                K...
            }} noexcept;})
            requires (requires{{range<impl::zip<
                impl::zip_subscript<meta::remove_rvalue<C>>,
                meta::remove_rvalue<decltype((K))>...
            >>{
                impl::zip_subscript{std::forward<C>(c)},
                K...
            }};})
        {
            return range<impl::zip<
                impl::zip_subscript<meta::remove_rvalue<C>>,
                meta::remove_rvalue<decltype((K))>...
            >>{
                impl::zip_subscript{std::forward<C>(c)},
                K...
            };
        }
    };
    template <auto... K>
        requires (
            impl::range_index::valid<decltype(K)...> &&
            impl::range_index::runtime<decltype(K)...>
        )
    struct at<K...> {
        [[no_unique_address]] impl::basic_tuple<typename decltype(K)::type...> idx;

        [[nodiscard]] constexpr at() = default;
        [[nodiscard]] constexpr at(meta::forward<typename decltype(K)::type>... k)
            noexcept (requires{{impl::basic_tuple<typename decltype(K)::type...>{
                std::forward<typename decltype(K)::type>(k)...
            }} noexcept;})
            requires (requires{{impl::basic_tuple<typename decltype(K)::type...>{
                std::forward<typename decltype(K)::type>(k)...
            }};})
        :
            idx{std::forward<typename decltype(K)::type>(k)...}
        {}

    private:
        template <size_t N = 0> requires (N <= sizeof...(K))
        struct eval {
            template <typename Self, typename C, typename... A>
                requires ((N + sizeof...(A)) == sizeof...(K))
            static constexpr decltype(auto) subscript(Self&& self, C&& c, A&&... a)
                noexcept (requires{{std::forward<C>(c)[std::forward<A>(a)...]} noexcept;})
                requires (requires{{std::forward<C>(c)[std::forward<A>(a)...]};})
            {
                return (std::forward<C>(c)[std::forward<A>(a)...]);
            }

            template <typename Self, typename C, typename... A>
                requires ((N + sizeof...(A)) < sizeof...(K))
            static constexpr decltype(auto) subscript(Self&& self, C&& c, A&&... a)
                noexcept (requires{{subscript(
                    std::forward<Self>(self),
                    std::forward<C>(c),
                    std::forward<A>(a)...,
                    std::forward<Self>(self).idx.template get<N + sizeof...(A)>()
                )} noexcept;})
                requires (requires{
                    {std::forward<C>(c)[
                        std::forward<A>(a)...,
                        std::forward<Self>(self).idx.template get<N + sizeof...(A)>()
                    ]};
                    {subscript(
                        std::forward<Self>(self),
                        std::forward<C>(c),
                        std::forward<A>(a)...,
                        std::forward<Self>(self).idx.template get<N + sizeof...(A)>()
                    )};
                })
            {
                return (subscript(
                    std::forward<Self>(self),
                    std::forward<C>(c),
                    std::forward<A>(a)...,
                    std::forward<Self>(self).idx.template get<N + sizeof...(A)>()
                ));
            }

            template <typename Self, typename C, typename... A> requires (sizeof...(A) > 0)
            static constexpr decltype(auto) subscript(Self&& self, C&& c, A&&... a)
                noexcept (requires{{eval<N + sizeof...(A)>::subscript(
                    std::forward<Self>(self),
                    std::forward<C>(c),
                    std::forward<A>(a)...
                )} noexcept;})
                requires (
                    (N + sizeof...(A)) < sizeof...(K) &&
                    !requires{{subscript(
                        std::forward<Self>(self),
                        std::forward<C>(c),
                        std::forward<A>(a)...,
                        std::forward<Self>(self).idx.template get<N + sizeof...(A)>()
                    )};} &&
                    requires{{eval<N + sizeof...(A)>::subscript(
                        std::forward<Self>(self),
                        std::forward<C>(c),
                        std::forward<A>(a)...
                    )};}
                )
            {
                return (eval<N + sizeof...(A)>::subscript(
                    std::forward<Self>(self),
                    std::forward<C>(c),
                    std::forward<A>(a)...
                ));
            }

            template <typename Self, meta::tuple_like C> requires (N + 1 == sizeof...(K))
            static constexpr decltype(auto) get(Self&& self, C&& c)
                noexcept (requires{{impl::range_index::vtable<C>{size_t(impl::normalize_index(
                    meta::tuple_size<C>,
                    std::forward<Self>(self).idx.template get<N>()
                ))}(std::forward<C>(c))} noexcept;})
                requires (requires{{impl::range_index::vtable<C>{size_t(impl::normalize_index(
                    meta::tuple_size<C>,
                    std::forward<Self>(self).idx.template get<N>()
                ))}(std::forward<C>(c))};})
            {
                return (impl::range_index::vtable<C>{size_t(impl::normalize_index(
                    meta::tuple_size<C>,
                    std::forward<Self>(self).idx.template get<N>()
                ))}(std::forward<C>(c)));
            }

            template <typename Self, meta::tuple_like C> requires (N + 1 < sizeof...(K))
            static constexpr decltype(auto) get(Self&& self, C&& c)
                noexcept (requires{{eval<N + 1>::get(
                    std::forward<Self>(self),
                    impl::range_index::vtable<C>{size_t(impl::normalize_index(
                        meta::tuple_size<C>,
                        std::forward<Self>(self).idx.template get<N>()
                    ))}(std::forward<C>(c))
                )} noexcept;})
                requires (requires{{eval<N + 1>::get(
                    std::forward<Self>(self),
                    impl::range_index::vtable<C>{size_t(impl::normalize_index(
                        meta::tuple_size<C>,
                        std::forward<Self>(self).idx.template get<N>()
                    ))}(std::forward<C>(c))
                )};})
            {
                return (eval<N + 1>::get(
                    std::forward<Self>(self),
                    impl::range_index::vtable<C>{size_t(impl::normalize_index(
                        meta::tuple_size<C>,
                        std::forward<Self>(self).idx.template get<N>()
                    ))}(std::forward<C>(c))
                ));
            }

            template <typename Self, typename C> requires (N + 1 == sizeof...(K))
            static constexpr decltype(auto) offset(Self&& self, C&& c)
                noexcept (requires{{impl::range_index::offset(
                    std::forward<C>(c),
                    std::forward<Self>(self).idx.template get<N>()
                )} noexcept;})
                requires (requires{{impl::range_index::offset(
                    std::forward<C>(c),
                    std::forward<Self>(self).idx.template get<N>()
                )};})
            {
                return (impl::range_index::offset(
                    std::forward<C>(c),
                    std::forward<Self>(self).idx.template get<N>()
                ));
            }

            template <typename Self, typename C> requires (N + 1 < sizeof...(K))
            static constexpr decltype(auto) offset(Self&& self, C&& c)
                noexcept (requires{{eval<N + 1>::offset(
                    std::forward<Self>(self),
                    impl::range_index::offset(
                        std::forward<C>(c),
                        std::forward<Self>(self).idx.template get<N>()
                    ))
                } noexcept;})
                requires (requires{{eval<N + 1>::offset(
                    std::forward<Self>(self),
                    impl::range_index::offset(
                        std::forward<C>(c),
                        std::forward<Self>(self).idx.template get<N>()
                    ))
                };})
            {
                return (eval<N + 1>::offset(
                    std::forward<Self>(self),
                    impl::range_index::offset(
                        std::forward<C>(c),
                        std::forward<Self>(self).idx.template get<N>()
                    )
                ));
            }

            template <typename Self, typename C> requires (N + 1 == sizeof...(K))
            static constexpr decltype(auto) invoke(Self&& self, C&& c)
                noexcept (requires{{std::forward<Self>(self).idx.template get<N>()(
                    std::forward<C>(c)
                )} noexcept;})
                requires (requires{{std::forward<Self>(self).idx.template get<N>()(
                    std::forward<C>(c)
                )};})
            {
                return (std::forward<Self>(self).idx.template get<N>()(std::forward<C>(c)));
            }

            template <typename Self, typename C> requires (N + 1 < sizeof...(K))
            static constexpr decltype(auto) invoke(Self&& self, C&& c)
                noexcept (requires{{eval<N + 1>::invoke(
                    std::forward<Self>(self),
                    std::forward<Self>(self).idx.template get<N>()(std::forward<C>(c)))
                } noexcept;})
                requires (requires{{eval<N + 1>::invoke(
                    std::forward<Self>(self),
                    std::forward<Self>(self).idx.template get<N>()(std::forward<C>(c)))
                };})
            {
                return (eval<N + 1>::invoke(
                    std::forward<Self>(self),
                    std::forward<Self>(self).idx.template get<N>()(std::forward<C>(c))
                ));
            }
        };

        template <typename Self, typename C, size_t... I>
        constexpr auto zip(this Self&& self, C&& c, std::index_sequence<I...>)
            noexcept (requires{{range<impl::zip<
                impl::zip_subscript<meta::remove_rvalue<C>>,
                meta::remove_rvalue<decltype((std::forward<Self>(self).idx.template get<I>()))>...
            >>{
                impl::zip_subscript{std::forward<C>(c)},
                std::forward<Self>(self).idx.template get<I>()...
            }} noexcept;})
            requires (requires{{range<impl::zip<
                impl::zip_subscript<meta::remove_rvalue<C>>,
                meta::remove_rvalue<decltype((std::forward<Self>(self).idx.template get<I>()))>...
            >>{
                impl::zip_subscript{std::forward<C>(c)},
                std::forward<Self>(self).idx.template get<I>()...
            }};})
        {
            return range<impl::zip<
                impl::zip_subscript<meta::remove_rvalue<C>>,
                meta::remove_rvalue<decltype((std::forward<Self>(self).idx.template get<I>()))>...
            >>{
                impl::zip_subscript{std::forward<C>(c)},
                std::forward<Self>(self).idx.template get<I>()...
            };
        }

    public:
        template <typename C> requires (sizeof...(K) == 0)
        [[nodiscard]] static constexpr decltype(auto) operator()(C&& c) noexcept {
            return (std::forward<C>(c));
        }

        template <typename Self, typename C>
            requires (sizeof...(K) > 0 && (!meta::range<typename decltype(K)::type> && ...))
        [[nodiscard]] constexpr decltype(auto) operator()(this Self&& self, C&& c)
            noexcept (
                requires{{eval<>::subscript(
                    std::forward<Self>(self),
                    meta::strip_range(std::forward<C>(c))
                )};} ?
                requires{{eval<>::subscript(
                    std::forward<Self>(self),
                    meta::strip_range(std::forward<C>(c))
                )} noexcept;} : (
                    requires{{eval<>::get(
                        std::forward<Self>(self),
                        meta::strip_range(std::forward<C>(c))
                    )};} ?
                    requires{{eval<>::get(
                        std::forward<Self>(self),
                        meta::strip_range(std::forward<C>(c))
                    )} noexcept;} : (
                        requires{{eval<>::offset(
                            std::forward<Self>(self),
                            meta::strip_range(std::forward<C>(c))
                        )};} ?
                        requires{{eval<>::offset(
                            std::forward<Self>(self),
                            meta::strip_range(std::forward<C>(c))
                        )} noexcept;} :
                        requires{{eval<>::invoke(
                            std::forward<Self>(self),
                            meta::from_range(std::forward<C>(c))
                        )} noexcept;}
                    )
                )
            )
            requires (
                requires{{eval<>::subscript(
                    std::forward<Self>(self),
                    meta::strip_range(std::forward<C>(c))
                )};} ||
                requires{{eval<>::get(
                    std::forward<Self>(self),
                    meta::strip_range(std::forward<C>(c))
                )};} ||
                requires{{eval<>::offset(
                    std::forward<Self>(self),
                    meta::strip_range(std::forward<C>(c))
                )};} ||
                requires{{eval<>::invoke(
                    std::forward<Self>(self),
                    meta::from_range(std::forward<C>(c))
                )};}
            )
        {
            if constexpr (requires{{eval<>::subscript(
                std::forward<Self>(self),
                meta::strip_range(std::forward<C>(c))
            )};}) {
                return (eval<>::subscript(
                    std::forward<Self>(self),
                    meta::strip_range(std::forward<C>(c))
                ));
            } else if constexpr (requires{{eval<>::get(
                std::forward<Self>(self),
                meta::strip_range(std::forward<C>(c))
            )};}) {
                return (eval<>::get(
                    std::forward<Self>(self),
                    meta::strip_range(std::forward<C>(c))
                ));
            } else if constexpr (requires{{eval<>::offset(
                std::forward<Self>(self),
                meta::strip_range(std::forward<C>(c))
            )};}) {
                return (eval<>::offset(
                    std::forward<Self>(self),
                    meta::strip_range(std::forward<C>(c))
                ));
            } else {
                return (eval<>::invoke(
                    std::forward<Self>(self),
                    meta::from_range(std::forward<C>(c))
                ));
            }
        }

        template <typename Self, typename C>
            requires (sizeof...(K) > 0 && (meta::range<typename decltype(K)::type> || ...))
        [[nodiscard]] constexpr auto operator()(this Self&& self, C&& c)
            noexcept (requires{{std::forward<Self>(self).zip(
                std::forward<C>(c),
                std::make_index_sequence<sizeof...(K)>()
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).zip(
                std::forward<C>(c),
                std::make_index_sequence<sizeof...(K)>()
            )};})
        {
            return std::forward<Self>(self).zip(
                std::forward<C>(c),
                std::make_index_sequence<sizeof...(K)>()
            );
        }
    };
    template <typename... K>
    at(K&&...) -> at<type<meta::remove_rvalue<K>>...>;

    /* A function object that reverses the order of iteration for a supported container.

    The ranges that are produced by this object act just like normal ranges, but with the
    forward and reverse iterators swapped, and the indexing logic modified to map index
    `i` to index `-i - 1` before applying Python-style wraparound.

    Note that the `reverse` class must be default-constructed, which standardizes it with
    respect to other range adaptors and allows it to be easily chained together with other
    operations to form more complex range-based algorithms. */
    struct reverse {
        template <typename T>
        [[nodiscard]] static constexpr auto operator()(T&& r)
            noexcept (requires{
                {range<impl::reverse<meta::as_range<T>>>{std::forward<T>(r)}} noexcept;
            })
            requires (meta::reverse_iterable<meta::as_lvalue<meta::as_range<T>>> && requires{
                {range<impl::reverse<meta::as_range<T>>>{std::forward<T>(r)}};
            })
        {
            return range<impl::reverse<meta::as_range<T>>>{std::forward<T>(r)};
        }
    };

    /* A function object that merges multiple ranges and/or scalar values into a single
    range, passing each element to a given transformation function, which is computed
    lazily.  If no transformation function is given, then the range defaults to
    returning a `Tuple` of the individual elements.

    This class unifies and replaces the following standard library views:

        1.  `std::views::zip` -> `zip{}(a...)`
        2.  `std::views::zip_transform` -> `zip{f}(a...)`
        3.  `std::views::transform` -> `zip{f}(r)`
        4.  `std::views::enumerate` -> `zip{f}(range(0, {}), r)`

    This class also serves as the basis for monadic operations on ranges, which differ
    only in the transformation function used to compute each element.  An expression
    such as `range(x) + 2` is therefore expression-equivalent to:

        ```
        zip{[](auto&& l, auto&& r) -> decltype(auto) {
            return (std::forward<decltype(l)>(l) + std::forward<decltype(r)>(r));
        }}(range(x), 2);
        ```

    Similar definitions exist for all overloadable operators, which act as simple
    elementwise transformations on the zipped range(s).  Special cases exist for
    `operator()`, `operator->*`, `operator[]`, which use simple adaptors to forward
    the intended behavior.

    For the range-based call `operator()`, the adaptor simply extends the argument list
    with the range on which the operator was invoked, and then forwards the remaining
    arguments to each element after broadcasting.  `range(x)(a...)` is therefore
    expression-equivalent to:

        ```
        zip{[](auto&& r, auto&&... a) -> decltype(auto) {
            return (std::forward<decltype(r)>(r)(
                std::forward<decltype(a)>(a)...
            ));
        }}(range(x), a...);
        ```

    The visitation `operator->*` uses an adaptor that attempts to destructure tuple
    elements into separate arguments, enabling simple pattern matching if the visitor
    is not directly callable with the tuple itself.  Additionally, because the `->*`
    operator attempts to recursively call itself if it cannot be resolved (similar to
    the `->` operator), the resulting comprehension will respect any nested ranges
    and preserve their shape as long as the visitor eventually becomes callable at
    some depth.  This recursion also allows nested pattern matching for ranges
    containing union types, in which case the visitor may customize behavior for each
    alternative separately.  See the general monad documentation for more details on
    this operator and its behavior.

    Lastly, the multidimensional indexing `operator[]` detects whether any of the
    arguments are ranges, and if so, zips them along with the range on which the
    operator was invoked.  It then recursively invokes `iter::at{}` using the
    broadcasted arguments until the indexing operation can be resolved.  This respects
    both the shape of the outer range as well as those of the index arguments, allowing
    numpy-style fancy indexing with all the other normalizations of that algorithm.
    See `iter::at{}` for more details on the indexing behavior. */
    template <meta::not_rvalue F = impl::zip_tuple>
    struct zip {
        [[no_unique_address]] F f;

        template <typename Self, typename... A>
        [[nodiscard]] constexpr auto operator()(this Self&& self, A&&... a)
            noexcept (requires{{range<impl::zip<
                meta::remove_rvalue<decltype((std::forward<Self>(self).f))>,
                meta::remove_rvalue<A>...
            >>{
                std::forward<Self>(self).f,
                std::forward<A>(a)...
            }} noexcept;})
            requires (
                impl::zip_concept<decltype((self.f)), A...> &&
                requires{{range<impl::zip<
                meta::remove_rvalue<decltype((std::forward<Self>(self).f))>,
                meta::remove_rvalue<A>...
            >>{
                std::forward<Self>(self).f,
                std::forward<A>(a)...
            }};}
            )
        {
            return range<impl::zip<
                meta::remove_rvalue<decltype((std::forward<Self>(self).f))>,
                meta::remove_rvalue<A>...
            >>{
                std::forward<Self>(self).f,
                std::forward<A>(a)...
            };
        }
    };
    template <typename F>
    zip(F&&) -> zip<meta::remove_rvalue<F>>;

    /* A wrapper for an arbitrary container that can be used to form iterable
    expressions.

    Ranges can be constructed in a variety of ways, effectively replacing each of the
    following with a simplified CTAD constructor (in order of preference):

        1.  `std::views::empty()` -> `range()`, producing an empty range with no
            elements.  By default, the deduced yield type will be equal to `None`.
        2.  `std::views::all(container)` -> `range(container)`, where `container` is
            any iterable or tuple-like type.  If the type is tuple-like and not
            iterable, then a vtable will be generated to provide random-access
            iteration over the tuple's elements, possibly involving dynamic dispatch
            and/or yielding `Union<Ts...>` if the elements are heterogeneous.
        3.  `std::views::single(value)` -> `range(value)`, where `value` is not
            iterable or tuple-like.  This produces a range of length 1 that yields
            a reference to `value` as the only element, and whose iterators devolve to
            simple pointers.
        4.  `std::views::iota(start)` -> `range(start, {})`, which represents an
            infinite range beginning at `start` and applying `++start` on each
            iteration.
        5.  `std::views::iota(start, stop)` -> `range(start, stop)`, where `start` and
            `stop` are arbitrary types for which `++start` and `start < stop` are
            well-formed.  `stop` may alternatively be a function predicate that takes
            the current value of `start` and returns a boolean indicating whether
            iteration should continue (`true`) or terminate (`false`), just like a
            traditional `for` or `while` loop condition.
        6.  `std::views::iota(start, stop) | std::views::stride(step)` ->
            `range(start, stop, step)`, which allows a nontrivial step size between
            each element, assuming `start += step` is well-formed.  If `step < 0` is
            valid, then `start > stop` must also be supported, in order to properly
            bound iteration with negative step sizes.  Similar to (4), `step` may
            alternatively be a function predicate that takes the current value of
            `start` and modifies it in-place to produce the next value in the range
            (which can be thought of as equivalent to the "update" step in a C-style
            `for` loop).
        7.  `std::ranges::subrange(start, stop)` -> `range(start, stop)`, where `start`
            is an iterator and `stop` is a matching sentinel or integer, in which case
            it behaves like `std::views::counted(start, stop)`.  The result has all the
            same characteristics as (5), except that `start == stop` is also a valid
            termination condition if the iterator and sentinel are not totally ordered.
            Infinite subranges are also permitted by default-constructing the `stop`
            index (e.g. `range(start, {})`).
        8.  `std::ranges::subrange(start, stop) | std::views::stride(step)` ->
            `range(start, stop, step)`, which behaves similarly to (6), except that
            `start == stop` is also a valid termination condition if the iterator and
            sentinel are not totally ordered, and `start += step` may be replaced by a
            loop of `++start` or `--start` calls (depending on the sign of `step`) if
            the iterator does not support random-access addition.  Negative step sizes
            are only allowed if the iterator supports bidirectional iteration.

    Once constructed, ranges provide member equivalents for most of the Customization
    Point Objects (CPOs) in the `std::ranges` namespace (plus a few others), including:

        a.  `begin()`, `end()`, `rbegin()`, `rend()`, and const equivalents, which
            allow all ranges to be iterated over, regardless of the capabilities of the
            underlying container.  The iterators of the underlying container will be
            reused where possible, and are therefore zero-cost.
        b.  `data()`, which directly forwards to the underlying container assuming it
            supports it.  Disabled otherwise.
        c.  `size()` and `ssize()`, which return the size as an unsigned or signed
            integer, respectively, assuming the container supports at least one or the
            other.
        d.  `empty()`, which is always supported.
        e.  `shape()`, which equates to a `meta::shape()` call on the range itself.
            This is always supported, and distinct from `size()` and `ssize()` in both
            return type (returning an array-like object) and complexity, with this
            method possibly equating to a `std::ranges::distance()` call in the first
            dimension.  See `meta::shape()` for more details.
        f.  `front()` and `back()`, which forward to the underlying container if
            possible, and may involve dereferencing the `begin()` or `rbegin()`
            iterators, respectively.  Both may throw `TypeError`s as a debug assertion
            if invoked on an empty range, in order to avoid undefined behavior.
        g.  `get<K...>()`, where `K...` can be any number of non-type template
            parameters equating to an `iter::at<K...>{}(self)` call internally.  See
            that algorithm for more details.
        h.  `operator[](k...)`, where `k...` can be any number of subscript indices,
            equating to an `iter::at{k...}(self)` call internally.  See
            that algorithm for more details.
        i.  `any(f)` and `all(f)`, which equate to `iter::any{f}(self)` and
            `iter::all{f}(self)` calls internally, respectively.  See those algorithms
            for more details.
        j.  `contains(k)`, which equates to an `iter::contains{k}(self)` call
            internally.  See that algorithm for more details.

    Additionally, ranges can be dereferenced via the `*` and/or `->` operators as if
    they were pointers, which allows indirect access to the underlying container.  Note
    that invoking any constructor other than (2) or (3) above will cause the internal
    empty, iota, or subrange type to be exposed on dereference, since there is no other
    container to access.

    Ranges also provide implicit conversions toward any type that satisfies the
    requirements of `std::ranges::to<T>(self)`, with small modifications to ensure
    consistency.  Explicit conversions (including contextual boolean conversions) are
    only allowed if the underlying container supports them, and no other implicit
    conversion could be found.

    Similarly, ranges provide custom assignment operators from both scalars (which will
    be broadcasted across the range) and other ranges (which lead to elementwise
    assignment), as well as braced initializer lists (which act as anonymous ranges).
    These effectively replace most uses of `std::ranges::fill()` and
    `std::ranges::copy()`, respectively.

    Lastly, all ranges are automatically exposed to Bertrand's monadic operator
    interface, which returns further ranges representing fused expression templates,
    which are lazily-evaluated upon indexing, iteration, conversion, or assignment, and
    reduce to single loops that can be aggressively optimized by the compiler.

    A number of range adaptors are provided in the `iter::` namespace, allowing easy
    composition of common range algorithms.  See each of those adaptors for more
    details. */
    template <impl::range_concept C>
    struct range {
        [[no_unique_address]] impl::ref<C> __value;

        [[nodiscard]] constexpr range() = default;
        [[nodiscard]] constexpr range(const range&) = default;
        [[nodiscard]] constexpr range(range&&) = default;

        template <typename... A> requires (sizeof...(A) > 0)
        [[nodiscard]] constexpr range(A&&... args)
            noexcept (requires{{impl::ref<C>{C(std::forward<A>(args)...)}} noexcept;})
            requires (requires{{impl::ref<C>{C(std::forward<A>(args)...)}};})
        :
            __value{C(std::forward<A>(args)...)}
        {}

        template <typename Start, typename Stop = impl::trivial, typename Step = impl::trivial>
        [[nodiscard]] constexpr explicit range(
            Start&& start,
            Stop&& stop,
            Step&& step = {}
        )
            requires (!meta::iterator<Start> && meta::specialization_of<C, impl::iota>)
        :
            __value{C(
                std::forward<Start>(start),
                std::forward<Stop>(stop),
                std::forward<Step>(step)
            )}
        {}

        template <typename Start, typename Stop = impl::trivial, typename Step = impl::trivial>
        [[nodiscard]] constexpr explicit range(
            Start&& start,
            Stop&& stop,
            Step&& step = {}
        )
            requires (meta::iterator<Start> && meta::specialization_of<C, impl::subrange>)
        :
            __value{C(
                std::forward<Start>(start),
                std::forward<Stop>(stop),
                std::forward<Step>(step)
            )}
        {}

        /* Produce a deep copy of the underlying container.  This is equivalent to
        invoking `meta::copy()` on the underlying container, which forwards to a deep
        `copy()` method if one is present, otherwise falling back to a normal copy
        constructor. */
        [[nodiscard]] constexpr auto copy() const
            noexcept (requires{{iter::range(meta::copy(*__value))} noexcept;})
            requires (requires{{iter::range(meta::copy(*__value))};})
        {
            return iter::range(meta::copy(*__value));
        }

        /* `swap()` operator between ranges. */
        constexpr void swap(range& other)
            noexcept (requires{{meta::swap(__value, other.__value)} noexcept;})
            requires (requires{{meta::swap(__value, other.__value)};})
        {
            meta::swap(__value, other.__value);
        }

        /* Perfectly forward the underlying container or scalar value. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (
                !meta::range_transparent<C> ||
                requires{{**std::forward<Self>(self).__value} noexcept;}
            )
            requires (
                !meta::range_transparent<C> ||
                requires{{**std::forward<Self>(self).__value};}
            )
        {
            if constexpr (meta::range_transparent<C>) {
                return (**std::forward<Self>(self).__value);
            } else {
                return (*std::forward<Self>(self).__value);
            }
        }

        /* Indirectly access a member of the underlying container or scalar value. */
        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self)
            noexcept (!meta::range_transparent<C> || requires{
                {*std::forward<Self>(self)} noexcept;
            })
            requires (!meta::range_transparent<C> || requires{
                {*std::forward<Self>(self)};
            })
        {
            return std::addressof(*std::forward<Self>(self));
        }

        /* Get a forward iterator to the start of the range. */
        template <typename Self>
        [[nodiscard]] constexpr auto begin(this Self&& self)
            noexcept (requires{{meta::begin(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::begin(*std::forward<Self>(self).__value)};})
        {
            return meta::begin(*std::forward<Self>(self).__value);
        }

        /* Get a forward iterator to one past the last element of the range. */
        template <typename Self>
        [[nodiscard]] constexpr auto end(this Self&& self)
            noexcept (requires{{meta::end(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::end(*std::forward<Self>(self).__value)};})
        {
            return meta::end(*std::forward<Self>(self).__value);
        }

        /* Get a reverse iterator to the last element of the range. */
        template <typename Self>
        [[nodiscard]] constexpr auto rbegin(this Self&& self)
            noexcept (requires{{meta::rbegin(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::rbegin(*std::forward<Self>(self).__value)};})
        {
            return meta::rbegin(*std::forward<Self>(self).__value);
        }

        /* Get a reverse iterator to one before the first element of the range. */
        template <typename Self>
        [[nodiscard]] constexpr auto rend(this Self&& self)
            noexcept (requires{{meta::rend(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::rend(*std::forward<Self>(self).__value)};})
        {
            return meta::rend(*std::forward<Self>(self).__value);
        }

        /* Get a pointer to the underlying data array, if one exists.  This is
        identical to a `meta::data()` call on the underlying value, assuming that
        expression is well-formed.  For scalars, this returns a pointer to the
        underlying value. */
        template <typename Self>
        [[nodiscard]] constexpr auto data(this Self&& self)
            noexcept (requires{{meta::data(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::data(*std::forward<Self>(self).__value)};})
        {
            return meta::data(*std::forward<Self>(self).__value);
        }

        /* Forwarding `shape()` operator for the underlying container, provided one can
        be deduced using the `meta::shape()` metafunction.  Note that sized iterables
        always produce a shape of at least one dimension.

        This method always returns an `impl::shape<rank>` object, where `rank` is
        equal to the number of dimensions in the container's shape, or `None` if they
        cannot be determined at compile time (therefore yielding a dynamic shape).  In
        both cases, the `shape` object behaves like a read-only
        `std::array<size_t, N>` or `std::vector<size_t>`, respectively. */
        [[nodiscard]] constexpr decltype(auto) shape() const
            noexcept (requires{{meta::shape(*__value)} noexcept;})
            requires (requires{{meta::shape(*__value)};})
        {
            return (meta::shape(*__value));
        }

        /* Forwarding `size()` operator for the underlying container, provided the
        container supports it.  This is identical to a `meta::size()` call on the
        underlying value.  Scalars always have a size of 1. */
        [[nodiscard]] constexpr decltype(auto) size() const
            noexcept (requires{{meta::size(*__value)} noexcept;})
            requires (requires{{meta::size(*__value)};})
        {
            return (meta::size(*__value));
        }

        /* Forwarding `ssize()` operator for the underlying container, provided the
        container supports it.  This is identical to a `meta::ssize()` call unless the
        underlying container exposes a `val.ssize()` member method or `ssize(val)` ADL
        method.  Scalars always have a size of 1. */
        [[nodiscard]] constexpr decltype(auto) ssize() const
            noexcept (requires{{meta::ssize(*__value)} noexcept;})
            requires (requires{{meta::ssize(*__value)};})
        {
            return (meta::ssize(*__value));
        }

        /* Forwarding `empty()` operator for the underlying container, provided the
        container supports it.  This is identical to a `meta::empty()` call on
        the underlying value.  It is always false for scalar ranges. */
        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{meta::empty(*__value)} noexcept;})
            requires (requires{{meta::empty(*__value)};})
        {
            return meta::empty(*__value);
        }

        /* Access the first element in the underlying container by searching for an
        appropriate `front()` member or ADL method, or dereferencing the `begin()`
        iterator.  The result will always be returned as a (possibly scalar) range,
        just like the indexing and tuple access operators.

        Note that no extra bounds checking is performed to guard against empty ranges,
        maintaining the zero-cost guarantee for the underlying container.  Individual
        containers may implement bounds checking in their `front()` method if desired,
        which is the case for all of Bertrand's core container types (via a debug
        assertion). */
        template <typename Self>
        [[nodiscard]] constexpr auto front(this Self&& self)
            noexcept (requires{{meta::front(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::front(*std::forward<Self>(self).__value)};})
        {
            return meta::front(*std::forward<Self>(self).__value);
        }

        /* Access the last element in the underlying container by searching for an
        appropriate `back()` member or ADL method, dereferencing the `rbegin()`
        iterator, advancing `begin()` to the last element via `it += ssize() - 1`, or
        decrementing the `end()` iterator, as appropriate.  The result will always be
        returned as a (possibly scalar) range, just like the indexing and tuple access
        operators.

        Note that no extra bounds checking is performed to guard against empty ranges,
        maintaining the zero-cost guarantee for the underlying container.  Individual
        containers may implement bounds checking in their `back()` method if desired,
        which is the case for all of Bertrand's core container types (via a debug
        assertion). */
        template <typename Self>
        [[nodiscard]] constexpr auto back(this Self&& self)
            noexcept (requires{{meta::back(*std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::back(*std::forward<Self>(self).__value)};})
        {
            return meta::back(*std::forward<Self>(self).__value);
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
            return iter::contains{k}(*this);
        }

        /* Range-based implicit conversion operator.  This invokes the following (in
        order of precedence):

            1.  A direct conversion from the underlying container, if possible.
            2.  A `to(std::from_range, std::forward<Self>(self))` constructor.
            3.  A `to(self.begin(), self.end())` constructor.
            4.  If both the range and target type are tuple-like and have the same
                size, a fold expression that constructs the target (via braced
                initialization) from the unpacked range elements.
            5.  If the target type is tuple-like but the range is not, an
                iterator-based fold expression that constructs the target (via braced
                initialization) from dereferenced iterator elements.  If the range size
                does not exactly match the target tuple size, then a `TypeError` may be
                thrown as a debug assertion.
            6.  If the target type is iterable, a recursive elementwise conversion that
                converts each element in the range to the target's value type, and then
                constructs the target using one of the previous methods.  This allows
                nested ranges to be converted into nested containers of arbitrary depth
                if none of the previous methods are applicable.

        This conversion replaces most uses of `std::ranges::to`, and allows ranges to
        be seamlessly converted to compatible container types.  The only difference is
        that this conversion does not allow 2-phase constructions that equate to
        default initialization followed by elementwise assignment/appends.  Instead, it
        provides the additional conversion methods described by (4) and (5) in order to
        replace these with braced initialization where possible.  2-phase conversions
        can still be performed manually via the range assignment operator if needed. */
        template <typename Self, typename to> requires (!meta::prefer_constructor<to>)
        [[nodiscard]] constexpr operator to(this Self&& self)
            noexcept (requires{
                {impl::range_convert::convert<to>(std::forward<Self>(self))} noexcept;
            })
            requires (
                !meta::is<to, bool> &&
                impl::range_convert::direct<Self, to> ||
                impl::range_convert::construct<Self, to> ||
                impl::range_convert::traverse<Self, to> ||
                impl::range_convert::unpack_tuple<Self, to> ||
                impl::range_convert::unpack_iter<Self, to> ||
                impl::range_convert::recursive<Self, to>
            )
        {
            return impl::range_convert::convert<to>(std::forward<Self>(self));
        }

        /* Range-based explicit conversion operator.  This is only enabled if none of
        the implicit conversion methods would be valid, and only permits direct
        conversions from the underlying container via `static_cast<to>(...)`.  Note
        that this specifically excludes contextual conversions to `bool`, as that would
        interfere with the monadic logical operators and often leads to subtle bugs in
        user code. */
        template <typename Self, typename to> requires (!meta::prefer_constructor<to>)
        [[nodiscard]] explicit constexpr operator to(this Self&& self)
            noexcept (requires{{static_cast<to>(*std::forward<Self>(self))} noexcept;})
            requires (
                !meta::is<to, bool> &&
                !impl::range_convert::direct<Self, to> &&
                !impl::range_convert::construct<Self, to> &&
                !impl::range_convert::traverse<Self, to> &&
                !impl::range_convert::unpack_tuple<Self, to> &&
                !impl::range_convert::unpack_iter<Self, to> &&
                requires{{static_cast<to>(*std::forward<Self>(self))};}
            )
        {
            return static_cast<to>(*std::forward<Self>(self));
        }

        constexpr range& operator=(const range&) = default;
        constexpr range& operator=(range&&) = default;

        /* Range-based assignment operator.  This invokes the following (in order of
        precedence):

            1.  A direct assignment to the underlying container, if possible.
            2.  An elementwise assignment to a scalar value, assigning the value
                across the entire range using `begin()` as an output iterator.
            3.  If the other operand is a range, an elementwise assignment by iterating
                over both ranges using `begin()` as an output iterator.  If the ranges
                are not the same size, then a `TypeError` may be thrown as a debug
                assertion.  If both ranges are tuple-like, then the size check will be
                performed at compile-time instead, causing a compilation error if it
                fails.
            4.  If the other operand is a range and overload (3) is not available, then
                check for tuple-based elementwise assignment using a fold expression
                over one or both operands.  Similarly to (3), a `TypeError` may be
                thrown as a debug assertion if the ranges are not the same size, which
                may be promoted to a compilation error if both are tuple-like.

        This operator replaces most uses of `std::ranges::copy` and `std::ranges::fill`
        depending on whether the other operand is a range or scalar value,
        respectively.  Note that invoking this operator can be done on a single line,
        but requires braced initialization for the left-hand side:

            ```
            std::array arr {0, 0, 0};
            iter::range{arr} = {1, 2, 3};
            assert(arr[0] == 1);
            assert(arr[1] == 2);
            assert(arr[2] == 3);
            ```

        Replacing `iter::range{arr}` with `iter::range(arr)` causes a compilation
        error due to the "most vexing parse" problem. */
        template <typename T>
        constexpr range& operator=(T&& rhs)
            noexcept (requires{
                {impl::range_assign::assign(*this, std::forward<T>(rhs))} noexcept;
            })
            requires (
                impl::range_assign::direct<range, T> ||
                impl::range_assign::scalar<range, T> ||
                impl::range_assign::iter_from_iter<range, T> ||
                impl::range_assign::tuple_from_tuple<range, T> ||
                impl::range_assign::iter_from_tuple<range, T> ||
                impl::range_assign::tuple_from_iter<range, T>
            )
        {
            impl::range_assign::assign(*this, std::forward<T>(rhs));
            return *this;
        }

        /* A special case of range-based assignment operator where the other operand is
        a simple, braced initializer list.  This behaves identically to assignment from
        an equivalent range, but allows the container type and `range{}` constructor to
        be omitted for readability purposes. */
        template <typename T>
        constexpr range& operator=(std::initializer_list<T> il)
            noexcept (requires{{impl::range_assign::assign(
                *this,
                range<std::initializer_list<T>>(std::move(il))
            )} noexcept;})
            requires (
                impl::range_assign::direct<range, range<std::initializer_list<T>>> ||
                impl::range_assign::iter_from_iter<range, range<std::initializer_list<T>>> ||
                impl::range_assign::tuple_from_iter<range, range<std::initializer_list<T>>>
            )
        {
            impl::range_assign::assign(*this, range<std::initializer_list<T>>(std::move(il)));
            return *this;
        }

        template <typename Self, typename... A>
        constexpr auto operator()(this Self&& self, A&&... a)
            noexcept (requires{{iter::range<impl::zip<
                impl::zip_call,
                meta::remove_rvalue<Self>,
                meta::remove_rvalue<A>...
            >>{
                impl::zip_call{},
                std::forward<Self>(self),
                std::forward<A>(a)...
            }} noexcept;})
            requires (
                meta::callable<meta::yield_type<Self>, impl::zip_arg<A>...> &&
                meta::not_void<meta::call_type<meta::yield_type<Self>, impl::zip_arg<A>...>>
            )
        {
            return iter::range<impl::zip<
                impl::zip_call,
                meta::remove_rvalue<Self>,
                meta::remove_rvalue<A>...
            >>{
                impl::zip_call{},
                std::forward<Self>(self),
                std::forward<A>(a)...
            };
        }
    };
    template <>
    struct range<impl::empty_range<>> {
        [[no_unique_address]] impl::ref<impl::empty_range<>> __value;

        [[nodiscard]] constexpr range() = default;
        [[nodiscard]] constexpr range(const range&) = default;
        [[nodiscard]] constexpr range(range&&) = default;

        [[nodiscard]] constexpr range copy() const noexcept { return *this; }
        constexpr void swap(range& other) noexcept {}

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept {
            return (*std::forward<Self>(self).__value);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto operator->(this Self&& self) noexcept {
            return std::addressof(*std::forward<Self>(self));
        }

        template <typename Self>
        [[nodiscard]] constexpr auto begin(this Self&& self) noexcept {
            return (*std::forward<Self>(self)).begin();
        }

        template <typename Self>
        [[nodiscard]] constexpr auto end(this Self&& self) noexcept {
            return (*std::forward<Self>(self)).end();
        }

        template <typename Self>
        [[nodiscard]] constexpr auto rbegin(this Self&& self) noexcept {
            return (*std::forward<Self>(self)).rbegin();
        }

        template <typename Self>
        [[nodiscard]] constexpr auto rend(this Self&& self) noexcept {
            return (*std::forward<Self>(self)).rend();
        }

        [[nodiscard]] constexpr impl::shape<0> shape() const noexcept { return {}; }
        [[nodiscard]] constexpr size_t size() const noexcept { return 0; }
        [[nodiscard]] constexpr ssize_t ssize() const noexcept { return 0; }
        [[nodiscard]] constexpr bool empty() const noexcept { return true; }

        template <typename T>
        [[nodiscard]] constexpr bool contains(const T& k) const noexcept {
            return false;
        }

        template <typename to> requires (!meta::prefer_constructor<to>)
        [[nodiscard]] constexpr operator to() const
            noexcept (meta::nothrow::default_constructible<to>)
            requires (
                meta::iterable<to> &&
                meta::default_constructible<to>
            )
        {
            return {};
        }

        /* A call operator allows the default `range{}` object to be ued as a range
        algorithm that converts the first non-range yield type into a range,
        effectively increasing the argument's shape by one dimension.  This allows
        the following syntax:

            ```
            iter::range(r) ->* iter::range{};
            ```

        Which is expression-equivalent to:

            ```
            iter::range(r) ->* [](auto&& val) requires (!meta::range<decltype(val)>) {
                return iter::range(std::forward<decltype(val)>(val));
            };
            ```
        */
        template <meta::range T>
        [[nodiscard]] static constexpr auto operator()(T&& val)
            noexcept (requires{{std::forward<T>(val) ->* impl::zip_nest{}} noexcept;})
            requires (requires{{std::forward<T>(val) ->* impl::zip_nest{}};})
        {
            return std::forward<T>(val) ->* impl::zip_nest{};
        }
    };

    /* Broadcasting operator for range monads.  A visitor function must be provided on
    the right hand side of this operator, which must either be a range algorithm for
    which `meta::range_algorithm` evaluates true and which is directly callable with
    the left-hand range, or another function object that is callable with the range's
    yield type.  If the range yields tuples and the function is not directly callable
    with the tuple, then this operator will attempt to destructure it and invoke the
    function with each element as a separate argument.

    Similar to the built-in `->` indirection operator, this operator will attempt to
    recursively call itself in order to match nested patterns involving other monads.
    Namely, if neither of the above conditions are met, but the continuation
    `->* visitor` operation is valid for the range's yield type, then that operation
    will be invoked instead, possibly creating a nested comprehension that preserves
    the original's shape. */
    template <meta::range L, typename R>
    [[nodiscard]] constexpr decltype(auto) operator->*(L&& l, R&& r)
        noexcept ((meta::range_algorithm<R> && meta::callable<R, L>) ?
            requires{{std::forward<R>(r)(std::forward<L>(l))} noexcept;} : (
                impl::zip_visitable<L, R> ?
                    requires{{iter::range<impl::zip<
                        impl::zip_visit<meta::remove_rvalue<R>>,
                        meta::remove_rvalue<L>
                    >>{
                        impl::zip_visit{std::forward<R>(r)},
                        std::forward<L>(l)
                    }} noexcept;} :
                    requires{{iter::range<impl::zip<
                        impl::ArrowDereference,
                        meta::remove_rvalue<L>,
                        meta::remove_rvalue<R>
                    >>{
                        impl::ArrowDereference{},
                        std::forward<L>(l),
                        std::forward<R>(r)
                    }} noexcept;}
            )
        )
        requires ((meta::range_algorithm<R> && meta::callable<R, L>) || (
            !meta::range<R> && (
                impl::zip_visitable<L, R> ||
                requires{{iter::range<impl::zip<
                    impl::ArrowDereference,
                    meta::remove_rvalue<L>,
                    meta::remove_rvalue<R>
                >>{
                    impl::ArrowDereference{},
                    std::forward<L>(l),
                    std::forward<R>(r)
                }};}
            )
        ))
    {
        if constexpr (meta::range_algorithm<R> && meta::callable<R, L>) {
            return (std::forward<R>(r)(std::forward<L>(l)));
        } else if constexpr (impl::zip_visitable<L, R>) {
            return (iter::range<impl::zip<
                impl::zip_visit<meta::remove_rvalue<R>>,
                meta::remove_rvalue<L>
            >>{
                impl::zip_visit{std::forward<R>(r)},
                std::forward<L>(l)
            });
        } else {
            return (iter::range<impl::zip<
                impl::ArrowDereference,
                meta::remove_rvalue<L>,
                meta::remove_rvalue<R>
            >>{
                impl::ArrowDereference{},
                std::forward<L>(l),
                std::forward<R>(r)
            });
        }
    }

    template <meta::range T>
    [[nodiscard]] constexpr auto operator!(T&& r)
        noexcept (requires{{iter::range<impl::zip<
            impl::LogicalNot,
            meta::remove_rvalue<T>
        >>{
            impl::LogicalNot{},
            std::forward<T>(r)
        }} noexcept;})
        requires (
            meta::has_logical_not<meta::yield_type<T>> &&
            meta::not_void<meta::logical_not_type<meta::yield_type<T>>>
        )
    {
        return iter::range<impl::zip<
            impl::LogicalNot,
            meta::remove_rvalue<T>
        >>{
            impl::LogicalNot{},
            std::forward<T>(r)
        };
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator&&(L&& l, R&& r)
        noexcept (requires{{iter::range<impl::zip<
            impl::LogicalAnd,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::LogicalAnd{},
            std::forward<L>(l),
            std::forward<R>(r)
        }} noexcept;})
        requires (
            meta::has_logical_and<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::logical_and_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::LogicalAnd,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::LogicalAnd{},
            std::forward<L>(l),
            std::forward<R>(r)
        };
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator||(L&& l, R&& r)
        noexcept (requires{{iter::range<impl::zip<
            impl::LogicalOr,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::LogicalOr{},
            std::forward<L>(l),
            std::forward<R>(r)
        }} noexcept;})
        requires (
            meta::has_logical_or<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::logical_or_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::LogicalOr,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::LogicalOr{},
            std::forward<L>(l),
            std::forward<R>(r)
        };
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator<(L&& l, R&& r)
        noexcept (requires{{iter::range<impl::zip<
            impl::Less,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Less{},
            std::forward<L>(l),
            std::forward<R>(r)
        }} noexcept;})
        requires (
            meta::has_lt<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::lt_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::Less,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Less{},
            std::forward<L>(l),
            std::forward<R>(r)
        };
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator<=(L&& l, R&& r)
        noexcept (requires{{iter::range<impl::zip<
            impl::LessEqual,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::LessEqual{},
            std::forward<L>(l),
            std::forward<R>(r)
        }} noexcept;})
        requires (
            meta::has_le<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::le_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::LessEqual,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::LessEqual{},
            std::forward<L>(l),
            std::forward<R>(r)
        };
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator==(L&& l, R&& r)
        noexcept (requires{{iter::range<impl::zip<
            impl::Equal,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Equal{},
            std::forward<L>(l),
            std::forward<R>(r)
        }} noexcept;})
        requires (
            meta::has_eq<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::eq_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::Equal,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Equal{},
            std::forward<L>(l),
            std::forward<R>(r)
        };
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator!=(L&& l, R&& r)
        noexcept (requires{{iter::range<impl::zip<
            impl::NotEqual,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::NotEqual{},
            std::forward<L>(l),
            std::forward<R>(r)
        }} noexcept;})
        requires (
            meta::has_ne<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::ne_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::NotEqual,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::NotEqual{},
            std::forward<L>(l),
            std::forward<R>(r)
        };
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator>=(L&& l, R&& r)
        noexcept (requires{{iter::range<impl::zip<
            impl::GreaterEqual,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::GreaterEqual{},
            std::forward<L>(l),
            std::forward<R>(r)
        }} noexcept;})
        requires (
            meta::has_ge<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::ge_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::GreaterEqual,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::GreaterEqual{},
            std::forward<L>(l),
            std::forward<R>(r)
        };
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator>(L&& l, R&& r)
        noexcept (requires{{iter::range<impl::zip<
            impl::Greater,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Greater{},
            std::forward<L>(l),
            std::forward<R>(r)
        }} noexcept;})
        requires (
            meta::has_gt<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::gt_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::Greater,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Greater{},
            std::forward<L>(l),
            std::forward<R>(r)
        };
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator<=>(L&& l, R&& r)
        noexcept (requires{{iter::range<impl::zip<
            impl::Spaceship,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Spaceship{},
            std::forward<L>(l),
            std::forward<R>(r)
        }} noexcept;})
        requires (
            meta::has_spaceship<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::spaceship_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::Spaceship,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Spaceship{},
            std::forward<L>(l),
            std::forward<R>(r)
        };
    }

    template <meta::range T>
    [[nodiscard]] constexpr auto operator+(T&& r)
        noexcept (requires{{iter::range<impl::zip<
            impl::Pos,
            meta::remove_rvalue<T>
        >>{
            impl::Pos{},
            std::forward<T>(r)
        }} noexcept;})
        requires (
            meta::has_pos<meta::yield_type<T>> &&
            meta::not_void<meta::pos_type<meta::yield_type<T>>>
        )
    {
        return iter::range<impl::zip<
            impl::Pos,
            meta::remove_rvalue<T>
        >>{
            impl::Pos{},
            std::forward<T>(r)
        };
    }

    template <meta::range T>
    [[nodiscard]] constexpr auto operator-(T&& r)
        noexcept (requires{{iter::range<impl::zip<
            impl::Neg,
            meta::remove_rvalue<T>
        >>{
            impl::Neg{},
            std::forward<T>(r)
        }} noexcept;})
        requires (
            meta::has_neg<meta::yield_type<T>> &&
            meta::not_void<meta::neg_type<meta::yield_type<T>>>
        )
    {
        return iter::range<impl::zip<
            impl::Neg,
            meta::remove_rvalue<T>
        >>{
            impl::Neg{},
            std::forward<T>(r)
        };
    }

    template <meta::range T>
    constexpr decltype(auto) operator++(T&& r)
        noexcept (requires{{exhaust{}(iter::range<impl::zip<
            impl::PreIncrement,
            meta::remove_rvalue<T>
        >>{
            impl::PreIncrement{},
            std::forward<T>(r)
        })} noexcept;})
        requires (
            meta::has_preincrement<meta::yield_type<T>>
        )
    {
        exhaust{}(iter::range<impl::zip<
            impl::PreIncrement,
            meta::remove_rvalue<T>
        >>{
            impl::PreIncrement{},
            std::forward<T>(r)
        });
        return (std::forward<T>(r));
    }

    template <meta::range T>
    [[nodiscard]] constexpr auto operator++(T&& r, int)
        noexcept (requires{
            {r.copy()} noexcept;
            {++std::forward<T>(r)} noexcept;
        })
        requires (
            requires{{r.copy()};} &&
            meta::has_preincrement<meta::yield_type<T>>
        )
    {
        auto copy = r.copy();
        ++std::forward<T>(r);
        return copy;
    }

    template <meta::range T>
    constexpr decltype(auto) operator--(T&& r)
        noexcept (requires{{exhaust{}(iter::range<impl::zip<
            impl::PreDecrement,
            meta::remove_rvalue<T>
        >>{
            impl::PreDecrement{},
            std::forward<T>(r)
        })} noexcept;})
        requires (
            meta::has_predecrement<meta::yield_type<T>>
        )
    {
        exhaust{}(iter::range<impl::zip<
            impl::PreDecrement,
            meta::remove_rvalue<T>
        >>{
            impl::PreDecrement{},
            std::forward<T>(r)
        });
        return (std::forward<T>(r));
    }

    template <meta::range T>
    [[nodiscard]] constexpr auto operator--(T&& r, int)
        noexcept (requires{
            {r.copy()} noexcept;
            {--std::forward<T>(r)} noexcept;
        })
        requires (
            requires{{r.copy()};} &&
            meta::has_predecrement<meta::yield_type<T>>
        )
    {
        auto copy = r.copy();
        --std::forward<T>(r);
        return copy;
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator+(L&& lhs, R&& rhs)
        noexcept (requires{{iter::range<impl::zip<
            impl::Add,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Add{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (
            meta::has_add<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::add_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::Add,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Add{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::range L, typename R>
    constexpr decltype(auto) operator+=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust{}(iter::range<impl::zip<
            impl::InplaceAdd,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceAdd{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (
            meta::has_iadd<meta::yield_type<L>, impl::zip_arg<R>>
        )
    {
        exhaust{}(iter::range<impl::zip<
            impl::InplaceAdd,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceAdd{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
        return (std::forward<L>(lhs));
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator-(L&& lhs, R&& rhs)
        noexcept (requires{{iter::range<impl::zip<
            impl::Subtract,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Subtract{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (
            meta::has_sub<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::sub_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::Subtract,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Subtract{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::range L, typename R>
    constexpr decltype(auto) operator-=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust{}(iter::range<impl::zip<
            impl::InplaceSubtract,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceSubtract{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (
            meta::has_isub<meta::yield_type<L>, impl::zip_arg<R>>
        )
    {
        exhaust{}(iter::range<impl::zip<
            impl::InplaceSubtract,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceSubtract{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
        return (std::forward<L>(lhs));
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator*(L&& lhs, R&& rhs)
        noexcept (requires{{iter::range<impl::zip<
            impl::Multiply,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Multiply{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (
            meta::has_mul<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::mul_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::Multiply,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Multiply{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::range L, typename R>
    constexpr decltype(auto) operator*=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust{}(iter::range<impl::zip<
            impl::InplaceMultiply,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceMultiply{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (
            meta::has_imul<meta::yield_type<L>, impl::zip_arg<R>>
        )
    {
        exhaust{}(iter::range<impl::zip<
            impl::InplaceMultiply,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceMultiply{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
        return (std::forward<L>(lhs));
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator/(L&& lhs, R&& rhs)
        noexcept (requires{{iter::range<impl::zip<
            impl::Divide,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Divide{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (
            meta::has_div<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::div_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::Divide,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Divide{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::range L, typename R>
    constexpr decltype(auto) operator/=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust{}(iter::range<impl::zip<
            impl::InplaceDivide,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceDivide{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (
            meta::has_idiv<meta::yield_type<L>, impl::zip_arg<R>>
        )
    {
        exhaust{}(iter::range<impl::zip<
            impl::InplaceDivide,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceDivide{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
        return (std::forward<L>(lhs));
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator%(L&& lhs, R&& rhs)
        noexcept (requires{{iter::range<impl::zip<
            impl::Modulus,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Modulus{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (
            meta::has_mod<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::mod_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::Modulus,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::Modulus{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::range L, typename R>
    constexpr decltype(auto) operator%=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust{}(iter::range<impl::zip<
            impl::InplaceModulus,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceModulus{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (
            meta::has_imod<meta::yield_type<L>, impl::zip_arg<R>>
        )
    {
        exhaust{}(iter::range<impl::zip<
            impl::InplaceModulus,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceModulus{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
        return (std::forward<L>(lhs));
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator<<(L&& lhs, R&& rhs)
        noexcept (requires{{iter::range<impl::zip<
            impl::LeftShift,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::LeftShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (
            meta::has_lshift<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::lshift_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::LeftShift,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::LeftShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::range L, typename R>
    constexpr decltype(auto) operator<<=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust{}(iter::range<impl::zip<
            impl::InplaceLeftShift,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceLeftShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (
            meta::has_ilshift<meta::yield_type<L>, impl::zip_arg<R>>
        )
    {
        exhaust{}(iter::range<impl::zip<
            impl::InplaceLeftShift,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceLeftShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
        return (std::forward<L>(lhs));
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator>>(L&& lhs, R&& rhs)
        noexcept (requires{{iter::range<impl::zip<
            impl::RightShift,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::RightShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        }} noexcept;})
        requires (
            meta::has_rshift<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::rshift_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::RightShift,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::RightShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        };
    }

    template <meta::range L, typename R>
    constexpr decltype(auto) operator>>=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust{}(iter::range<impl::zip<
            impl::InplaceRightShift,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceRightShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (
            meta::has_irshift<meta::yield_type<L>, impl::zip_arg<R>>
        )
    {
        exhaust{}(iter::range<impl::zip<
            impl::InplaceRightShift,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceRightShift{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
        return (std::forward<L>(lhs));
    }

    template <meta::range T>
    [[nodiscard]] constexpr auto operator~(T&& r)
        noexcept (requires{{iter::range<impl::zip<
            impl::BitwiseNot,
            meta::remove_rvalue<T>
        >>{
            impl::BitwiseNot{},
            std::forward<T>(r)
        }} noexcept;})
        requires (
            meta::has_bitwise_not<meta::yield_type<T>> &&
            meta::not_void<meta::bitwise_not_type<meta::yield_type<T>>>
        )
    {
        return iter::range<impl::zip<
            impl::BitwiseNot,
            meta::remove_rvalue<T>
        >>{
            impl::BitwiseNot{},
            std::forward<T>(r)
        };
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator&(L&& l, R&& r)
        noexcept (requires{{iter::range<impl::zip<
            impl::BitwiseAnd,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::BitwiseAnd{},
            std::forward<L>(l),
            std::forward<R>(r)
        }} noexcept;})
        requires (
            meta::has_and<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::and_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::BitwiseAnd,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::BitwiseAnd{},
            std::forward<L>(l),
            std::forward<R>(r)
        };
    }

    template <meta::range L, typename R>
    constexpr decltype(auto) operator&=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust{}(iter::range<impl::zip<
            impl::InplaceBitwiseAnd,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceBitwiseAnd{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (
            meta::has_iand<meta::yield_type<L>, impl::zip_arg<R>>
        )
    {
        exhaust{}(iter::range<impl::zip<
            impl::InplaceBitwiseAnd,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceBitwiseAnd{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
        return (std::forward<L>(lhs));
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator|(L&& l, R&& r)
        noexcept (requires{{iter::range<impl::zip<
            impl::BitwiseOr,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::BitwiseOr{},
            std::forward<L>(l),
            std::forward<R>(r)
        }} noexcept;})
        requires (
            meta::has_or<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::or_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::BitwiseOr,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::BitwiseOr{},
            std::forward<L>(l),
            std::forward<R>(r)
        };
    }

    template <meta::range L, typename R>
    constexpr decltype(auto) operator|=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust{}(iter::range<impl::zip<
            impl::InplaceBitwiseOr,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceBitwiseOr{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (
            meta::has_ior<meta::yield_type<L>, impl::zip_arg<R>>
        )
    {
        exhaust{}(iter::range<impl::zip<
            impl::InplaceBitwiseOr,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceBitwiseOr{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
        return (std::forward<L>(lhs));
    }

    template <typename L, typename R> requires (meta::range<L> || meta::range<R>)
    [[nodiscard]] constexpr auto operator^(L&& l, R&& r)
        noexcept (requires{{iter::range<impl::zip<
            impl::BitwiseXor,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::BitwiseXor{},
            std::forward<L>(l),
            std::forward<R>(r)
        }} noexcept;})
        requires (
            meta::has_xor<impl::zip_arg<L>, impl::zip_arg<R>> &&
            meta::not_void<meta::xor_type<impl::zip_arg<L>, impl::zip_arg<R>>>
        )
    {
        return iter::range<impl::zip<
            impl::BitwiseXor,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::BitwiseXor{},
            std::forward<L>(l),
            std::forward<R>(r)
        };
    }

    template <meta::range L, typename R>
    constexpr decltype(auto) operator^=(L&& lhs, R&& rhs)
        noexcept (requires{{exhaust{}(iter::range<impl::zip<
            impl::InplaceBitwiseXor,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceBitwiseXor{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        })} noexcept;})
        requires (
            meta::has_ixor<meta::yield_type<L>, impl::zip_arg<R>>
        )
    {
        exhaust{}(iter::range<impl::zip<
            impl::InplaceBitwiseXor,
            meta::remove_rvalue<L>,
            meta::remove_rvalue<R>
        >>{
            impl::InplaceBitwiseXor{},
            std::forward<L>(lhs),
            std::forward<R>(rhs)
        });
        return (std::forward<L>(lhs));
    }

}


namespace impl {

    template <meta::not_rvalue T>
    struct zip_subscript {
        [[no_unique_address]] impl::ref<T> container;

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{
                {iter::at{std::forward<A>(args)...}(*std::forward<Self>(self).container)} noexcept;
            })
            requires (requires{
                {iter::at{std::forward<A>(args)...}(*std::forward<Self>(self).container)};
            })
        {
            return (iter::at{std::forward<A>(args)...}(*std::forward<Self>(self).container));
        }
    };

}


namespace meta {

    namespace detail {

        template <typename F>
        constexpr bool range_algorithm<iter::all<F>> = true;
        template <typename F>
        constexpr bool range_algorithm<iter::any<F>> = true;
        template <typename F>
        constexpr bool range_algorithm<iter::compare<F>> = true;
        template <typename T>
        constexpr bool range_algorithm<iter::contains<T>> = true;
        template <typename T>
        constexpr bool range_algorithm<iter::count<T>> = true;
        template <>
        inline constexpr bool range_algorithm<iter::exhaust> = true;
        template <typename F>
        constexpr bool range_algorithm<iter::max<F>> = true;
        template <typename F>
        constexpr bool range_algorithm<iter::min<F>> = true;
        template <typename F>
        constexpr bool range_algorithm<iter::minmax<F>> = true;
        template <auto... K>
        constexpr bool range_algorithm<iter::at<K...>> = true;
        template <>
        inline constexpr bool range_algorithm<iter::reverse> = true;
        template <typename F>
        constexpr bool range_algorithm<iter::zip<F>> = true;
        template <>
        inline constexpr bool range_algorithm<iter::range<>> = true;

    }

}


}  // namespace bertrand


_LIBCPP_BEGIN_NAMESPACE_STD

    namespace ranges {

        template <typename C>
        constexpr bool enable_borrowed_range<bertrand::iter::range<C>> = borrowed_range<C>;

        template <typename T>
        constexpr bool enable_borrowed_range<bertrand::impl::reverse<T>> = borrowed_range<T>;

        template <typename T>
        constexpr bool enable_borrowed_range<bertrand::impl::empty_range<T>> = true;

        template <typename T>
        constexpr bool enable_borrowed_range<bertrand::impl::scalar<T>> =
            enable_borrowed_range<bertrand::Optional<T>>;

    }

    template <bertrand::meta::tuple_like T>
    struct tuple_size<bertrand::iter::range<T>> : tuple_size<bertrand::meta::unqualify<T>> {};

    template <bertrand::meta::tuple_like T>
    struct tuple_size<bertrand::impl::reverse<T>> : tuple_size<bertrand::meta::unqualify<T>> {};

    template <typename T>
    struct tuple_size<bertrand::impl::empty_range<T>> : std::integral_constant<size_t, 0> {};

    template <typename T>
    struct tuple_size<bertrand::impl::scalar<T>> : std::integral_constant<size_t, 1> {};

    template <typename T>
    struct tuple_size<bertrand::impl::tuple_range<T>> : tuple_size<bertrand::meta::unqualify<T>> {};

    template <typename F, typename... A>
        requires (!(bertrand::meta::range<A> || bertrand::meta::tuple_like<A>) && ...)
    struct tuple_size<bertrand::impl::zip<F, A...>> : std::integral_constant<
        size_t,
        bertrand::impl::zip_tuple_size<A...>
    > {};

    template <size_t I, bertrand::meta::tuple_like T>
        requires (requires{typename tuple_element<I, bertrand::meta::remove_reference<T>>;})
    struct tuple_element<I, bertrand::iter::range<T>> : tuple_element<
        I,
        bertrand::meta::remove_reference<T>
    > {};

    template <size_t I, bertrand::meta::tuple_like T> requires (I < bertrand::meta::tuple_size<T>)
    struct tuple_element<I, bertrand::impl::reverse<T>> {
        using type = bertrand::meta::remove_rvalue<
            decltype((std::declval<bertrand::impl::reverse<T>>().template get<I>()))
        >;
    };

    template <size_t I, typename T>
    struct tuple_element<I, bertrand::impl::empty_range<T>> {
        using type = bertrand::NoneType;
    };

    template <size_t I, typename T> requires (I == 0)
    struct tuple_element<I, bertrand::impl::scalar<T>> {
        using type = T;
    };

    template <size_t I, typename T>
        requires (requires{typename tuple_element<I, bertrand::meta::remove_reference<T>>;})
    struct tuple_element<I, bertrand::impl::tuple_range<T>> : tuple_element<
        I,
        bertrand::meta::remove_reference<T>
    > {};

    template <size_t I, typename F, typename... A>
        requires (
            (!(bertrand::meta::range<A> || bertrand::meta::tuple_like<A>) && ...) &&
            I < bertrand::impl::zip_tuple_size<A...>
        )
    struct tuple_element<I, bertrand::impl::zip<F, A...>> {
        using type = bertrand::meta::remove_rvalue<
            decltype((std::declval<bertrand::impl::zip<F, A...>>().template get<I>()))
        >;
    };

_LIBCPP_END_NAMESPACE_STD


namespace bertrand::iter {

    static_assert(contains{range{std::array{0, 1}}}(
        std::tuple{0, 1, 2.5}
    ));
    static_assert(contains{[](int x) { return x > 1; }}(
        std::array{0, 1, 2}
    ));


    static_assert(count{}() == 0);
    static_assert(count{}(std::array{1, 2, 3}) == 3);
    static_assert(count{}(std::array{1, 2, 3}, std::array{4, 5, 6}) == 6);
    static_assert(count{[](int x) { return x > 1; }}(
        std::tuple{1, 2, 3}
    ) == 2);
    static_assert(count{[](int x) { return x > 1; }}(
        std::tuple{1, 2, 3},
        std::array{1, 2, 3}
    ) == 4);



    static_assert([] {
        std::array arr {1, 2, 3};

        // range r(1, 4);
        range r(arr.begin(), arr.end());
        auto it = std::move(r).begin();
        auto&& x = *it;

        return true;
    }());


    static constexpr auto m1 = min{}(range(std::array{1, 2, 3}), 4, 5, 0);
    static_assert(m1 == 0);

    static constexpr decltype(auto) m2 = max{}(range(std::array{1, 2, 3}));
    static_assert(m2 == 3);

    static constexpr auto m3 = minmax{}(range(std::array{1, 2, 3}), 4, 5, 2);
    static_assert(m3.min == 1 && m3.max == 5);


    static_assert([] {
        range r(std::vector{10, 20, 30});
        auto r2 = r.copy();
        r[0] = 100;
        if (r[0] != 100) return false;
        if (r2[0] != 10) return false;

        return true;
    }());

    static_assert([] {
        range r = std::array{1, 2, 3};
        auto z = r + 1;
        auto it = z.begin();
        if (*it++ != 2) return false;
        if (*it++ != 3) return false;
        if (*it++ != 4) return false;
        if (it != z.end()) return false;

        auto z2 = r ->* [](int x) { return x - 1; };
        auto it2 = z2.begin();
        if (*it2++ != 0) return false;
        if (*it2++ != 1) return false;
        if (*it2++ != 2) return false;
        if (it2 != z2.end()) return false;

        if (((r + 1) ->* max{}) != 4) return false;
        if (((r + range(std::array{2, 1, 0})) != 3) ->* any()) return false;

        return true;
    }());

    static_assert([] {
        range r = std::array{Optional<int>{1}, Optional<int>{2}, Optional<int>{None}};
        auto r2 = r ->* [](int x) { return x * 10; };
        auto it = r2.begin();
        if (**it++ != 10) return false;
        if (**it++ != 20) return false;
        if (*it++ != None) return false;
        if (it != r2.end()) return false;

        auto rit = r2.rbegin();
        if (*rit++ != None) return false;
        if (**rit++ != 20) return false;
        if (**rit++ != 10) return false;
        if (rit != r2.rend()) return false;
        return true;
    }());

    static_assert([] {
        Optional<range<std::array<int, 3>>> orng = std::array{1, 2, 3};
        auto orng2 = orng ->* [](int x) { return x + 5; };
        auto it = orng2->begin();
        if (*it++ != 6) return false;
        if (*it++ != 7) return false;
        if (*it++ != 8) return false;
        if (it != orng2->end()) return false;

        return true;
    }());

    static_assert([] {
        range r = std::array{std::pair{1, 2}, std::pair{3, 4}};
        auto r2 = r ->* [](int a, int b) { return a + b; };
        auto it = r2.begin();
        if (*it++ != 3) return false;
        if (*it++ != 7) return false;
        if (it != r2.end()) return false;

        return true;
    }());


    static_assert([] {
        range r = std::array{1, 2, 3, 4, 5};
        auto r2 = r[range{std::array{
            range{std::array{1, 0}},
            range{std::array{0, 1}}
        }}] + 1;
        if (r2[0][0] != 3) return false;
        if (r2[0][1] != 2) return false;
        if (r2[1][0] != 2) return false;
        if (r2[1][1] != 3) return false;

        return true;
    }());


    static_assert([] {
        range r = std::array{1, 2, 3};
        auto r2 = r ->* range{} ->* range{} ->* *range{};
        if (r2[0][0] != 1) return false;

        return true;
    }());

}



#endif  // BERTRAND_ITER_RANGE_H