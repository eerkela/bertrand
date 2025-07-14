#ifndef BERTRAND_TUPLE_H
#define BERTRAND_TUPLE_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/union.h"


/// TODO: this could probably be moved fully into func.h, which would centralize
/// most of the tuple logic there, alongside the ->* operator, argument annotations,
/// etc.





namespace bertrand {


namespace impl {
    struct tuple_storage_tag {};

}


namespace meta {

    template <typename T>
    concept tuple_storage = inherits<T, impl::tuple_storage_tag>;


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

    

/// TODO: document tuples.  These won't be fully defined until func.h, so that they
/// can integrate with argument annotations for named tuple support.  Eventually with
/// reflection, I can probably even make the argument names available through the
/// recursive inheritance structure, so you'd be able to just do
/// Tuple t{"foo"_ = 1, "bar"_ = 2.5};
/// t.foo;  // 1
/// t.bar;  // 2.5


template <meta::not_void... Ts> requires (!meta::rvalue<Ts> && ...)
struct Tuple;


template <meta::not_void... Ts>
Tuple(Ts&&...) -> Tuple<meta::remove_rvalue<Ts>...>;


namespace impl {


    /// TODO: match(), which is a special case of `visit()` that accepts only one
    /// argument, which may be a tuple or union of tuples, which may contain nested
    /// unions or tuples within it





    /* Tuple iterators can be optimized away if the tuple is empty, or into an array of
    pointers if all elements unpack to the same lvalue type.  Otherwise, they must
    build a vtable and perform a dynamic dispatch to yield a proper value type, which
    may be a union. */
    enum class tuple_array_kind {
        NO_COMMON_TYPE,
        EMPTY,
        CONSISTENT,
        DYNAMIC
    };

    /* Indexing and/or iterating over a tuple requires the creation of some kind of
    array, which can either be a flat array of homogenous references or a vtable of
    function pointers that produce a common type (which may be a `Union`) to which all
    results are convertible. */
    template <typename, typename>
    struct _tuple_array {
        using types = meta::pack<>;
        using reference = const NoneType&;
        static constexpr tuple_array_kind kind = tuple_array_kind::EMPTY;
        static constexpr bool nothrow = true;
    };
    template <typename in, typename T>
    struct _tuple_array<in, meta::pack<T>> {
        using types = meta::pack<T>;
        using reference = T;
        static constexpr tuple_array_kind kind = meta::lvalue<T> && meta::has_address<T> ?
            tuple_array_kind::CONSISTENT : tuple_array_kind::DYNAMIC;
        static constexpr bool nothrow = true;
    };
    template <typename in, typename... Ts> requires (sizeof...(Ts) > 1)
    struct _tuple_array<in, meta::pack<Ts...>> {
        using types = meta::pack<Ts...>;
        using reference = bertrand::Union<Ts...>;
        static constexpr tuple_array_kind kind = (meta::convertible_to<Ts, reference> && ...) ?
            tuple_array_kind::DYNAMIC : tuple_array_kind::NO_COMMON_TYPE;
        static constexpr bool nothrow = (meta::nothrow::convertible_to<Ts, reference> && ...);
    };
    template <meta::tuple_like T>
    struct tuple_array :
        _tuple_array<T, typename meta::tuple_types<T>::template eval<meta::to_unique>>
    {
    private:
        using base = _tuple_array<
            T,
            typename meta::tuple_types<T>::template eval<meta::to_unique>
        >;

    public:
        using ptr = base::reference(*)(T) noexcept (base::nothrow);

        template <size_t I>
        static constexpr base::reference fn(T t) noexcept (base::nothrow) {
            return meta::unpack_tuple<I>(t);
        }

        template <typename = std::make_index_sequence<meta::tuple_size<T>>>
        static constexpr ptr tbl[0] {};
        template <size_t... Is>
        static constexpr ptr tbl<std::index_sequence<Is...>>[sizeof...(Is)] { &fn<Is>... };
    };

    template <typename>
    struct tuple_iterator {};

    template <typename T>
    concept enable_tuple_iterator =
        meta::lvalue<T> &&
        meta::tuple_like<T> &&
        tuple_array<T>::kind != tuple_array_kind::NO_COMMON_TYPE;

    /// TODO: figure out how to properly handle arrows for tuple iterators, as well as
    /// possibly optional iterators.

    /* An iterator over an otherwise non-iterable tuple type, which constructs a vtable
    of callback functions yielding each value.  This allows tuples to be used as inputs
    to iterable algorithms, as long as those algorithms are built to handle possible
    `Union` values. */
    template <enable_tuple_iterator T>
        requires (tuple_array<T>::kind == tuple_array_kind::DYNAMIC)
    struct tuple_iterator<T> {
        using types = tuple_array<T>::types;
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using reference = tuple_array<T>::reference;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<value_type>;

    private:
        using table = tuple_array<T>;
        using indices = std::make_index_sequence<types::size()>;
        using storage = meta::as_pointer<T>;

        [[nodiscard]] constexpr tuple_iterator(storage data, difference_type index) noexcept :
            data(data),
            index(index)
        {}

    public:
        storage data;
        difference_type index;

        [[nodiscard]] constexpr tuple_iterator(difference_type index = 0) noexcept :
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
            return table::template tbl<>[index](*data);
        }

        /// TODO: I can expose a perfectly normal operator->() if I wrap a reference

        [[nodiscard]] constexpr reference operator[](
            difference_type n
        ) const noexcept (table::nothrow) {
            return table::template tbl<>[index + n](*data);
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

    /* A special case of `tuple_iterator` for tuples where all elements share the
    same addressable type.  In this case, the vtable is reduced to a simple array of
    pointers that are initialized on construction, without requiring dynamic
    dispatch. */
    template <enable_tuple_iterator T>
        requires (tuple_array<T>::kind == tuple_array_kind::CONSISTENT)
    struct tuple_iterator<T> {
        using types = tuple_array<T>::types;
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using reference = tuple_array<T>::reference;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::address_type<reference>;

    private:
        using indices = std::make_index_sequence<types::size()>;
        using array = std::array<pointer, types::size()>;

        template <size_t... Is>
        static constexpr array init(std::index_sequence<Is...>, T t)
            noexcept ((requires{{
                std::addressof(meta::unpack_tuple<Is>(t))
            } noexcept -> meta::nothrow::convertible_to<pointer>;} && ...))
        {
            return {std::addressof(meta::unpack_tuple<Is>(t))...};
        }

        [[nodiscard]] constexpr tuple_iterator(const array& arr, difference_type index) noexcept :
            arr(arr),
            index(index)
        {}

    public:
        array arr;
        difference_type index;

        [[nodiscard]] constexpr tuple_iterator(difference_type index = 0) noexcept :
            arr{},
            index(index)
        {}

        [[nodiscard]] constexpr tuple_iterator(T t, difference_type index)
            noexcept (requires{{init(indices{}, t)} noexcept;})
        :
            arr(init(indices{}, t)),
            index(index)
        {}

        [[nodiscard]] constexpr reference operator*() const noexcept {
            return *arr[index];
        }

        [[nodiscard]] constexpr pointer operator->() const noexcept {
            return arr[index];
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const noexcept {
            return *arr[index + n];
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
            return {self.arr, self.index + n};
        }

        [[nodiscard]] friend constexpr tuple_iterator operator+(
            difference_type n,
            const tuple_iterator& self
        ) noexcept {
            return {self.arr, self.index + n};
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
            return {arr, index - n};
        }

        [[nodiscard]] constexpr difference_type operator-(const tuple_iterator& rhs) const noexcept {
            return index - index;
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

    /* A special case of `tuple_iterator` for empty tuples, which do not yield any
    results, and are optimized away by the compiler. */
    template <enable_tuple_iterator T>
        requires (tuple_array<T>::kind == tuple_array_kind::EMPTY)
    struct tuple_iterator<T> {
        using types = meta::pack<>;
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const NoneType;
        using pointer = const NoneType*;
        using reference = const NoneType&;

        [[nodiscard]] constexpr tuple_iterator(difference_type = 0) noexcept {}
        [[nodiscard]] constexpr tuple_iterator(T, difference_type) noexcept {}

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

    template <typename...>
    struct _tuple_storage : tuple_storage_tag {
        using types = meta::pack<>;
        constexpr void swap(_tuple_storage&) noexcept {}
        template <size_t I, typename Self> requires (false)  // never actually called
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept;
    };

    template <typename T, typename... Ts>
    struct _tuple_storage<T, Ts...> : _tuple_storage<Ts...> {
    private:
        using type = meta::remove_rvalue<T>;

    public:
        [[no_unique_address]] type data;

        [[nodiscard]] constexpr _tuple_storage() = default;
        [[nodiscard]] constexpr _tuple_storage(T val, Ts... rest)
            noexcept (
                meta::nothrow::convertible_to<T, type> &&
                meta::nothrow::constructible_from<_tuple_storage<Ts...>, Ts...>
            )
            requires (
                meta::convertible_to<T, type> &&
                meta::constructible_from<_tuple_storage<Ts...>, Ts...>
            )
        :
            _tuple_storage<Ts...>(std::forward<Ts>(rest)...),
            data(std::forward<T>(val))
        {}

        constexpr void swap(_tuple_storage& other)
            noexcept (
                meta::nothrow::swappable<meta::remove_rvalue<T>> &&
                meta::nothrow::swappable<_tuple_storage<Ts...>>
            )
            requires (
                meta::swappable<meta::remove_rvalue<T>> &&
                meta::swappable<_tuple_storage<Ts...>>
            )
        {
            _tuple_storage<Ts...>::swap(other);
            std::ranges::swap(data, other.data);
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{
                {std::forward<Self>(self).data(std::forward<A>(args)...)} noexcept;
            })
            requires (
                requires{{std::forward<Self>(self).data(std::forward<A>(args)...)};} &&
                !requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            return (std::forward<Self>(self).data(std::forward<A>(args)...));
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                std::forward<A>(args)...
            )} noexcept;})
            requires (
                !requires{{std::forward<Self>(self).data(std::forward<A>(args)...)};} &&
                requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            using base = meta::qualify<_tuple_storage<Ts...>, Self>;
            return (std::forward<base>(self)(std::forward<A>(args)...));
        }

        template <size_t I, typename Self> requires (I < sizeof...(Ts) + 1)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            if constexpr (I == 0) {
                return (std::forward<Self>(self).data);
            } else {
                using base = meta::qualify<_tuple_storage<Ts...>, Self>;
                return (std::forward<base>(self).template get<I - 1>());
            }
        }
    };

    template <meta::lvalue T, typename... Ts>
    struct _tuple_storage<T, Ts...> : _tuple_storage<Ts...> {
        [[no_unique_address]] struct { T ref; } data;

        /// NOTE: no default constructor for lvalue references
        [[nodiscard]] constexpr _tuple_storage(T ref, Ts... rest)
            noexcept (meta::nothrow::constructible_from<_tuple_storage<Ts...>, Ts...>)
            requires (meta::constructible_from<_tuple_storage<Ts...>, Ts...>)
        :
            _tuple_storage<Ts...>(std::forward<Ts>(rest)...),
            data{ref}
        {}

        constexpr _tuple_storage(const _tuple_storage&) = default;
        constexpr _tuple_storage(_tuple_storage&&) = default;
        constexpr _tuple_storage& operator=(const _tuple_storage& other) {
            _tuple_storage<Ts...>::operator=(other);
            std::construct_at(&data, other.data.ref);
            return *this;
        };
        constexpr _tuple_storage& operator=(_tuple_storage&& other) {
            _tuple_storage<Ts...>::operator=(std::move(other));
            std::construct_at(&data, other.data.ref);
            return *this;
        };

        constexpr void swap(_tuple_storage& other)
            noexcept (meta::nothrow::swappable<_tuple_storage<Ts...>>)
            requires (meta::swappable<_tuple_storage<Ts...>>)
        {
            _tuple_storage<Ts...>::swap(other);
            auto tmp = data;
            std::construct_at(&data, other.data.ref);
            std::construct_at(&other.data, tmp.ref);
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{
                {std::forward<Self>(self).data.ref(std::forward<A>(args)...)} noexcept;
            })
            requires (
                requires{{std::forward<Self>(self).data.ref(std::forward<A>(args)...)};} &&
                !requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            return (std::forward<Self>(self).data.ref(std::forward<A>(args)...));
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                std::forward<A>(args)...
            )} noexcept;})
            requires (
                !requires{{std::forward<Self>(self).data.ref(std::forward<A>(args)...)};} &&
                requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            using base = meta::qualify<_tuple_storage<Ts...>, Self>;
            return (std::forward<base>(self)(std::forward<A>(args)...));
        }

        template <size_t I, typename Self> requires (I < sizeof...(Ts) + 1)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            if constexpr (I == 0) {
                return (std::forward<Self>(self).data.ref);
            } else {
                using base = meta::qualify<_tuple_storage<Ts...>, Self>;
                return (std::forward<base>(self).template get<I - 1>());
            }
        }
    };

    /* A basic implementation of a tuple using recursive inheritance, meant to be used
    in conjunction with `union_storage` as the basis for further algebraic types.
    Tuples of this form can be destructured just like `std::tuple`, invoked as if they
    were overload sets, and iterated over/indexed like an array, possibly yielding
    `Union`s if the tuple types are heterogeneous. */
    template <meta::not_void... Ts>
    struct tuple_storage : _tuple_storage<Ts...> {
        using types = meta::pack<Ts...>;
        using size_type = size_t;
        using index_type = ssize_t;
        using iterator = tuple_iterator<tuple_storage&>;
        using const_iterator = tuple_iterator<const tuple_storage&>;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        using _tuple_storage<Ts...>::_tuple_storage;

        /* Return the total number of elements within the tuple, as an unsigned
        integer. */
        [[nodiscard]] static constexpr size_type size() noexcept {
            return sizeof...(Ts);
        }

        /* Return the total number of elements within the tuple, as a signed
        integer. */
        [[nodiscard]] static constexpr index_type ssize() noexcept {
            return index_type(size());
        }

        /* Return true if the tuple holds no elements. */
        [[nodiscard]] static constexpr bool empty() noexcept {
            return size() == 0;
        }

        /* Perfectly forward the value at a specific index, where that index is known
        at compile time. */
        template <size_type I, typename Self> requires (I < size())
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            using base = meta::qualify<_tuple_storage<Ts...>, Self>;
            return (std::forward<base>(self).template get<I>());
        }

        /* Swap the contents of two tuples with the same type specification. */
        constexpr void swap(tuple_storage& other)
            noexcept (meta::nothrow::swappable<_tuple_storage<Ts...>>)
            requires (meta::swappable<_tuple_storage<Ts...>>)
        {
            if (this != &other) {
                _tuple_storage<Ts...>::swap(other);
            }
        }

        /* Index into the tuple, perfectly forwarding the result according to the
        tuple's current cvref qualifications. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self)
            noexcept (tuple_array<meta::forward<Self>>::nothrow)
            requires (
                tuple_array<meta::forward<Self>>::kind != tuple_array_kind::NO_COMMON_TYPE &&
                tuple_array<meta::forward<Self>>::kind != tuple_array_kind::EMPTY
            )
        {
            return (tuple_array<meta::forward<Self>>::template tbl<>[
                self._value.index()
            ](std::forward<Self>(self)));
        }

        /* Get an iterator to a specific index of the tuple. */
        [[nodiscard]] constexpr iterator at(size_type index)
            noexcept (requires{{iterator{*this, index}} noexcept;})
            requires (requires{{iterator{*this, index}};})
        {
            return {*this, index_type(index)};
        }

        /* Get an iterator to a specific index of the tuple. */
        [[nodiscard]] constexpr const_iterator at(size_type index) const
            noexcept (requires{{const_iterator{*this, index}} noexcept;})
            requires (requires{{const_iterator{*this, index}};})
        {
            return {*this, index_type(index)};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr iterator begin()
            noexcept (requires{{iterator{*this, 0}} noexcept;})
            requires (requires{{iterator{*this, 0}};})
        {
            return {*this, 0};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr const_iterator begin() const
            noexcept (requires{{const_iterator{*this, 0}} noexcept;})
            requires (requires{{const_iterator{*this, 0}};})
        {
            return {*this, 0};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr const_iterator cbegin() const
            noexcept (requires{{const_iterator{*this, 0}} noexcept;})
            requires (requires{{const_iterator{*this, 0}};})
        {
            return {*this, 0};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr iterator end()
            noexcept (requires{{iterator{ssize()}} noexcept;})
            requires (requires{{iterator{ssize()}};})
        {
            return {ssize()};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr const_iterator end() const
            noexcept (requires{{const_iterator{ssize()}} noexcept;})
            requires (requires{{const_iterator{ssize()}};})
        {
            return {ssize()};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr const_iterator cend() const
            noexcept (requires{{const_iterator{ssize()}} noexcept;})
            requires (requires{{const_iterator{ssize()}};})
        {
            return {ssize()};
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr reverse_iterator rbegin()
            noexcept (requires{{std::make_reverse_iterator(end())} noexcept;})
            requires (requires{{std::make_reverse_iterator(end())};})
        {
            return std::make_reverse_iterator(end());
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator rbegin() const
            noexcept (requires{{std::make_reverse_iterator(end())} noexcept;})
            requires (requires{{std::make_reverse_iterator(end())};})
        {
            return std::make_reverse_iterator(end());
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator crbegin() const
            noexcept (requires{{std::make_reverse_iterator(cend())} noexcept;})
            requires (requires{{std::make_reverse_iterator(cend())};})
        {
            return std::make_reverse_iterator(cend());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr reverse_iterator rend()
            noexcept (requires{{std::make_reverse_iterator(begin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin())};})
        {
            return std::make_reverse_iterator(begin());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator rend() const
            noexcept (requires{{std::make_reverse_iterator(begin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin())};})
        {
            return std::make_reverse_iterator(begin());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator crend() const
            noexcept (requires{{std::make_reverse_iterator(cbegin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(cbegin())};})
        {
            return std::make_reverse_iterator(cbegin());
        }
    };

    /* A special case of `tuple_storage<Ts...>` where all `Ts...` are identical,
    allowing the storage layout to optimize to a flat array instead of requiring
    recursive base classes, speeding up both compilation and indexing/iteration. */
    template <meta::not_void T, meta::not_void... Ts> requires (std::same_as<T, Ts> && ...)
    struct tuple_storage<T, Ts...> : tuple_storage_tag {
        using types = meta::pack<T, Ts...>;
        using size_type = size_t;
        using index_type = ssize_t;

        /* Return the total number of elements within the tuple, as an unsigned integer. */
        [[nodiscard]] static constexpr size_type size() noexcept {
            return sizeof...(Ts) + 1;
        }

        /* Return the total number of elements within the tuple, as a signed integer. */
        [[nodiscard]] static constexpr index_type ssize() noexcept {
            return index_type(size());
        }

        /* Return true if the tuple holds no elements. */
        [[nodiscard]] static constexpr bool empty() noexcept {
            return size() == 0;
        }

    private:
        struct store { meta::remove_rvalue<T> value; };
        using array = std::array<store, size()>;

    public:
        struct iterator {
            using iterator_category = std::contiguous_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = meta::remove_reference<T>;
            using reference = meta::as_lvalue<value_type>;
            using pointer = meta::as_pointer<value_type>;

            store* ptr;

            [[nodiscard]] constexpr reference operator*() const noexcept {
                return ptr->value;
            }

            [[nodiscard]] constexpr pointer operator->() const
                noexcept (meta::nothrow::address_returns<pointer, reference>)
                requires (meta::address_returns<pointer, reference>)
            {
                return std::addressof(ptr->value);
            }

            [[nodiscard]] constexpr reference operator[](difference_type n) const noexcept {
                return ptr[n].value;
            }

            constexpr iterator& operator++() noexcept {
                ++ptr;
                return *this;
            }

            [[nodiscard]] constexpr iterator operator++(int) noexcept {
                iterator tmp = *this;
                ++ptr;
                return tmp;
            }

            [[nodiscard]] friend constexpr iterator operator+(
                const iterator& self,
                difference_type n
            ) noexcept {
                return {self.ptr + n};
            }

            [[nodiscard]] friend constexpr iterator operator+(
                difference_type n,
                const iterator& self
            ) noexcept {
                return {self.ptr + n};
            }

            constexpr iterator& operator+=(difference_type n) noexcept {
                ptr += n;
                return *this;
            }

            constexpr iterator& operator--() noexcept {
                --ptr;
                return *this;
            }

            [[nodiscard]] constexpr iterator operator--(int) noexcept {
                iterator tmp = *this;
                --ptr;
                return tmp;
            }

            [[nodiscard]] constexpr iterator operator-(difference_type n) const noexcept {
                return {ptr - n};
            }

            [[nodiscard]] constexpr difference_type operator-(const iterator& other) const noexcept {
                return ptr - other.ptr;
            }

            [[nodiscard]] constexpr bool operator==(const iterator& other) const noexcept {
                return ptr == other.ptr;
            }

            [[nodiscard]] constexpr auto operator<=>(const iterator& other) const noexcept {
                return ptr <=> other.ptr;
            }
        };

        struct const_iterator {
            using iterator_category = std::contiguous_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = meta::remove_reference<meta::as_const<T>>;
            using reference = meta::as_lvalue<value_type>;
            using pointer = meta::as_pointer<value_type>;

            const store* ptr;

            [[nodiscard]] constexpr reference operator*() const noexcept {
                return ptr->value;
            }

            [[nodiscard]] constexpr pointer operator->() const
                noexcept (meta::nothrow::address_returns<pointer, reference>)
                requires (meta::address_returns<pointer, reference>)
            {
                return std::addressof(ptr->value);
            }

            [[nodiscard]] constexpr reference operator[](difference_type n) const noexcept {
                return ptr[n].value;
            }

            constexpr const_iterator& operator++() noexcept {
                ++ptr;
                return *this;
            }

            [[nodiscard]] constexpr const_iterator operator++(int) noexcept {
                const_iterator tmp = *this;
                ++ptr;
                return tmp;
            }

            [[nodiscard]] friend constexpr const_iterator operator+(
                const const_iterator& self,
                difference_type n
            ) noexcept {
                return {self.ptr + n};
            }

            [[nodiscard]] friend constexpr const_iterator operator+(
                difference_type n,
                const const_iterator& self
            ) noexcept {
                return {self.ptr + n};
            }

            constexpr const_iterator& operator+=(difference_type n) noexcept {
                ptr += n;
                return *this;
            }

            constexpr const_iterator& operator--() noexcept {
                --ptr;
                return *this;
            }

            [[nodiscard]] constexpr const_iterator operator--(int) noexcept {
                const_iterator tmp = *this;
                --ptr;
                return tmp;
            }

            [[nodiscard]] constexpr const_iterator operator-(difference_type n) const noexcept {
                return {ptr - n};
            }

            [[nodiscard]] constexpr difference_type operator-(
                const const_iterator& other
            ) const noexcept {
                return ptr - other.ptr;
            }

            [[nodiscard]] constexpr bool operator==(const const_iterator& other) const noexcept {
                return ptr == other.ptr;
            }

            [[nodiscard]] constexpr auto operator<=>(const const_iterator& other) const noexcept {
                return ptr <=> other.ptr;
            }
        };

        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        array data;

        [[nodiscard]] constexpr tuple_storage()
            noexcept (meta::nothrow::default_constructible<meta::remove_rvalue<T>>)
            requires (!meta::lvalue<T> && meta::default_constructible<meta::remove_rvalue<T>>)
        :
            data{}
        {}

        [[nodiscard]] constexpr tuple_storage(T val, Ts... rest)
            noexcept (requires{
                {array{store{std::forward<T>(val)}, store{std::forward<Ts>(rest)}...}} noexcept;
            })
            requires (requires{
                {array{store{std::forward<T>(val)}, store{std::forward<Ts>(rest)}...}};
            })
        :
            data{store{std::forward<T>(val)}, store{std::forward<Ts>(rest)}...}
        {}

        [[nodiscard]] constexpr tuple_storage(const tuple_storage&) = default;
        [[nodiscard]] constexpr tuple_storage(tuple_storage&&) = default;

        constexpr tuple_storage& operator=(const tuple_storage& other)
            noexcept (meta::lvalue<T> || meta::nothrow::copy_assignable<meta::remove_rvalue<T>>)
            requires (meta::lvalue<T> || meta::copy_assignable<meta::remove_rvalue<T>>)
        {
            if constexpr (meta::lvalue<T>) {
                if (this != &other) {
                    for (size_type i = 0; i < size(); ++i) {
                        std::construct_at(&data[i].value, other.data[i].value);
                    }
                }
            } else {
                data = other.data;
            }
            return *this;
        }

        constexpr tuple_storage& operator=(tuple_storage&& other)
            noexcept (meta::lvalue<T> || meta::nothrow::move_assignable<meta::remove_rvalue<T>>)
            requires (meta::lvalue<T> || meta::move_assignable<meta::remove_rvalue<T>>)
        {
            if constexpr (meta::lvalue<T>) {
                if (this != &other) {
                    for (size_type i = 0; i < size(); ++i) {
                        std::construct_at(&data[i].value, other.data[i].value);
                    }
                }
            } else {
                data = std::move(other).data;
            }
            return *this;
        }

        /* Swap the contents of two tuples with the same type specification. */
        constexpr void swap(tuple_storage& other)
            noexcept (meta::lvalue<T> || meta::nothrow::swappable<meta::remove_rvalue<T>>)
            requires (meta::lvalue<T> || meta::swappable<meta::remove_rvalue<T>>)
        {
            if (this != &other) {
                for (size_type i = 0; i < size(); ++i) {
                    if constexpr (meta::lvalue<T>) {
                        store tmp = data[i];
                        std::construct_at(&data[i].value, other.data[i].value);
                        std::construct_at(&other.data[i].value, tmp.value);
                    } else {
                        std::ranges::swap(data[i].value, other.data[i].value);
                    }
                }
            }
        }

        /* Invoke the tuple as an overload set, assuming precisely one element is
        invocable with the given arguments. */
        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{
                {std::forward<Self>(self).data[0].value(std::forward<A>(args)...)} noexcept;
            })
            requires (size() == 1 && requires{
                {std::forward<Self>(self).data[0].value(std::forward<A>(args)...)};
            })
        {
            return (std::forward<Self>(self).data[0].value(std::forward<A>(args)...));
        }

        /* Perfectly forward the value at a specific index, where that index is known
        at compile time. */
        template <size_t I, typename Self> requires (I < size())
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            return (std::forward<Self>(self).data[I].value);
        }

        /* Index into the tuple, perfectly forwarding the result according to the
        tuple's current cvref qualifications. */
        template <typename Self>
        constexpr decltype(auto) operator[](this Self&& self, size_type index) noexcept {
            return (std::forward<Self>(self).data[index].value);
        }

        /* Get an iterator to a specific index of the tuple. */
        [[nodiscard]] constexpr iterator at(size_type index) noexcept {
            return {data.data() + index};
        }

        /* Get an iterator to a specific index of the tuple. */
        [[nodiscard]] constexpr const_iterator at(size_type index) const noexcept {
            return {data.data() + index};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr iterator begin() noexcept {
            return {data.data()};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr const_iterator begin() const noexcept {
            return {data.data()};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
            return {data.data()};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr iterator end() noexcept {
            return {data.data() + size()};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr const_iterator end() const noexcept {
            return {data.data() + size()};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr const_iterator cend() const noexcept {
            return {data.data() + size()};
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr reverse_iterator rbegin() noexcept {
            return std::make_reverse_iterator(end());
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept {
            return std::make_reverse_iterator(end());
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept {
            return std::make_reverse_iterator(cend());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr reverse_iterator rend() noexcept {
            return std::make_reverse_iterator(begin());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept {
            return std::make_reverse_iterator(begin());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept {
            return std::make_reverse_iterator(cbegin());
        }
    };

    template <typename... Ts>
    tuple_storage(Ts&&...) -> tuple_storage<meta::remove_rvalue<Ts>...>;

}


/// TODO: instead of `unpack()` and `comprehension()` being public operators, the
/// new tuple iterable refactor means I should be able to roll them into `range()` -
/// which is ideal.  Unambiguous unpacking can therefore be accomplished by
/// `func(*range(opt))` instead of `func(unpack(opt))`.  That's actually superior,
/// because it's harder to confuse with an ordinary function call, and removes another
/// symbol from the public API.



}


namespace std {

    template <typename... Ts>
    struct tuple_size<bertrand::impl::tuple_storage<Ts...>> : std::integral_constant<
        typename std::remove_cvref_t<bertrand::impl::tuple_storage<Ts...>>::size_type,
        bertrand::impl::tuple_storage<Ts...>::size()
    > {};

    template <size_t I, typename... Ts>
        requires (I < std::tuple_size<bertrand::impl::tuple_storage<Ts...>>::value)
    struct tuple_element<I, bertrand::impl::tuple_storage<Ts...>> {
        using type = bertrand::impl::tuple_storage<Ts...>::types::template at<I>;
    };

    template <size_t I, bertrand::meta::tuple_storage T>
        requires (I < std::tuple_size<std::remove_cvref_t<T>>::value)
    constexpr decltype(auto) get(T&& t) {
        return (std::forward<T>(t).template get<I>());
    }

}


#endif // BERTRAND_TUPLE_H

