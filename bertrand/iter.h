#ifndef BERTRAND_ITER_H
#define BERTRAND_ITER_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/union.h"
#include <ranges>


namespace bertrand {


namespace impl {
    struct range_tag {};
    struct slice_tag {};
    struct where_tag {};
    struct comprehension_tag {};
    struct tuple_storage_tag {};

    template <typename Start, typename Stop, typename Step>
    concept iota_spec =
        meta::unqualified<Start> &&
        meta::unqualified<Stop> &&
        meta::copyable<Start> &&
        meta::copyable<Stop> &&
        meta::lt_returns<bool, const Stop&, const Start&> && (
            (meta::is_void<Step> && meta::has_preincrement<Start&>) || (
                meta::not_void<Step> &&
                meta::unqualified<Step> &&
                meta::copyable<Step> &&
                meta::gt_returns<bool, const Stop&, const Start&> &&
                meta::has_iadd<Start&, const Step&>
            )
        );

    template <typename Start, typename Stop, typename Step> requires (iota_spec<Start, Stop, Step>)
    struct iota;

}


template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
struct range;


template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
range(C&&) -> range<meta::remove_rvalue<C>>;


template <typename Stop>
    requires (!meta::iterable<Stop> && !meta::tuple_like<Stop> && impl::iota_spec<Stop, Stop, void>)
range(Stop) -> range<impl::iota<Stop, Stop, void>>;


template <meta::iterator Begin, meta::sentinel_for<Begin> End>
range(Begin, End) -> range<std::ranges::subrange<Begin, End>>;


template <typename Start, typename Stop>
    requires (
        (!meta::iterator<Start> || !meta::sentinel_for<Stop, Start>) &&
        impl::iota_spec<Start, Stop, void>
    )
range(Start, Stop) -> range<impl::iota<Start, Stop, void>>;


template <typename Start, typename Stop, typename Step>
    requires (impl::iota_spec<Start, Stop, Step>)
range(Start, Stop, Step) -> range<impl::iota<Start, Stop, Step>>;


template <meta::not_void Start, meta::not_void Stop, meta::not_void Step>
struct slice;


template <typename C>
struct where;


/// TODO: this can possibly remove the extra initializer list, and just accept any
/// number of booleans as arguments.


template <meta::boolean T>
where(std::initializer_list<T>) -> where<std::initializer_list<meta::remove_rvalue<T>>>;


template <meta::yields<bool> C>
where(C&&) -> where<meta::remove_rvalue<C>>;


template <typename F>
where(F&&) -> where<meta::remove_rvalue<F>>;


template <meta::not_void... Ts>
struct Tuple;


template <meta::not_void... Ts>
Tuple(Ts&&...) -> Tuple<meta::remove_rvalue<Ts>...>;


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

    /// TODO: maybe I can eliminate the concept of a separate `slice_indices` class,
    /// and can instead template `impl::slice` to only accept slices 

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

    /// TODO: maybe `slice` needs to also be templated on the integer type that was
    /// used for the indices.

    /* A subclass of `range` that only represents a subset of the elements, according
    to Python slicing semantics.  The indices are given in a `bertrand::slice` helper
    struct, which serves as a factory for this type. */
    template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
    struct slice;

    template <typename C, meta::slice S>
        requires ((meta::iterable<C> || meta::tuple_like<C>) && S::normalized)
    slice(C&&, S) -> slice<meta::remove_rvalue<C>>;


    /// TODO: mask -> where, and then it can be part of the public API, as
    ///    List list2 = List{1, 2, 3}[where{true, false, true}];
    ///    List list2 = where{true, false, true}(std::vector{1, 2, 3});

    /// TODO: maybe `mask` can also take a function that will be called for each
    /// element, and the result of that function is used as the mask.
    ///    List list2 = List{1, 2, 3}[where{[](int x) { return x % 2 == 0; }}];

    /// TODO: bonus points if both the `where{}` and `slice{}` keyword objects can be
    /// used along with function chaining, so that the following works as well:
    ///     auto f = List{1, 2, 3} ->* where{[](int x) { return x % 2 == 0; }};
    /// This would need to return an `impl::mask` type where `C` is the original
    /// container, and `M` is an lvalue comprehension over it, which invokes the
    /// supplied function.  `->*` would be special-cased to accept this and slices
    /// in a similar fashion, which allows it to be used as a kind of universal
    /// substitution operator, where unions stack overloads, tuples stack arguments,
    /// ranges can produce either comprehensions, masks, or slices.



    /* A subclass of `range` that only represents a subset of the elements, which
    correspond to the `true` indices of a boolean mask.  The length of the range is
    given by the number of true values in the mask, or the size of the underlying
    container, whichever comes first. */
    template <typename C, meta::yields<bool> M> requires (meta::iterable<C> || meta::tuple_like<C>)
    struct where;

    template <typename C, meta::yields<bool> M> requires (meta::iterable<C> || meta::tuple_like<C>)
    where(C&&, M&&) -> where<meta::remove_rvalue<C>, meta::remove_rvalue<M>>;





    /// TODO: enable_borrowed_range should always be enabled for iotas.

    /* Iota iterators will use the difference type between `stop` and `start` if
    available and integer-like. */
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
            !requires{{step < 0} -> meta::explicitly_convertible_to<bool>;} || (
            requires{
                {step < 0} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {!(start > stop)} noexcept -> meta::nothrow::convertible_to<bool>;
            }
        )))
        requires (requires{{!(start < stop)} -> meta::convertible_to<bool>;} && (
            !requires{{step < 0} -> meta::explicitly_convertible_to<bool>;} ||
            requires{{!(start > stop)} -> meta::convertible_to<bool>;}
        ))
    {
        if constexpr (requires{{step < 0} -> meta::explicitly_convertible_to<bool>;}) {
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
    template <typename Start, typename Stop, typename Step> requires (iota_spec<Start, Stop, Step>)
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
            noexcept (meta::nothrow::has_iadd<Start&, const Step&>)
            requires (meta::has_iadd<Start&, const Step&>)
        {
            start += step;
            return *this;
        }


        [[nodiscard]] constexpr iota_iterator operator++(int)
            noexcept (
                meta::nothrow::copyable<iota_iterator> &&
                meta::nothrow::has_iadd<Start&, const Step&>
            )
            requires (meta::has_iadd<Start&, const Step&>)
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
            noexcept (meta::nothrow::has_isub<Start&, const Step&>)
            requires (meta::has_isub<Start&, const Step&>)
        {
            start -= step;
            return *this;
        }

        [[nodiscard]] constexpr iota_iterator operator--(int)
            noexcept (
                meta::nothrow::copyable<iota_iterator> &&
                meta::nothrow::has_isub<Start&, const Step&>
            )
            requires (meta::has_isub<Start&, const Step&>)
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
            noexcept (requires{{iota_empty(self.start, self.stop, self.step)} noexcept;})
            requires (requires{{iota_empty(self.start, self.stop, self.step)};})
        {
            return iota_empty(self.start, self.stop, self.step);
        }

        [[nodiscard]] friend constexpr bool operator==(NoneType, const iota_iterator& self)
            noexcept (requires{{iota_empty(self.start, self.stop, self.step)} noexcept;})
            requires (requires{{iota_empty(self.start, self.stop, self.step)};})
        {
            return iota_empty(self.start, self.stop, self.step);
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
            noexcept (requires{
                {self.start < self.stop} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{self.start < self.stop} -> meta::convertible_to<bool>;})
        {
            return self.start < self.stop;
        }

        [[nodiscard]] friend constexpr bool operator!=(NoneType, const iota_iterator& self)
            noexcept (requires{
                {self.start < self.stop} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{self.start < self.stop} -> meta::convertible_to<bool>;})
        {
            return self.start < self.stop;
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
    template <typename Start, typename Stop> requires (iota_spec<Start, Stop, void>)
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
            noexcept (requires{
                {!(self.start < self.stop)} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{!(self.start < self.stop)} -> meta::convertible_to<bool>;})
        {
            return !(self.start < self.stop);
        }

        [[nodiscard]] friend constexpr bool operator==(NoneType, const iota_iterator& self)
            noexcept (requires{
                {!(self.start < self.stop)} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{!(self.start < self.stop)} -> meta::convertible_to<bool>;})
        {
            return !(self.start < self.stop);
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
            noexcept (requires{
                {self.start < self.stop} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{self.start < self.stop} -> meta::convertible_to<bool>;})
        {
            return self.start < self.stop;
        }

        [[nodiscard]] friend constexpr bool operator!=(NoneType, const iota_iterator& self)
            noexcept (requires{
                {self.start < self.stop} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{{self.start < self.stop} -> meta::convertible_to<bool>;})
        {
            return self.start < self.stop;
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
    template <typename Start, typename Stop, typename Step> requires (iota_spec<Start, Stop, Step>)
    struct iota {
        using start_type = Start;
        using stop_type = Stop;
        using step_type = Step;
        using size_type = size_t;
        using index_type = ssize_t;
        using iterator = iota_iterator<Start, Stop, Step>;

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

        /* Get an iterator to the start of the iota. */
        [[nodiscard]] constexpr iterator cbegin() const
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
        [[nodiscard]] constexpr NoneType end() const noexcept { return None; }

        /* Get a sentinel for the end of the iota. */
        [[nodiscard]] constexpr NoneType cend() const noexcept { return None; }

        /// TODO: at()?

    };

    /* A specialization of `iota` that lacks a step size.  This causes the iteration
    algorithm to use prefix `++` rather than `+= step` to get the next value, which
    allows us to ignore negative step sizes and increase performance. */
    template <typename Start, typename Stop> requires (iota_spec<Start, Stop, void>)
    struct iota<Start, Stop, void> {
        using start_type = Start;
        using stop_type = Stop;
        using step_type = void;
        using size_type = size_t;
        using index_type = ssize_t;
        using iterator = iota_iterator<Start, Stop, void>;

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


        /* Get an iterator to the start of the iota. */
        [[nodiscard]] constexpr iterator begin() const
            noexcept (meta::nothrow::constructible_from<iterator, const Start&, const Stop&>)
        {
            return iterator{start, stop};
        }

        /* Get an iterator to the start of the iota. */
        [[nodiscard]] constexpr iterator cbegin() const
            noexcept (meta::nothrow::constructible_from<iterator, const Start&, const Stop&>)
        {
            return iterator{start, stop};
        }

        /* Get a sentinel for the end of the iota. */
        [[nodiscard]] constexpr NoneType end() const noexcept { return None; }

        /* Get a sentinel for the end of the iota. */
        [[nodiscard]] constexpr NoneType cend() const noexcept { return None; }


        /// TODO: at()




    };

    template <typename Stop> requires (iota_spec<Stop, Stop, void>)
    iota(Stop) -> iota<Stop, Stop, void>;

    template <typename Start, typename Stop> requires (iota_spec<Start, Stop, void>)
    iota(Start, Stop) -> iota<Start, Stop, void>;

    template <typename Start, typename Stop, typename Step> requires (iota_spec<Start, Stop, Step>)
    iota(Start, Stop, Step) -> iota<Start, Stop, Step>;

}


namespace meta {

    namespace detail {

        template <typename>
        constexpr bool unpack = false;
        template <typename C>
        constexpr bool unpack<impl::unpack<C>> = true;

        template <typename>
        constexpr bool slice = false;
        template <typename... Ts>
        constexpr bool slice<bertrand::slice<Ts...>> = true;

        template <typename>
        constexpr bool where = false;
        template <typename... Ts>
        constexpr bool where<bertrand::where<Ts...>> = true;

        using where_trivial = range<::std::array<bool, 0>>;

    }

    template <typename T>
    concept range = inherits<T, impl::range_tag>;

    template <typename T>
    concept unpack = detail::unpack<unqualify<T>>;

    template <typename T>
    concept slice = detail::slice<unqualify<T>>;

    template <typename T>
    concept where = detail::where<unqualify<T>>;

    template <typename T>
    concept comprehension = inherits<T, impl::comprehension_tag>;

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




    namespace detail {

        template <meta::range T>
        constexpr bool prefer_constructor<T> = true;

        template <meta::range T>
        constexpr bool exact_size<T> = meta::exact_size<typename T::__type>;

    }

}


/////////////////////
////    RANGE    ////
/////////////////////


namespace impl {

    /* Tuple iterators can be optimized away if the tuple is empty, or into an array of
    pointers if all elements unpack to the same lvalue type.  Otherwise, they must
    build a vtable and perform a dynamic dispatch to yield a proper value type, which
    may be a union. */
    enum class tuple_array_kind : uint8_t {
        NO_COMMON_TYPE,
        DYNAMIC,
        ARRAY,
        EMPTY,
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
            tuple_array_kind::ARRAY : tuple_array_kind::DYNAMIC;
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

    template <typename>
    struct tuple_iterator {};

    template <typename T>
    concept enable_tuple_iterator =
        meta::lvalue<T> &&
        meta::tuple_like<T> &&
        tuple_array<T>::kind != tuple_array_kind::NO_COMMON_TYPE;

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
        using indices = std::make_index_sequence<meta::tuple_size<T>>;
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
            return typename table::dispatch{index}(*data);
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

    /* A special case of `tuple_iterator` for tuples where all elements share the
    same addressable type.  In this case, the vtable is reduced to a simple array of
    pointers that are initialized on construction, without requiring dynamic
    dispatch. */
    template <enable_tuple_iterator T>
        requires (tuple_array<T>::kind == tuple_array_kind::ARRAY)
    struct tuple_iterator<T> {
        using types = tuple_array<T>::types;
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using reference = tuple_array<T>::reference;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::address_type<reference>;

    private:
        using indices = std::make_index_sequence<meta::tuple_size<T>>;
        using array = std::array<pointer, meta::tuple_size<T>>;

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

        [[nodiscard]] constexpr tuple_iterator(difference_type index = meta::tuple_size<T>) noexcept :
            arr{},
            index(index)
        {}

        [[nodiscard]] constexpr tuple_iterator(T t, difference_type index = 0)
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

        [[nodiscard]] constexpr bool operator==(const tuple_iterator& other) const noexcept {
            return index == other.index;
        }

        [[nodiscard]] constexpr auto operator<=>(const tuple_iterator& other) const noexcept {
            return index <=> other.index;
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
    template <meta::lvalue T>
    struct make_range_iterator {
    private:
        using C = decltype((std::declval<T>().__value));

    public:
        static constexpr bool tuple = true;
        using begin_type = tuple_iterator<C>;
        using end_type = begin_type;

        [[nodiscard]] static constexpr begin_type begin(T t)
            noexcept (requires{{begin_type{t.__value}} noexcept;})
        {
            return begin_type(t.__value);
        }

        [[nodiscard]] static constexpr end_type end(T t)
            noexcept (requires{{end_type{}} noexcept;})
        {
            return end_type{};
        }
    };
    template <meta::lvalue T> requires (meta::iterable<decltype((std::declval<T>().__value))>)
    struct make_range_iterator<T> {
    private:
        using C = decltype((std::declval<T>().__value));

    public:
        static constexpr bool tuple = false;
        using begin_type = make_range_begin<C>::type;
        using end_type = make_range_end<C>::type;

        [[nodiscard]] static constexpr begin_type begin(T t)
            noexcept (meta::nothrow::has_begin<C>)
            requires (meta::has_begin<C>)
        {
            return std::ranges::begin(t.__value);
        }

        [[nodiscard]] static constexpr end_type end(T t)
            noexcept (meta::nothrow::has_end<C>)
            requires (meta::has_end<C>)
        {
            return std::ranges::end(t.__value);
        }
    };

    /* `make_range_reversed` abstracts the reverse iterator methods for a `range`,
    synthesizing a corresponding tuple iterator if the underlying container is not
    already iterable. */
    template <meta::lvalue T>
    struct make_range_reversed {
    private:
        using C = decltype((std::declval<T>().__value));

    public:
        static constexpr bool tuple = true;
        using begin_type = std::reverse_iterator<tuple_iterator<C>>;
        using end_type = begin_type;

        [[nodiscard]] static constexpr begin_type begin(T t)
            noexcept (requires{{begin_type{begin_type{t.__value, meta::tuple_size<C>}}} noexcept;})
        {
            return begin_type{tuple_iterator<C>{t.__value, meta::tuple_size<C>}};
        }

        [[nodiscard]] static constexpr end_type end(T t)
            noexcept (requires{{end_type{begin_type{size_t(0)}}} noexcept;})
        {
            return end_type{begin_type{size_t(0)}};
        }
    };
    template <meta::lvalue T> requires (meta::reverse_iterable<decltype((std::declval<T>().__value))>)
    struct make_range_reversed<T> {
    private:
        using C = decltype((std::declval<T>().__value));

    public:
        static constexpr bool tuple = false;
        using begin_type = make_range_rbegin<C>::type;
        using end_type = make_range_rend<C>::type;

        [[nodiscard]] static constexpr begin_type begin(T t)
            noexcept (meta::nothrow::has_rbegin<C>)
            requires (meta::has_rbegin<C>)
        {
            return std::ranges::rbegin(t.__value);
        }

        [[nodiscard]] static constexpr end_type end(T t)
            noexcept (meta::nothrow::has_rend<C>)
            requires (meta::has_rend<C>)
        {
            return std::ranges::rend(t.__value);
        }
    };

    /* An adaptor for a container that causes `range<impl::reversed<C>>` to reverse
    iterate over the container `C` instead of forward iterating.  This equates to
    swapping all of the `begin()` and `end()` methods with their reversed counterparts,
    and modifying the indexing logic to map index `i` to index `-i - 1`, which
    triggers Python-style wraparound. */
    template <typename C> requires (meta::reverse_iterable<C> || meta::tuple_like<C>)
    struct reversed {
        using __type = meta::remove_rvalue<C>;

        [[no_unique_address]] __type __value;

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{meta::to_arrow(__value)} noexcept;})
            requires (requires{{meta::to_arrow(__value)};})
        {
            return meta::to_arrow(__value);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{meta::to_arrow(__value)} noexcept;})
            requires (requires{{meta::to_arrow(__value)};})
        {
            return meta::to_arrow(__value);
        }

        [[nodiscard]] constexpr auto size() const
            noexcept (meta::nothrow::has_size<C> || meta::tuple_like<C>)
            requires (meta::has_size<C> || meta::tuple_like<C>)
        {
            if constexpr (meta::has_size<C>) {
                return std::ranges::size(__value);
            } else {
                return meta::tuple_size<C>;
            }
        }

        [[nodiscard]] constexpr auto ssize() const
            noexcept (meta::nothrow::has_ssize<C> || meta::tuple_like<C>)
            requires (meta::has_ssize<C> || meta::tuple_like<C>)
        {
            if constexpr (meta::has_ssize<C>) {
                return std::ranges::ssize(__value);
            } else {
                return meta::to_signed(meta::tuple_size<C>);
            }
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (meta::nothrow::has_empty<C> || meta::tuple_like<C>)
            requires (meta::has_empty<C> || meta::tuple_like<C>)
        {
            if constexpr (meta::has_empty<C>) {
                return std::ranges::empty(__value);
            } else {
                return meta::tuple_size<C> == 0;
            }
        }

        template <ssize_t I, typename Self>
        constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{meta::unpack_tuple<-I - 1>(std::forward<Self>(self).__value)} noexcept;})
            requires (requires{{meta::unpack_tuple<-I - 1>(std::forward<Self>(self).__value)};})
        {
            return (meta::unpack_tuple<-I - 1>(std::forward<Self>(self).__value));
        }

        template <typename Self>
        constexpr decltype(auto) operator[](this Self&& self, ssize_t i)
            noexcept (requires{{std::forward<Self>(self).__value[
                size_t(impl::normalize_index(self.ssize(), -i - 1))]
            } noexcept;})
            requires (requires{{std::forward<Self>(self).__value[
                size_t(impl::normalize_index(self.ssize(), -i - 1))]
            };})
        {
            return (std::forward<Self>(self).__value[
                size_t(impl::normalize_index(self.ssize(), -i - 1))
            ]);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) begin(this Self& self)
            noexcept (requires{{impl::make_range_reversed<Self&>::begin(self)} noexcept;})
            requires (requires{{impl::make_range_reversed<Self&>::begin(self)};})
        {
            return (impl::make_range_reversed<Self&>::begin(self));
        }

        [[nodiscard]] constexpr decltype(auto) cbegin() const
            noexcept (requires{{impl::make_range_reversed<const reversed&>::begin(*this)} noexcept;})
            requires (requires{{impl::make_range_reversed<const reversed&>::begin(*this)};})
        {
            return (impl::make_range_reversed<const reversed&>::begin(*this));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) end(this Self& self)
            noexcept (requires{{impl::make_range_reversed<Self&>::end(self)} noexcept;})
            requires (requires{{impl::make_range_reversed<Self&>::end(self)};})
        {
            return (impl::make_range_reversed<Self&>::end(self));
        }

        [[nodiscard]] constexpr decltype(auto) cend() const
            noexcept (requires{{impl::make_range_reversed<const reversed&>::end(*this)} noexcept;})
            requires (requires{{impl::make_range_reversed<const reversed&>::end(*this)};})
        {
            return (impl::make_range_reversed<const reversed&>::end(*this));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) rbegin(this Self& self)
            noexcept (requires{{impl::make_range_iterator<Self&>::begin(self)} noexcept;})
            requires (requires{{impl::make_range_iterator<Self&>::begin(self)};})
        {
            return (impl::make_range_iterator<Self&>::begin(self));
        }

        [[nodiscard]] constexpr decltype(auto) crbegin() const
            noexcept (requires{{impl::make_range_iterator<const reversed&>::begin(*this)} noexcept;})
            requires (requires{{impl::make_range_iterator<const reversed&>::begin(*this)};})
        {
            return (impl::make_range_iterator<const reversed&>::begin(*this));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) rend(this Self& self)
            noexcept (requires{{impl::make_range_iterator<Self&>::end(self)} noexcept;})
            requires (requires{{impl::make_range_iterator<Self&>::end(self)};})
        {
            return (impl::make_range_iterator<Self&>::end(self));
        }

        [[nodiscard]] constexpr decltype(auto) crend() const
            noexcept (requires{{impl::make_range_iterator<const reversed&>::end(*this)} noexcept;})
            requires (requires{{impl::make_range_iterator<const reversed&>::end(*this)};})
        {
            return (impl::make_range_iterator<const reversed&>::end(*this));
        }
    };

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

}


/// TODO: all ranges (including the `reversed` helper above) should use `impl::ref`
/// to store the underlying container, such that they can store both lvalue and
/// rvalue references, and can be assigned to, etc.


/* A wrapper for an arbitrary container type that can be used to form iterable
expressions.

Ranges can be constructed in a variety of ways, effectively replacing each of the
following with a unified CTAD constructor:

    1.  `std::ranges::subrange(begin, end)` -> `range(begin, end)`, where `begin` is
        an iterator and `end` is a corresponding sentinel.
    2.  `std::views::iota(start, stop)` -> `range(start, stop)`, where `start` and
        `stop` are integers.  Also permits an extra, non-zero, integer step size
        similar to Python's `range(start, stop, step)`.
    3.  `std::views::all(container)` -> `range(container)`, where `container` is any
        iterable or tuple-like type.

If the underlying container is tuple-like, then the range will be as well, and will
forward to the container's `get<I>()` method when accessed or destructured.  If the
tuple is not directly iterable, then an iterator will be generated for it, which may
yield `Union<Ts...>`, where `Ts...` are the unique return types for `get<I>()` over
each index.  Thus, all tuples, regardless of implementation, should produce iterable
ranges just like any other container.

`range` has a number of subclasses, all of which extend the iteration interface in some
way:

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

Each subclass of `range` is also exposed to Bertrand's monadic operator interface,
which returns lazily-evaluated `comprehension`s that encode each operation into an
expression tree.  The tree will only be evaluated when the range is indexed, iterated
over, or converted to a compatible type, which reduces it to a single loop that can be
aggressively optimized by the compiler. */
template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
struct range : impl::range_tag {
    using __type = meta::remove_rvalue<C>;

    [[no_unique_address]] __type __value;

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
            !meta::iterable<Stop> &&
            !meta::tuple_like<Stop> &&
            impl::iota_spec<Stop, Stop, void>
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
            impl::iota_spec<Start, Stop, void>
        )
    [[nodiscard]] explicit constexpr range(Start start, Stop stop)
        noexcept (meta::nothrow::constructible_from<__type, Start, Stop>)
        requires (meta::constructible_from<__type, Start, Stop>)
    :
        __value(start, stop)
    {}

    /* CTAD constructor for 3-argument iota ranges. */
    template <typename Start, typename Stop, typename Step>
        requires (impl::iota_spec<Start, Stop, Step>)
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
        std::ranges::swap(__value, other.__value);
    }

    /* Dereferencing a range promotes it into a trivial `unpack` subclass, which allows
    it to be destructured when used as an argument to a Bertrand function. */
    template <typename Self>
    [[nodiscard]] constexpr auto operator*(this Self&& self)
        noexcept (requires{{impl::unpack{std::forward<Self>(self).__value}} noexcept;})
        requires (requires{{impl::unpack{std::forward<Self>(self).__value}};})
    {
        return impl::unpack{std::forward<Self>(self).__value};
    }

    /* Indirectly access a member of the wrapped container. */
    [[nodiscard]] constexpr auto operator->()
        noexcept (requires{{meta::to_arrow(__value)} noexcept;})
        requires (requires{{meta::to_arrow(__value)};})
    {
        return meta::to_arrow(__value);
    }

    /* Indirectly access a member of the wrapped container. */
    [[nodiscard]] constexpr auto operator->() const
        noexcept (requires{{meta::to_arrow(__value)} noexcept;})
        requires (requires{{meta::to_arrow(__value)};})
    {
        return meta::to_arrow(__value);
    }

    /* Forwarding `size()` operator for the underlying container, provided the
    container supports it. */
    [[nodiscard]] constexpr auto size() const
        noexcept (meta::nothrow::has_size<C> || meta::tuple_like<C>)
        requires (meta::has_size<C> || meta::tuple_like<C>)
    {
        if constexpr (meta::has_size<C>) {
            return std::ranges::size(__value);
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
            return std::ranges::ssize(__value);
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
            return std::ranges::empty(__value);
        } else {
            return meta::tuple_size<C> == 0;
        }
    }

    /* Forwarding `get<I>()` accessor, provided the underlying container is
    tuple-like.  Automatically applies Python-style wraparound for negative indices. */
    template <ssize_t I, typename Self>
    constexpr decltype(auto) get(this Self&& self)
        noexcept (requires{{meta::unpack_tuple<I>(std::forward<Self>(self).__value)} noexcept;})
        requires (requires{{meta::unpack_tuple<I>(std::forward<Self>(self).__value)};})
    {
        return (meta::unpack_tuple<I>(std::forward<Self>(self).__value));
    }

    /* Integer indexing operator.  Accepts a single signed integer and retrieves the
    corresponding element from the underlying container after applying Python-style
    wraparound for negative indices.  If the container does not support indexing, but
    is otherwise tuple-like, then a vtable will be synthesized to back this
    operator. */
    template <typename Self>
    constexpr decltype(auto) operator[](this Self&& self, ssize_t i)
        noexcept (requires{{std::forward<Self>(self).__value[
            size_t(impl::normalize_index(self.ssize(), i))
        ]} noexcept;} || meta::tuple_like<C>)
        requires (requires{{std::forward<Self>(self).__value[
            size_t(impl::normalize_index(self.ssize(), i))
        ]};} || meta::tuple_like<C>)
    {
        if constexpr (requires{{std::forward<Self>(self).__value[
            size_t(impl::normalize_index(self.ssize(), i))
        ]};}) {
            return (std::forward<Self>(self).__value[
                size_t(impl::normalize_index(self.ssize(), i))
            ]);

        } else {
            using container = decltype((std::forward<Self>(self).__value));
            return typename impl::tuple_array<container>::dispatch{
                size_t(impl::normalize_index(self.ssize(), i))
            }(std::forward<Self>(self).__value);
        }
    }

    /* Slice operator, which returns a subset of the range according to a Python-style
    `slice` expression. */
    template <typename Self>
    constexpr auto operator[](this Self&& self, const bertrand::slice& s)
        noexcept (requires{{s(std::forward<Self>(self).__value, s)} noexcept;})
        requires (requires{{s(std::forward<Self>(self).__value, s)};})
    {
        return s(std::forward<Self>(self).__value, s);
    }

    /* Where operator, which returns a subset of the range corresponding to the `true`
    values of a `where` expression.  The length of the resulting range is given by the
    number of true values in the mask or the size of this range, whichever is
    smaller. */
    template <typename Self, meta::where M>
    constexpr auto operator[](this Self&& self, M&& mask)
        noexcept (requires{{std::forward<M>(mask)(std::forward<Self>(self).__value)} noexcept;})
        requires (requires{{std::forward<M>(mask)(std::forward<Self>(self).__value)};})
    {
        return std::forward<M>(mask)(std::forward<Self>(self).__value);
    }

    /* Get a forward iterator to the start of the range. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) begin(this Self& self)
        noexcept (requires{{impl::make_range_iterator<Self&>::begin(self)} noexcept;})
        requires (requires{{impl::make_range_iterator<Self&>::begin(self)};})
    {
        return (impl::make_range_iterator<Self&>::begin(self));
    }

    /* Get a forward iterator to the start of the range. */
    [[nodiscard]] constexpr decltype(auto) cbegin() const
        noexcept (requires{{impl::make_range_iterator<const range&>::begin(*this)} noexcept;})
        requires (requires{{impl::make_range_iterator<const range&>::begin(*this)};})
    {
        return (impl::make_range_iterator<const range&>::begin(*this));
    }

    /* Get a forward iterator to one past the last element of the range. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) end(this Self& self)
        noexcept (requires{{impl::make_range_iterator<Self&>::end(self)} noexcept;})
        requires (requires{{impl::make_range_iterator<Self&>::end(self)};})
    {
        return (impl::make_range_iterator<Self&>::end(self));
    }

    /* Get a forward iterator to one past the last element of the range. */
    [[nodiscard]] constexpr decltype(auto) cend() const
        noexcept (requires{{impl::make_range_iterator<const range&>::end(*this)} noexcept;})
        requires (requires{{impl::make_range_iterator<const range&>::end(*this)};})
    {
        return (impl::make_range_iterator<const range&>::end(*this));
    }

    /* Get a reverse iterator to the last element of the range. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) rbegin(this Self& self)
        noexcept (requires{{impl::make_range_reversed<Self&>::begin(self)} noexcept;})
        requires (requires{{impl::make_range_reversed<Self&>::begin(self)};})
    {
        return (impl::make_range_reversed<Self&>::begin(self));
    }

    /* Get a reverse iterator to the last element of the range. */
    [[nodiscard]] constexpr decltype(auto) crbegin() const
        noexcept (requires{{impl::make_range_reversed<const range&>::begin(*this)} noexcept;})
        requires (requires{{impl::make_range_reversed<const range&>::begin(*this)};})
    {
        return (impl::make_range_reversed<const range&>::begin(*this));
    }

    /* Get a reverse iterator to one before the first element of the range. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) rend(this Self& self)
        noexcept (requires{{impl::make_range_reversed<Self&>::end(self)} noexcept;})
        requires (requires{{impl::make_range_reversed<Self&>::end(self)};})
    {
        return (impl::make_range_reversed<Self&>::end(self));
    }

    /* Get a reverse iterator to one before the first element of the range. */
    [[nodiscard]] constexpr decltype(auto) crend() const
        noexcept (requires{{impl::make_range_reversed<const range&>::end(*this)} noexcept;})
        requires (requires{{impl::make_range_reversed<const range&>::end(*this)};})
    {
        return (impl::make_range_reversed<const range&>::end(*this));
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
                std::forward<Self>(self).__value,
                std::make_index_sequence<meta::tuple_size<C>>{}
            )} noexcept;} ||
            (
                !requires{{impl::range_tuple_conversion<to>(
                    std::forward<Self>(self).__value,
                    std::make_index_sequence<meta::tuple_size<C>>{}
                )};} &&
                requires{{to(std::from_range, std::forward<Self>(self))} noexcept;}
            ) || (
                !requires{{impl::range_tuple_conversion<to>(
                    std::forward<Self>(self).__value,
                    std::make_index_sequence<meta::tuple_size<C>>{}
                )};} &&
                !requires{{to(std::from_range, std::forward<Self>(self))};} &&
                requires{{to(self.begin(), self.end())} noexcept;}
            )
        )
        requires (
            requires{{impl::range_tuple_conversion<to>(
                std::forward<Self>(self).__value,
                std::make_index_sequence<meta::tuple_size<C>>{}
            )};} ||
            requires{{to(std::from_range, std::forward<Self>(self))};} ||
            requires{{to(self.begin(), self.end())};}
        )
    {
        if constexpr (requires{{impl::range_tuple_conversion<to>(
            std::forward<Self>(self).__value,
            std::make_index_sequence<meta::tuple_size<C>>{}
        )};}) {
            return impl::range_tuple_conversion<to>(
                std::forward<Self>(self).__value,
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
    template <meta::range T>
    constexpr range& operator=(T&& other)
        noexcept (requires{{impl::range_tuple_assignment(
            __value,
            std::forward<T>(other),
            std::make_index_sequence<meta::tuple_size<C>>{}
        )} noexcept;})
        requires (
            meta::tuple_like<C> && meta::tuple_like<T> &&
            meta::tuple_size<C> == meta::tuple_size<T> &&
            requires{{impl::range_tuple_assignment(
                __value,
                std::forward<T>(other),
                std::make_index_sequence<meta::tuple_size<C>>{}
            )};}
        )
    {
        impl::range_tuple_assignment(
            __value,
            std::forward<T>(other),
            std::make_index_sequence<meta::tuple_size<C>>{}
        );
        return *this;
    }

    /* Assigning a range to another range triggers elementwise assignment between their
    contents.  If either range is not tuple-like, then the assignment must be done with
    an elementwise loop. */
    template <meta::range T>
    constexpr range& operator=(T&& other)
        requires (
            (!meta::tuple_like<C> || !meta::tuple_like<T>) &&
            requires(
                decltype(begin()) this_it,
                decltype(end()) this_end,
                decltype(std::ranges::begin(other)) other_it,
                decltype(std::ranges::end(other)) other_end
            ){
                {this_it != this_end};
                {other_it != other_end};
                {*this_it = *other_it};
                {++this_it};
                {++other_it};
            }
        )
    {
        auto this_it = begin();
        auto this_end = end();
        auto other_it = std::ranges::begin(other);
        auto other_end = std::ranges::end(other);
        while (this_it != this_end && other_it != other_end) {
            *this_it = *other_it;
            ++this_it;
            ++other_it;
        }
        if (this_it != this_end || other_it != other_end) {
            /// TODO: centralize this error message.
            throw ValueError("range assignment size mismatch");
        }
        return *this;
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


/* A subclass of `range` that reverses the order of iteration, provided the underlying
container is reverse iterable.

This class acts just like a normal `range`, but with the forward and reverse iterators
swapped, and the indexing logic modified to map index `i` to index `-i - 1` before
applying Python-style wraparound. */
template <typename C> requires (meta::reverse_iterable<C> || meta::tuple_like<C>)
struct reversed : range<impl::reversed<C>> {
    [[nodiscard]] explicit constexpr reversed(meta::forward<C> c)
        noexcept (meta::nothrow::constructible_from<impl::reversed<C>, meta::forward<C>>)
        requires (meta::constructible_from<impl::reversed<C>, meta::forward<C>>)
    :
        range<impl::reversed<C>>(impl::reversed<C>{std::forward<C>(c)})
    {}
};


template <typename C> requires (meta::reverse_iterable<C> || meta::tuple_like<C>)
reversed(C&& c) -> reversed<meta::remove_rvalue<C>>;


/////////////////////
////    SLICE    ////
/////////////////////


namespace impl {


    template <typename T>
    struct _slice_normalize { using type = meta::as_signed<meta::unqualify<T>>; };
    template <meta::None T>
    struct _slice_normalize<T> { using type = ssize_t; };
    template <typename T> requires ((meta::integer<T> && meta::has_signed<T>) || meta::None<T>)
    using slice_normalize = _slice_normalize<T>::type;

    template <typename... Ts> requires (meta::has_common_type<slice_normalize<Ts>...>)
    using slice_index = meta::common_type<slice_normalize<Ts>...>;




    /// TODO: when a `normalized` slice is passed to the impl::slice constructor,
    /// that's when I check to make sure the step size is not zero.  I otherwise
    /// assume everything has been previously normalized up to that point, so that
    /// I never call `.normalize()` more than once.  It should therefore be possible
    /// to implement slice in a somewhat recursive manner, whereby you accept a
    /// slice with non-integer indices, translate them into integer indices, and
    /// then construct an `impl::slice(*this, slice.normalize(ssize()))`, or
    /// construct a normalized slice directly.






    /// TODO: this slice object should be templated on the container type, which
    /// may be an lvalue.  I'll need some kind of container abstraction that can
    /// handle both cases, which can possibly be shared with the union types as well,
    /// similar to vtables.
    /// -> This exists in the `impl::store<T>` class, which standardizes this behavior.

    /// TODO: slice does need its own iterator class, and will never be reverse
    /// iterable.  Maybe I can use separate `slice` classes for slices with a step
    /// size vs those without?  `.normalize()` would simply return a step size of 1,
    /// and the call operator would produce a corresponding `slice` that can be
    /// absolutely sure that the step size is never negative (and therefore usable
    /// on forward-only ranges)?  Otherwise, if a negative step size is given, then
    /// the container must be iterable in both directions.
    /// -> Maybe the best way to handle this is just to have an if statement catch
    /// the negative case where the container is not reverse iterable and convert it
    /// into a debug assertion.

    /// TODO: the primary difficulty for slices is obtaining an iterator to the
    /// correct position.  In both cases, we get a `begin()` iterator and advance it
    /// to `start` using either `+= start` or a chain of `++` increments, depending
    /// on the capabilities of the iterator.  Then, we obtain each value by doing
    /// `it += step` (where negative steps are handled automatically), or by a nested
    /// `if` that selects between `++it` and `--it` depending on the sign of the step.
    /// -> This is where knowing that the step is positive at compile time would be
    /// helpful, since it avoids the if statement, and could allow faster iteration
    /// in the default case.  The only concern is that it turns the public `slice{}`
    /// helper into a template with 2 specializations.  Maybe this is a good thing
    /// though, since I can also detect `None` initializers and efficiently convert
    /// them into proper indices, without needing a special `missing` value.


    /// TODO: impl::slice_tag refers to the public `slice{}` helper, not the
    /// `impl::slice` class, which just equates to a subclass of `range`.


    /* A wrapper around a bidirectional iterator that yields a subset of a given
    container within a specified start and stop interval, with an arbitrary step
    size.  Containers can expose an `operator[]` overload that returns one of these
    objects to allow Python-style slicing semantics in conjunction with the
    `bertrand::slice` helper class, including basic iteration, assignment and
    extraction via an implicit conversion operator. */
    template <meta::unqualified T> requires (meta::bidirectional_iterator<T>)
    struct slice : slice_tag {
    private:
        using normalized = bertrand::slice::normalized;

        T m_begin;
        normalized m_indices;

    public:
        using value_type = std::iterator_traits<T>::value_type;
        using reference = std::iterator_traits<T>::reference;
        using const_reference = meta::as_const<reference>;
        using pointer = std::iterator_traits<T>::pointer;
        using const_pointer = meta::as_pointer<meta::as_const<meta::remove_pointer<pointer>>>;

        struct iterator {
            using iterator_category = std::conditional_t<
                meta::output_iterator<T, slice::value_type>,
                std::output_iterator_tag,
                std::input_iterator_tag
            >;
            using difference_type = std::iterator_traits<T>::difference_type;
            using value_type = slice::value_type;
            using reference = slice::reference;
            using const_reference = slice::const_reference;
            using pointer = slice::pointer;
            using const_pointer = slice::const_pointer;

        private:
            T iter;
            ssize_t step;
            ssize_t length;

        public:
            constexpr iterator() = default;
            constexpr iterator(const T& iter, ssize_t step, ssize_t length)
                noexcept(meta::nothrow::copyable<T>)
                requires(meta::copyable<T>)
            :
                iter(iter),
                step(step),
                length(length)
            {}

            constexpr iterator(T&& iter, ssize_t step, ssize_t length)
                noexcept(meta::nothrow::movable<T>)
                requires(meta::movable<T>)
            :
                iter(std::move(iter)),
                step(step),
                length(length)
            {}

            [[nodiscard]] constexpr decltype(auto) operator*()
                noexcept(meta::nothrow::has_dereference<T>)
                requires(meta::has_dereference<T>)
            {
                return (*iter);
            }

            [[nodiscard]] constexpr decltype(auto) operator*() const
                noexcept(meta::nothrow::has_dereference<const T>)
                requires(meta::has_dereference<const T>)
            {
                return (*iter);
            }

            /// TODO: use meta::to_arrow instead
            [[nodiscard]] constexpr decltype(auto) operator->()
                noexcept(meta::nothrow::has_arrow<T>)
                requires(meta::has_arrow<T>)
            {
                return (iter.operator->());
            }

            /// TODO: use meta::to_arrow instead
            [[nodiscard]] constexpr decltype(auto) operator->() const
                noexcept(meta::nothrow::has_arrow<const T>)
                requires(meta::has_arrow<const T>)
            {
                return (iter.operator->());
            }

            constexpr iterator& operator++()
                noexcept(
                    meta::nothrow::has_preincrement<T> &&
                    meta::nothrow::has_predecrement<T>
                )
                requires(
                    meta::random_access_iterator<T> &&
                    meta::has_iadd<T, ssize_t>
                )
            {
                iter += step;
                --length;
                return *this;
            }

            constexpr iterator& operator++()
                noexcept(
                    meta::nothrow::has_preincrement<T> &&
                    meta::nothrow::has_predecrement<T>
                )
                requires(
                    !meta::random_access_iterator<T> &&
                    meta::has_preincrement<T> &&
                    meta::has_predecrement<T>
                )
            {
                if (step > 0) {
                    for (ssize_t i = 0; i < step; ++i) ++iter;
                } else {
                    for (ssize_t i = step; i < 0; ++i) --iter;
                }
                --length;
                return *this;
            }

            [[nodiscard]] constexpr iterator operator++(int)
                noexcept(
                    meta::nothrow::copyable<iterator> &&
                    meta::nothrow::has_preincrement<iterator>
                )
                requires(
                    meta::copyable<iterator> &&
                    meta::has_preincrement<iterator>
                )
            {
                iterator copy = *this;
                ++(*this);
                return copy;
            }

            [[nodiscard]] constexpr bool operator==(const iterator& other) const
                noexcept
            {
                return length == other.length;
            }

            [[nodiscard]] constexpr bool operator!=(const iterator& other) const
                noexcept
            {
                return length != other.length;
            }
        };

        using const_iterator = iterator;

        template <meta::at_returns<T> C>
        [[nodiscard]] constexpr slice(C& container, const normalized& indices)
            noexcept(meta::nothrow::at_returns<C, T>)
        :
            m_begin(container.at(indices.start)),
            m_indices(indices)
        {}

        constexpr slice(const slice&) = delete;
        constexpr slice(slice&&) = delete;
        constexpr slice& operator=(const slice&) = delete;
        constexpr slice& operator=(slice&&) = delete;

        [[nodiscard]] constexpr const normalized& indices() const noexcept { return m_indices; }
        [[nodiscard]] constexpr ssize_t start() const noexcept { return m_indices.start; }
        [[nodiscard]] constexpr ssize_t stop() const noexcept { return m_indices.stop; }
        [[nodiscard]] constexpr ssize_t step() const noexcept { return m_indices.step; }
        [[nodiscard]] constexpr ssize_t ssize() const noexcept { return m_indices.length; }
        [[nodiscard]] constexpr size_t size() const noexcept { return size_t(ssize()); }
        [[nodiscard]] constexpr bool empty() const noexcept { return !ssize(); }
        [[nodiscard]] constexpr iterator begin() const
            noexcept(noexcept(iterator{m_begin, m_indices.step, m_indices.length}))
        {
            return {m_begin, m_indices.step, m_indices.length};
        }
        [[nodiscard]] constexpr iterator begin() &&
            noexcept(noexcept(iterator{
                std::move(m_begin),
                m_indices.step,
                m_indices.length
            }))
        {
            return {std::move(m_begin), m_indices.step, m_indices.length};
        }
        [[nodiscard]] constexpr iterator cbegin() const
            noexcept(noexcept(iterator{m_begin, m_indices.step, m_indices.length}))
        {
            return {m_begin, m_indices.step, m_indices.length};
        }
        [[nodiscard]] constexpr iterator cbegin() &&
            noexcept(noexcept(iterator{
                std::move(m_begin),
                m_indices.step,
                m_indices.length
            }))
        {
            return {std::move(m_begin), m_indices.step, m_indices.length};
        }
        [[nodiscard]] constexpr iterator end() const
            noexcept(noexcept(iterator{T{}, m_indices.stop, 0}))
        {
            return {T{}, m_indices.step, 0};
        }
        [[nodiscard]] constexpr iterator cend() const
            noexcept(noexcept(iterator{T{}, m_indices.stop, 0}))
        {
            return {T{}, m_indices.step, 0};
        }

        template <typename V>
        [[nodiscard]] constexpr operator V() const
            noexcept (requires{{V(std::from_range, *this)} noexcept;})
            requires (requires{{V(std::from_range, *this)};})
        {
            return V(std::from_range, *this);
        }

        template <typename V>
        [[nodiscard]] constexpr operator V() const
            noexcept (requires{{V(begin(), end())} noexcept;})
            requires (!requires{{V(std::from_range, *this)};} && requires{{V(begin(), end())};})
        {
            return V(begin(), end());
        }

        template <meta::iterable Range>
        constexpr slice& operator=(Range&& range)
            requires (meta::output_iterator<T, meta::yield_type<Range>>)
        {
            constexpr bool has_size = meta::has_size<meta::as_lvalue<Range>>;

            // if the range has an explicit size, then we can check it ahead of time
            // to ensure that it exactly matches that of the slice
            if constexpr (has_size) {
                if (std::ranges::size(range) != size()) {
                    throw ValueError(
                        "cannot assign a range of size " +
                        std::to_string(std::ranges::size(range)) +
                        " to a slice of size " + std::to_string(size())
                    );
                }
            }

            // If we checked the size above, we can avoid checking it again on each
            // iteration
            auto it = std::ranges::begin(range);
            auto end = std::ranges::end(range);
            auto output = begin();
            for (ssize_t i = 0; i < m_indices.length; ++i) {
                if constexpr (!has_size) {
                    if (it == end) {
                        throw ValueError(
                            "not enough values to fill slice of size " +
                            std::to_string(size())
                        );
                    }
                }
                *output = *it;
                ++it;
                ++output;
            }

            if constexpr (!has_size) {
                if (it != end) {
                    throw ValueError(
                        "range length exceeds slice of size " +
                        std::to_string(size())
                    );
                }
            }
            return *this;
        }
    };

}


/// TODO: slice<Start, Stop, Step>, which records all the indices as template
/// parameters, and do not necessarily have to be integers.


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
struct slice : impl::slice_tag {
    using start_type = meta::remove_rvalue<Start>;
    using stop_type = meta::remove_rvalue<Stop>;
    using step_type = meta::remove_rvalue<Step>;

    [[no_unique_address]] impl::store<start_type> start;
    [[no_unique_address]] impl::store<stop_type> stop;
    [[no_unique_address]] impl::store<step_type> step;

    /* Slices are considered to be normalized if they populate all 3 fields to a
    single, signed integer type, as indicated by this field.  If this is true, then
    the slice can be used to initialize an `impl::slice` range, which implements the
    range interface.  Otherwise, the `slice.normalize(ssize)` method can be used to
    coerce the indices into a normalized form, or a normalized slice can be constructed
    manually to implement custom indexing. */
    static constexpr bool normalized =
        std::same_as<Start, Stop> && std::same_as<Stop, Step> && meta::signed_integer<Start>;

    /* If the slice is normalized, then get the total number of elements that will
    be included within it as an unsigned integer. */
    [[nodiscard]] constexpr auto size() const noexcept requires (normalized) {
        return meta::to_unsigned(ssize());
    }

    /* If the slice is normalized, then get the total number of elements that will
    be included within it as a signed integer. */
    [[nodiscard]] constexpr Start ssize() const noexcept requires (normalized) {
        Start bias = step.value + (step.value < 0) - (step.value > 0);
        Start length = (stop.value - start.value + bias) / step.value;
        return length * (length > 0);
    }

    /* If the slice is normalized, check to see if it contains no elements. */
    [[nodiscard]] constexpr bool empty() const noexcept requires (normalized) {
        return ssize() == 0;
    }

    /// TODO: determine the common signed integer type between start, stop, and step,
    /// excluding any that are None, and defaulting to `ssize_t` if all are none.

    /* Normalize the provided indices against a container of a given size, returning a
    4-tuple with members `start`, `stop`, `step`, and `length` in that order, and
    supporting structured bindings.  If either of the original `start` or `stop`
    indices were given as negative values or `nullopt`, they will be normalized
    according to the size, and will be truncated to the nearest end if they are out
    of bounds.  `length` stores the total number of elements that will be included in
    the slice */
    [[nodiscard]] constexpr auto normalize(
        impl::slice_index<Start, Stop, Step> size
    ) const noexcept
        requires (
            ((meta::integer<start_type> && meta::has_signed<start_type>) || meta::None<start_type>) &&
            ((meta::integer<stop_type> && meta::has_signed<stop_type>) || meta::None<stop_type>) &&
            ((meta::integer<step_type> && meta::has_signed<step_type>) || meta::None<step_type>) &&
            meta::has_common_type<
                impl::slice_normalize<Start>,
                impl::slice_normalize<Stop>,
                impl::slice_normalize<Step>
            >
        )
    {
        using index = impl::slice_index<Start, Stop, Step>;
        slice<index, index, index> result {
            .start = 0,
            .stop = size,
            .step = 1
        };

        // if no step size is given, then we can exclude negative step sizes from the
        // normalization logic
        if constexpr (meta::None<step_type>) {
            // normalize and truncate start
            if constexpr (meta::integer<start_type>) {
                result.start = index(start.value);
                result.start += size * (result.start < 0);
                if (result.start < 0) {
                    result.start = 0;
                } else {
                    result.start = size;
                }
            }

            // normalize and truncate stop
            if constexpr (meta::integer<stop_type>) {
                result.stop = index(stop.value);
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
            result.step = index(step.value);
            bool sign = result.step < 0;

            // normalize and truncate start
            if constexpr (meta::None<start_type>) {
                result.start = (size - 1) * sign;  // neg: size - 1 | pos: 0
            } else {
                result.start = index(start.value);
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
                result.stop = index(stop.value);
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
        /// TODO: call the constructor for `impl::slice` with the result of
        /// `normalize(std::ranges::ssize(container))` or `normalize(meta::tuple_size<C>)`
    }


    /// TODO: .range(container, indices).  Maybe this can just be a 2-argument
    /// call operator?  Or maybe just the constructor for `impl::slice` directly?
    /// The second option is probably better, since I'll have to call that anyways,
    /// and it doesn't clutter the interface for this class.


    /// TODO: call operator that applies the slice to a container, returning an
    /// impl::slice range type.

};


/////////////////////
////    WHERE    ////
/////////////////////


/// TODO: where{}



//////////////////////////////
////    COMPREHENSIONS    ////
//////////////////////////////




/////////////////////////////////
////    MONADIC OPERATORS    ////
/////////////////////////////////









/////////////////////
////    TUPLE    ////
/////////////////////


/// TODO: Tuples are defined in their own header, which is included just after
/// static strings, in order to take advantage of named fields.  All I need for
/// comprehensions is a forward declaration, and then it can assume the rest of the
/// interface ahead of time.


namespace impl {

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

            /// TODO: use meta::to_arrow instead
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

            /// TODO: use meta::to_arrow instead
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















namespace impl {



    /* Enable/disable and optimize the `any()`/`all()` operators such that they fail
    fast where possible, unlike other reductions. */
    template <typename... Ts>
    struct truthy {
        template <typename F>
        static constexpr bool enable = ((
            meta::callable<F, Ts> &&
            meta::explicitly_convertible_to<
                meta::call_type<F, Ts>,
                bool
            >
        ) && ...);
        template <typename F>
        static constexpr bool nothrow = ((
            meta::nothrow::callable<F, Ts> &&
            meta::nothrow::explicitly_convertible_to<
                meta::nothrow::call_type<F, Ts>,
                bool
            >
        ) && ...);

        template <typename F, typename... A> requires (sizeof...(A) > 1 && enable<F>)
        static constexpr bool all(F&& func, A&&... args) noexcept(nothrow<F>) {
            return (std::forward<F>(func)(std::forward<A>(args)) && ...);
        }

        template <size_t I = 0, typename F, meta::tuple_like T>
            requires (I < meta::tuple_size<T> && meta::has_get<T, I> && enable<F>)
        static constexpr bool all(F&& func, T&& t) noexcept(
            nothrow<F> &&
            meta::nothrow::has_get<T, I> &&
            noexcept(all<I + 1>(std::forward<F>(func), std::forward<T>(t)))
        ) {
            if (!std::forward<F>(func)(meta::unpack_tuple<I>(std::forward<T>(t)))) {
                return false;
            }
            return all<I + 1>(std::forward<F>(func), std::forward<T>(t));;
        }

        template <size_t I = 0, typename F, meta::tuple_like T>
            requires (I >= meta::tuple_size<T> && enable<F>)
        static constexpr bool all(F&&, T&&) noexcept {
            return true;
        }

        template <typename F, typename... A> requires (sizeof...(A) > 1 && enable<F>)
        static constexpr bool any(F&& func, A&&... args) noexcept(nothrow<F>) {
            return (std::forward<F>(func)(std::forward<A>(args)) || ...);
        }

        template <size_t I = 0, typename F, meta::tuple_like T>
            requires (I < meta::tuple_size<T> && meta::has_get<T, I> && enable<F>)
        static constexpr bool any(F&& func, T&& t) noexcept(
            meta::tuple_types<T>::template eval<truthy>::template nothrow<F> &&
            meta::nothrow::has_get<T, I> &&
            noexcept(any<I + 1>(std::forward<F>(func), std::forward<T>(t)))
        ) {
            if (std::forward<F>(func)(meta::unpack_tuple<I>(std::forward<T>(t)))) {
                return true;
            }
            return any<I + 1>(std::forward<F>(func), std::forward<T>(t));;
        }

        template <size_t I = 0, typename F, meta::tuple_like T>
            requires (I >= meta::tuple_size<T> && enable<F>)
        static constexpr bool any(F&&, T&&) noexcept {
            return false;
        }
    };

    /* Apply a pairwise function over the arguments to implement a `fold_left()`
    function call. */
    template <typename out, typename...>
    struct _fold_left {
        template <typename F>
        struct traits {
            using type = meta::remove_rvalue<out>;
            static constexpr bool enable = true;
            static constexpr bool nothrow = true;
        };
        template <typename F, typename T>
        static constexpr traits<F>::type operator()(F&&, T&& arg) noexcept(
            meta::nothrow::convertible_to<T, typename traits<F>::type>
        ) {
            return std::forward<T>(arg);
        }
    };
    template <typename prev, typename curr, typename... next>
    struct _fold_left<prev, curr, next...> {
        template <typename F>
        struct traits {
            using type = void;
            static constexpr bool enable = false;
            static constexpr bool nothrow = false;
        };
        template <meta::callable<prev, curr> F>
            requires (meta::has_common_type<prev, meta::call_type<F, prev, curr>>)
        struct traits<F> {
            using recur = _fold_left<
                meta::common_type<prev, meta::call_type<F, prev, curr>>,
                next...
            >::template traits<F>;
            using type = recur::type;
            static constexpr bool enable = recur::enable;
            static constexpr bool nothrow =
                meta::nothrow::callable<F, prev, curr> &&
                meta::nothrow::convertible_to<meta::call_type<F, prev, curr>, type> &&
                recur::nothrow;
        };
        template <typename F, typename L, typename R, typename... Ts>
        static constexpr decltype(auto) operator()(
            F&& func,
            L&& lhs,
            R&& rhs,
            Ts&&... rest
        ) noexcept(traits<F>::nothrow) {
            return (_fold_left<
                meta::common_type<prev, meta::call_type<F, prev, curr>>,
                next...
            >{}(
                std::forward<F>(func),
                std::forward<F>(func)(std::forward<L>(lhs), std::forward<R>(rhs)),
                std::forward<Ts>(rest)...
            ));
        }
    };
    template <typename F, typename... Ts>
    using fold_left = _fold_left<Ts...>::template traits<F>;

    /* Apply a pairwise function over the arguments to implement a `fold_right()`
    function call. */
    template <typename out, typename...>
    struct _fold_right {
        template <typename F>
        struct traits {
            using type = meta::remove_rvalue<out>;
            static constexpr bool enable = true;
            static constexpr bool nothrow = true;
        };
        template <typename F, typename T>
        static constexpr traits<F>::type operator()(F&&, T&& arg) noexcept(
            meta::nothrow::convertible_to<T, typename traits<F>::type>
        ) {
            return std::forward<T>(arg);
        }
    };
    template <typename prev, typename curr, typename... next>
    struct _fold_right<prev, curr, next...> {
        template <typename F>
        using recur = _fold_right<curr, next...>::template traits<F>;

        template <typename F>
        struct traits {
            using type = void;
            static constexpr bool enable = false;
            static constexpr bool nothrow = false;
        };
        template <typename F>
            requires (
                meta::callable<F, prev, typename recur<F>::type> &&
                meta::has_common_type<
                prev,
                    meta::call_type<F, prev, typename recur<F>::type>
                >
            )
        struct traits<F> {
            using type = meta::common_type<
                prev,
                meta::call_type<F, prev, typename recur<F>::type>
            >;
            static constexpr bool enable = recur<F>::enable;
            static constexpr bool nothrow =
                recur<F>::nothrow &&
                meta::nothrow::callable<F, prev, typename recur<F>::type> &&
                meta::nothrow::convertible_to<
                    meta::call_type<F, prev, typename recur<F>::type>,
                    type
                >;
        };
        template <typename F, typename L, typename R, typename... Ts>
        static constexpr decltype(auto) operator()(
            F&& func,
            L&& lhs,
            R&& rhs,
            Ts&&... rest
        ) noexcept(traits<F>::nothrow) {
            return (std::forward<F>(func)(
                std::forward<L>(lhs),
                _fold_right<curr, next...>{}(
                    std::forward<F>(func),
                    std::forward<R>(rhs),
                    std::forward<Ts>(rest)...
                )
            ));
        }
    };
    template <typename F, typename... Ts>
    using fold_right = _fold_right<Ts...>::template traits<F>;

    /* Apply a boolean less-than predicate over the arguments to implement a `min()`
    function call. */
    template <typename out, typename...>
    struct _min {
        template <typename F>
        struct traits {
            using type = meta::remove_rvalue<out>;
            static constexpr bool enable = true;
            static constexpr bool nothrow = true;
        };
        template <typename F, typename T>
        static constexpr traits<F>::type operator()(F&&, T&& arg) noexcept(
            meta::nothrow::convertible_to<T, typename traits<F>::type>
        ) {
            return std::forward<T>(arg);
        }
    };
    template <typename prev, typename curr, typename... next>
    struct _min<prev, curr, next...> {
        template <typename F>
        struct traits {
            using type = void;
            static constexpr bool enable = false;
            static constexpr bool nothrow = false;
        };
        template <meta::callable<prev, curr> F>
            requires (meta::explicitly_convertible_to<
                meta::call_type<F, prev, curr>,
                bool
            >)
        struct traits<F> {
            using recur =
                _min<meta::common_type<prev, curr>, next...>::template traits<F>;
            using type = recur::type;
            static constexpr bool enable = recur::enable;
            static constexpr bool nothrow =
                meta::nothrow::callable<F, prev, curr> &&
                meta::nothrow::explicitly_convertible_to<
                    meta::call_type<F, prev, curr>,
                    bool
                > &&
                meta::nothrow::convertible_to<curr, type> &&
                recur::nothrow;
        };
        template <typename F, typename T, typename R, typename... Ts>
        static constexpr decltype(auto) operator()(
            F&& func,
            T&& min,
            R&& rhs,
            Ts&&... rest
        ) noexcept(traits<F>::nothrow) {
            // R < L
            if (std::forward<F>(func)(std::forward<R>(rhs), std::forward<T>(min))) {
                return (_min<meta::common_type<prev, curr>, next...>{}(
                    std::forward<F>(func),
                    std::forward<R>(rhs),  // forward R
                    std::forward<Ts>(rest)...
                ));
            } else {
                return (_min<meta::common_type<prev, curr>, next...>{}(
                    std::forward<F>(func),
                    std::forward<T>(min),  // retain min
                    std::forward<Ts>(rest)...
                ));
            }
        }
    };
    template <typename F, typename... Ts>
    using min = _min<Ts...>::template traits<F>;

    /* Apply a boolean less-than predicate over the argument to implement a `max()`
    function call. */
    template <typename out, typename...>
    struct _max {
        template <typename F>
        struct traits {
            using type = meta::remove_rvalue<out>;
            static constexpr bool enable = true;
            static constexpr bool nothrow = true;
        };
        template <typename F, typename T>
        static constexpr traits<F>::type operator()(F&&, T&& arg) noexcept(
            meta::nothrow::convertible_to<T, typename traits<F>::type>
        ) {
            return std::forward<T>(arg);
        }
    };
    template <typename prev, typename curr, typename... next>
    struct _max<prev, curr, next...> {
        template <typename F>
        struct traits {
            using type = void;
            static constexpr bool enable = false;
            static constexpr bool nothrow = false;
        };
        template <meta::callable<prev, curr> F>
            requires (meta::explicitly_convertible_to<
                meta::call_type<F, prev, curr>,
                bool
            >)
        struct traits<F> {
            using recur =
                _max<meta::common_type<prev, curr>, next...>::template traits<F>;
            using type = recur::type;
            static constexpr bool enable = recur::enable;
            static constexpr bool nothrow =
                meta::nothrow::callable<F, prev, curr> &&
                meta::nothrow::explicitly_convertible_to<
                    meta::call_type<F, prev, curr>,
                    bool
                > &&
                meta::nothrow::convertible_to<curr, type> &&
                recur::nothrow;
        };
        template <typename F, typename T, typename R, typename... Ts>
        static constexpr decltype(auto) operator()(
            F&& func,
            T&& max,
            R&& rhs,
            Ts&&... rest
        ) noexcept(traits<F>::nothrow) {
            // L < R
            if (std::forward<F>(func)(std::forward<T>(max), std::forward<R>(rhs))) {
                return (_max<meta::common_type<prev, curr>, next...>{}(
                    std::forward<F>(func),
                    std::forward<R>(rhs),  // forward R
                    std::forward<Ts>(rest)...
                ));
            } else {
                return (_max<meta::common_type<prev, curr>, next...>{}(
                    std::forward<F>(func),
                    std::forward<T>(max),  // retain max
                    std::forward<Ts>(rest)...
                ));
            }
        }
    };
    template <typename F, typename... Ts>
    using max = _max<Ts...>::template traits<F>;

    /* Apply a boolean less-than predicate over the argument to implement a `minmax()`
    function call. */
    template <typename out, typename...>
    struct _minmax {
        template <typename F>
        struct traits {
            using type = std::pair<meta::remove_rvalue<out>, meta::remove_rvalue<out>>;
            static constexpr bool enable = true;
            static constexpr bool nothrow = true;
        };
        template <typename F, typename T1, typename T2>
        static constexpr traits<F>::type operator()(
            F&&,
            T1&& min,
            T2&& max
        ) noexcept(
            meta::nothrow::constructible_from<typename traits<F>::type, T1, T2>
        ) {
            return {std::forward<T1>(min), std::forward<T2>(max)};
        }
    };
    template <typename prev, typename curr, typename... next>
    struct _minmax<prev, curr, next...> {
        template <typename F>
        struct traits {
            using type = void;
            static constexpr bool enable = false;
            static constexpr bool nothrow = false;
        };
        template <meta::callable<prev, curr> F>
            requires (meta::explicitly_convertible_to<
                meta::call_type<F, prev, curr>,
                bool
            >)
        struct traits<F> {
            using recur =
                _minmax<meta::common_type<prev, curr>, next...>::template traits<F>;
            using type = recur::type;
            static constexpr bool enable = recur::enable;
            static constexpr bool nothrow =
                meta::nothrow::callable<F, prev, curr> &&
                meta::nothrow::explicitly_convertible_to<
                    meta::call_type<F, prev, curr>,
                    bool
                > &&
                meta::nothrow::convertible_to<curr, type> &&
                recur::nothrow;
        };
        template <typename F, typename T1, typename T2, typename R, typename... Ts>
        static constexpr decltype(auto) operator()(
            F&& func,
            T1&& min,
            T2&& max,
            R&& rhs,
            Ts&&... rest
        ) noexcept(traits<F>::nothrow) {
            // R < min
            if (std::forward<F>(func)(std::forward<R>(rhs), std::forward<T1>(min))) {
                return (_minmax<meta::common_type<prev, curr>, next...>{}(
                    std::forward<F>(func),
                    std::forward<R>(rhs),  // forward R
                    std::forward<T2>(max),  // retain max
                    std::forward<Ts>(rest)...
                ));

            // max < R
            } else if (std::forward<F>(func)(std::forward<T2>(max), std::forward<R>(rhs))) {
                return (_minmax<meta::common_type<prev, curr>, next...>{}(
                    std::forward<F>(func),
                    std::forward<T1>(min),  // retain min
                    std::forward<R>(rhs),  // forward R
                    std::forward<Ts>(rest)...
                ));

            // min <= R <= max
            } else {
                return (_minmax<meta::common_type<prev, curr>, next...>{}(
                    std::forward<F>(func),
                    std::forward<T1>(min),  // retain min
                    std::forward<T2>(max),  // retain max
                    std::forward<Ts>(rest)...
                ));
            }
        }
    };
    template <typename F, typename... Ts>
    using minmax = _minmax<Ts...>::template traits<F>;

    /* Helper entry point for minmax(), which repeats the first element of the tuple
    before passing to _minmax proper. */
    template <typename... Ts>
    struct minmax_helper {
        template <typename F, typename First, typename... Rest>
        static constexpr decltype(auto) operator()(
            F&& func,
            First&& first,
            Rest&&... rest
        )
            noexcept(minmax<F, Ts...>::nothrow)
            requires(minmax<F, Ts...>::enable)
        {
            return _minmax<Ts...>{}(
                std::forward<F>(func),
                std::forward<First>(first),
                std::forward<First>(first),
                std::forward<Rest>(rest)...
            );
        }
    };

}


/// TODO: if the object has an ssize() method, call that instead of std::ranges::ssize().
/// Same with an adl ssize() method


/* Get the length of an arbitrary sequence in constant time as a signed integer.
Equivalent to calling `std::ranges::ssize(range)`. */
template <meta::has_ssize Range>
[[nodiscard]] constexpr decltype(auto) len(Range&& r)
    noexcept(meta::nothrow::has_ssize<Range>)
{
    return (std::ranges::ssize(std::forward<Range>(r)));
}


/* Get the length of a tuple-like container as a compile-time constant signed integer.
Equivalent to evaluating `meta::tuple_size<T>` on the given type `T`. */
template <meta::tuple_like T> requires (!meta::has_ssize<T>)
[[nodiscard]] constexpr ssize_t len(T&& r) noexcept {
    return ssize_t(meta::tuple_size<T>);
}


/* Get the distance between two iterators as a signed integer.  Equivalent to calling
`std::ranges::distance(begin, end)`.  This may run in O(n) time if the iterators do
not support constant-time distance measurements. */
template <meta::iterator Begin, meta::sentinel_for<Begin> End>
[[nodiscard]] constexpr decltype(auto) len(Begin&& begin, End&& end)
    noexcept(noexcept(std::ranges::distance(
        std::forward<Begin>(begin),
        std::forward<End>(end)
    )))
{
    return (std::ranges::distance(
        std::forward<Begin>(begin),
        std::forward<End>(end)
    ));
}


/* Produce a simple range starting at a default-constructed instance of `End` (zero if
`End` is an integer type), similar to Python's built-in `range()` operator.  This is
equivalent to a  `std::views::iota()` call under the hood. */
template <meta::default_constructible Stop>
[[nodiscard]] constexpr decltype(auto) range(Stop&& stop)
    noexcept(noexcept(std::views::iota(Stop{}, std::forward<Stop>(stop))))
    requires(requires{std::views::iota(Stop{}, std::forward<Stop>(stop));})
{
    return (std::views::iota(Stop{}, std::forward<Stop>(stop)));
}


/* Produce a simple range from `start` to `stop`, similar to Python's built-in
`range()` operator.  This is equivalent to a `std::views::iota()` call under the
hood. */
template <typename Start, typename Stop>
[[nodiscard]] constexpr decltype(auto) range(Start&& start, Stop&& stop)
    noexcept(noexcept(
        std::views::iota(std::forward<Start>(start), std::forward<Stop>(stop))
    ))
    requires(requires{
        std::views::iota(std::forward<Start>(start), std::forward<Stop>(stop));
    })
{
    return (std::views::iota(std::forward<Start>(start), std::forward<Stop>(stop)));
}


/* Produce a simple range object that encapsulates a `start` and `stop` iterator as a
range adaptor.  This is equivalent to a `std::ranges::subrange()` call under the
hood. */
template <meta::iterator Start, meta::sentinel_for<Start> Stop>
[[nodiscard]] constexpr decltype(auto) range(Start&& start, Stop&& stop)
    noexcept(noexcept(
        std::ranges::subrange(std::forward<Start>(start), std::forward<Stop>(stop))
    ))
    requires(
        !requires{
            std::views::iota(std::forward<Start>(start), std::forward<Stop>(stop));
        } &&
        requires{
            std::ranges::subrange(std::forward<Start>(start), std::forward<Stop>(stop));
        }
    )
{
    return (std::ranges::subrange(std::forward<Start>(start), std::forward<Stop>(stop)));
}


#ifdef __cpp_lib_ranges_stride

    /* Produce a simple range object from `start` to `stop` in intervals of `step`,
    similar to Python's built-in `range()` operator.  This is equivalent to a
    `std::views::iota() | std::views::stride()` call under the hood. */
    template <typename Start, typename Stop, typename Step>
    [[nodiscard]] constexpr decltype(auto) range(Start&& start, Stop&& stop, Step&& step)
        noexcept(noexcept(
            std::views::iota(std::forward<Start>(start), std::forward<Stop>(stop)) |
            std::views::stride(std::forward<Step>(step))
        ))
        requires(requires{
            std::views::iota(std::forward<Start>(start), std::forward<Stop>(stop)) |
            std::views::stride(std::forward<Step>(step));
        })
    {
        return (std::views::iota(start, stop) | std::views::stride(step));
    }

    /* Produce a simple range object that encapsulates a `start` and `stop` iterator
    with a stride of `step` as a range adaptor.  This is equivalent to a
    `std::ranges::subrange() | std::views::stride()` call under the hood. */
    template <meta::iterator Start, meta::sentinel_for<Start> Stop, typename Step>
    [[nodiscard]] constexpr decltype(auto) range(
        Start&& start,
        Stop&& stop,
        Step&& step
    )
        noexcept(noexcept(
            std::ranges::subrange(std::forward<Start>(start), std::forward<Stop>(stop)) |
            std::views::stride(std::forward<Step>(step))
        ))
        requires(
            !requires{
                std::views::iota(std::forward<Start>(start), std::forward<Stop>(stop)) |
                std::views::stride(std::forward<Step>(step));
            } &&
            requires{
                std::ranges::subrange(std::forward<Start>(start), std::forward<Stop>(stop)) |
                std::views::stride(std::forward<Step>(step));
            }
        )
    {
        return (std::ranges::subrange(start, stop) | std::views::stride(step));
    }

#endif


/* Produce a view over a reverse iterable range that can be used in range-based for
loops.  This is equivalent to a `std::views::reverse()` call under the hood. */
template <meta::reverse_iterable T>
[[nodiscard]] constexpr decltype(auto) reversed(T&& r)
    noexcept(noexcept(std::views::reverse(std::views::all(std::forward<T>(r)))))
    requires(requires{std::views::reverse(std::views::all(std::forward<T>(r)));})
{
    return (std::views::reverse(std::views::all(std::forward<T>(r))));
}


#ifdef __cpp_lib_ranges_enumerate

    /* Produce a view over a given range that yields tuples consisting of each item's
    index and ordinary value_type.  This is equivalent to a `std::views::enumerate()` call
    under the hood, but is easier to remember, and closer to Python syntax. */
    template <meta::iterable T>
    [[nodiscard]] constexpr decltype(auto) enumerate(T&& r)
        noexcept(noexcept(std::views::enumerate(std::views::all(std::forward<T>(r)))))
        requires(requires{std::views::enumerate(std::views::all(std::forward<T>(r)));})
    {
        return (std::views::enumerate(std::views::all(std::forward<T>(r))));
    }

#endif


/* Combine several ranges into a view that yields tuple-like values consisting of the
`i` th element of each range.  This is equivalent to a `std::views::zip()` call under
the hood. */
template <meta::iterable... Ts>
[[nodiscard]] constexpr decltype(auto) zip(Ts&&... rs)
    noexcept(noexcept(std::views::zip(std::views::all(std::forward<Ts>(rs))...)))
    requires(requires{std::views::zip(std::views::all(std::forward<Ts>(rs))...);})
{
    return (std::views::zip(std::views::all(std::forward<Ts>(rs))...));
}


/* Returns true if and only if the predicate function returns true for one or more of
the arguments.  The predicate must be default-constructible and invocable with
the argument types.  It defaults to a contextual boolean conversion, according to
the conversion semantics of the argument types. */
template <
    meta::default_constructible F = impl::ExplicitConvertTo<bool>,
    typename... Args
>
    requires (sizeof...(Args) > 1)
[[nodiscard]] constexpr bool any(Args&&... args)
    noexcept(((
        meta::nothrow::callable<F, Args> &&
        meta::nothrow::explicitly_convertible_to<
            meta::call_type<F, Args>,
            bool
        >
    ) && ...))
    requires(((
        meta::callable<F, Args> &&
        meta::explicitly_convertible_to<
            meta::call_type<F, Args>,
            bool
        >
    ) && ...))
{
    return impl::truthy<Args...>::any(F{}, std::forward<Args>(args)...);
}


/* Returns true if and only if the predicate function returns true for one or more
elements of a tuple-like container.  The predicate must be default-constructible and
invocable with the tuple's element types.  It defaults to a contextual boolean
conversion, according to the conversion semantics of the element types. */
template <
    meta::default_constructible F = impl::ExplicitConvertTo<bool>,
    meta::tuple_like T
>
    requires (!meta::iterable<T>)
[[nodiscard]] constexpr bool any(T&& t)
    noexcept(noexcept(meta::tuple_types<T>::template eval<impl::truthy>::any(
        F{},
        std::forward<T>(t)
    )))
    requires(requires{meta::tuple_types<T>::template eval<impl::truthy>::any(
        F{},
        std::forward<T>(t)
    );})
{
    return meta::tuple_types<T>::template eval<impl::truthy>::any(
        F{},
        std::forward<T>(t)
    );;
}


/* Returns true if and only if the predicate function returns true for one ore more
elements of a range.  The predicate must be default-constructible and invocable with
the range's yield type.  It defaults to a contextual boolean conversion, according to
the conversion semantics of the yield type.  This is equivalent to a
`std::ranges::any_of()` call under the hood. */
template <
    meta::default_constructible F = impl::ExplicitConvertTo<bool>,
    meta::iterable T
>
    requires (
        meta::callable<F, meta::yield_type<T>> &&
        meta::explicitly_convertible_to<meta::call_type<F, meta::yield_type<T>>, bool>
    )
[[nodiscard]] constexpr bool any(T&& r)
    noexcept(noexcept(std::ranges::any_of(std::forward<T>(r), F{})))
    requires(requires{std::ranges::any_of(std::forward<T>(r), F{});})
{
    return std::ranges::any_of(std::forward<T>(r), F{});
}


/* Returns true if and only if the predicate function returns true for all of the
arguments.  The predicate must be default-constructible and invocable with the argument
types.  It defaults to a contextual boolean conversion, according to the conversion
semantics of the argument types. */
template <
    meta::default_constructible F = impl::ExplicitConvertTo<bool>,
    typename... Args
>
    requires (sizeof...(Args) > 1)
[[nodiscard]] constexpr bool all(Args&&... args)
    noexcept(((
        meta::nothrow::callable<F, Args> &&
        meta::nothrow::explicitly_convertible_to<
            meta::call_type<F, Args>,
            bool
        >
    ) && ...))
    requires(((
        meta::callable<F, Args> &&
        meta::explicitly_convertible_to<
            meta::call_type<F, Args>,
            bool
        >
    ) && ...))
{
    return impl::truthy<Args...>::all(F{}, std::forward<Args>(args)...);
}


/* Returns true if and only if the predicate function returns true for all elements of
a tuple-like container.  The predicate must be default-constructible and invocable with
the tuple's element types.  It defaults to a contextual boolean conversion, according
to the conversion semantics of the element types. */
template <
    meta::default_constructible F = impl::ExplicitConvertTo<bool>,
    meta::tuple_like T
>
    requires (!meta::iterable<T>)
[[nodiscard]] constexpr bool all(T&& t)
    noexcept(noexcept(meta::tuple_types<T>::template eval<impl::truthy>::all(
        F{},
        std::forward<T>(t)
    )))
    requires(requires{meta::tuple_types<T>::template eval<impl::truthy>::all(
        F{},
        std::forward<T>(t)
    );})
{
    return meta::tuple_types<T>::template eval<impl::truthy>::all(
        F{},
        std::forward<T>(t)
    );
}


/* Returns true if and only if the predicate function returns true for all elements of
a range.  The predicate must be default-constructible and invocable with the range's
yield type.  It defaults to a contextual boolean conversion, according to the
conversion semantics of the yield type.  This is equivalent to a
`std::ranges::all_of()` call under the hood. */
template <
    meta::default_constructible F = impl::ExplicitConvertTo<bool>,
    meta::iterable T
>
    requires (
        meta::callable<F, meta::yield_type<T>> &&
        meta::explicitly_convertible_to<meta::call_type<F, meta::yield_type<T>>, bool>
    )
[[nodiscard]] constexpr bool all(T&& r)
    noexcept(noexcept(std::ranges::all_of(std::forward<T>(r), F{})))
    requires(requires{std::ranges::all_of(std::forward<T>(r), F{});})
{
    return std::ranges::all_of(std::forward<T>(r), F{});
}


/* Apply a pairwise reduction function over the arguments from left to right, returning
the accumulated result.  Formally evaluates to a recursive call chain of the form
`F(F(F(F(x_1, x_2), x_3), ...), x_n)`, where `F` is the reduction function and `x_i`
are the individual arguments.  The return type is deduced as the common type
between each invocation, assuming one exists.

This is effectively a generalization of the Python standard library `min()`, `max()`,
`sum()`, and similar functions, which describe specializations of this method for
particular reduction functions.  User-defined reductions can be provided as a template
parameter to inject custom behavior, as long as it is default constructible and
invocable with each pair of arguments.  The algorithm will fail to compile if any of
these requirements are not met. */
template <meta::default_constructible F, typename... Ts>
    requires (sizeof...(Ts) > 1)
[[nodiscard]] constexpr decltype(auto) fold_left(Ts&&... args)
    noexcept(impl::fold_left<F, Ts...>::nothrow)
    requires(impl::fold_left<F, Ts...>::enable)
{
    return (impl::_fold_left<Ts...>{}(F{}, std::forward<Ts>(args)...));
}


/* Apply a pairwise reduction function over a non-empty, tuple-like container that is
indexable via `get<I>()` (as a member method, ADL function, or `std::get<I>()`) from
left to right, returning the accumulated result.  Formally evaluates to a recursive
call chain of the form `F(F(F(F(x_1, x_2), x_3), ...), x_n)`, where `F` is the
reduction function and `x_i` are the tuple elements.  The return type deduces to the
common type between each invocation, assuming one exists.

This is effectively a generalization of the Python standard library `min()`, `max()`,
`sum()`, and similar functions, which describe specializations of this method for
particular reduction functions.  User-defined reductions can be provided as a template
parameter to inject custom behavior, as long as it is default constructible and
invocable with each pair of elements.  The algorithm will fail to compile if any of
these requirements are not met. */
template <meta::default_constructible F, meta::tuple_like T>
    requires (meta::tuple_size<T> > 0)
[[nodiscard]] constexpr decltype(auto) fold_left(T&& t)
    noexcept(meta::nothrow::apply<
        typename meta::tuple_types<T>::template eval<impl::_fold_left>,
        F,
        T
    >)
    requires(meta::apply<
        typename meta::tuple_types<T>::template eval<impl::_fold_left>,
        F,
        T
    >)
{
    return (apply(
        typename meta::tuple_types<T>::template eval<impl::_fold_left>{},
        F{},
        std::forward<T>(t)
    ));
}


/* Apply a pairwise reduction function over an iterable range from left to right,
returning the accumulated result.  Formally evaluates to a recursive call chain of the
form `F(F(F(F(x_1, x_2), x_3), ...), x_n)`, where `F` is the reduction function and
`x_i` are the elements of the range.  The return type deduces to `Optional<T>`, where
`T` is the common type between each invocation, assuming one exists.  The empty state
corresponds to an empty range, which cannot be reduced.

This is effectively a generalization of the Python standard library `min()`, `max()`,
`sum()`, and similar functions, which describe specializations of this method for
particular reduction functions.  User-defined reductions can be provided as a template
parameter to inject custom behavior, as long as it is default constructible and
invocable with each pair of elements.  The algorithm will fail to compile if any of
these requirements are not met. */
template <meta::default_constructible F, meta::iterable T>
    requires (!meta::tuple_like<T>)
[[nodiscard]] constexpr auto fold_left(T&& r)
    noexcept(
        noexcept(bool(std::ranges::begin(r) == std::ranges::end(r))) &&
        noexcept(Optional<decltype(std::ranges::fold_left(
            std::ranges::begin(r),
            std::ranges::end(r),
            *std::ranges::begin(r),
            F{}
        ))>(std::ranges::fold_left(
            std::ranges::begin(r),
            std::ranges::end(r),
            *std::ranges::begin(r),
            F{}
        )))
    )
    -> Optional<decltype(std::ranges::fold_left(
        std::ranges::begin(r),
        std::ranges::end(r),
        *std::ranges::begin(r),
        F{}
    ))>
    requires(requires{
        { std::ranges::begin(r) == std::ranges::end(r) }
            -> meta::explicitly_convertible_to<bool>;
        { std::ranges::fold_left(
            std::ranges::begin(r),
            std::ranges::end(r),
            *std::ranges::begin(r),
            F{}
        ) } -> meta::convertible_to<
            Optional<decltype(std::ranges::fold_left(
                std::ranges::begin(r),
                std::ranges::end(r),
                *std::ranges::begin(r),
                F{}
            ))>
        >;
    })
{
    auto it = std::ranges::begin(r);
    auto end = std::ranges::end(r);
    if (it == end) {
        return None;
    }
    return [](auto&& init, auto& it, auto& end) {
        ++it;
        return std::ranges::fold_left(
            it,
            end,
            std::forward<decltype(init)>(init),
            F{}
        );
    }(*it, it, end);
}


/* Apply a pairwise reduction function over the arguments from left to right, returning
the accumulated result.  Formally evaluates to a recursive call chain of the form
`F(x_1, F(x_2, F(..., F(x_n-1, x_n)))`, where `F` is the reduction function and `x_i`
are the individual arguments.  The return type is deduced as the common type
between each invocation, assuming one exists.

This is effectively a generalization of the Python standard library `min()`, `max()`,
`sum()`, and similar functions, which describe specializations of this method for
particular reduction functions.  User-defined reductions can be provided as a template
parameter to inject custom behavior, as long as it is default constructible and
invocable with each pair of arguments.  The algorithm will fail to compile if any of
these requirements are not met. */
template <meta::default_constructible F, typename... Ts>
    requires (sizeof...(Ts) > 1)
[[nodiscard]] constexpr decltype(auto) fold_right(Ts&&... args)
    noexcept(impl::fold_right<F, Ts...>::nothrow)
    requires(impl::fold_right<F, Ts...>::enable)
{
    return (impl::_fold_right<Ts...>{}(F{}, std::forward<Ts>(args)...));
}


/* Apply a pairwise reduction function over a non-empty, tuple-like container that is
indexable via `get<I>()` (as a member method, ADL function, or `std::get<I>()`) from
left to right, returning the accumulated result.  Formally evaluates to a recursive
call chain of the form `F(x_1, F(x_2, F(..., F(x_n-1, x_n)))`, where `F` is the
reduction function and `x_i` are the tuple elements.  The return type deduces to the
common type between each invocation, assuming one exists.

This is effectively a generalization of the Python standard library `min()`, `max()`,
`sum()`, and similar functions, which describe specializations of this method for
particular reduction functions.  User-defined reductions can be provided as a template
parameter to inject custom behavior, as long as it is default constructible and
invocable with each pair of elements.  The algorithm will fail to compile if any of
these requirements are not met. */
template <meta::default_constructible F, meta::tuple_like T>
    requires (meta::tuple_size<T> > 0)
[[nodiscard]] constexpr decltype(auto) fold_right(T&& t)
    noexcept(meta::nothrow::apply<
        typename meta::tuple_types<T>::template eval<impl::_fold_right>,
        F,
        T
    >)
    requires(meta::apply<
        typename meta::tuple_types<T>::template eval<impl::_fold_right>,
        F,
        T
    >)
{
    return (apply(
        typename meta::tuple_types<T>::template eval<impl::_fold_right>{},
        F{},
        std::forward<T>(t)
    ));
}


#ifdef __cpp_lib_ranges_fold

    /* Apply a pairwise reduction function over an iterable range from left to right,
    returning the accumulated result.  Formally evaluates to a recursive call chain of
    the form `F(x_1, F(x_2, F(..., F(x_n-1, x_n)))`, where `F` is the reduction
    function and `x_i` are the elements of the range.  The return type deduces to
    `Optional<T>`, where `T` is the common type between each invocation, assuming one
    exists.  The empty state corresponds to an empty range, which cannot be reduced.

    This is effectively a generalization of the Python standard library `min()`,
    `max()`, `sum()`, and similar functions, which describe specializations of this
    method for particular reduction functions.  User-defined reductions can be provided
    as a template parameter to inject custom behavior, as long as it is default
    constructible and invocable with each pair of elements.  The algorithm will fail to
    compile if any of these requirements are not met. */
    template <meta::default_constructible F, meta::iterable T>
        requires (!meta::tuple_like<T>)
    [[nodiscard]] constexpr auto fold_right(T&& r)
        noexcept(
            noexcept(bool(std::ranges::begin(r) == std::ranges::end(r))) &&
            noexcept(Optional<decltype(std::ranges::fold_right(
                std::ranges::begin(r),
                std::ranges::end(r),
                *std::ranges::begin(r),
                F{}
            ))>(std::ranges::fold_right(
                std::ranges::begin(r),
                std::ranges::end(r),
                *std::ranges::begin(r),
                F{}
            )))
        )
        -> Optional<decltype(std::ranges::fold_right(
            std::ranges::begin(r),
            std::ranges::end(r),
            *std::ranges::begin(r),
            F{}
        ))>
        requires(requires{
            { std::ranges::begin(r) == std::ranges::end(r) }
                -> meta::explicitly_convertible_to<bool>;
            { std::ranges::fold_right(
                std::ranges::begin(r),
                std::ranges::end(r),
                *std::ranges::begin(r),
                F{}
            ) } -> meta::convertible_to<
                Optional<decltype(std::ranges::fold_right(
                    std::ranges::begin(r),
                    std::ranges::end(r),
                    *std::ranges::begin(r),
                    F{}
                ))>
            >;
        })
    {
        auto it = std::ranges::begin(r);
        auto end = std::ranges::end(r);
        if (it == end) {
            return None;
        }
        return [](auto&& init, auto& it, auto& end) {
            ++it;
            return std::ranges::fold_right(
                it,
                end,
                std::forward<decltype(init)>(init),
                F{}
            );
        }(*it, it, end);
    }

#endif


/* Left-fold to obtain the minimum value over a sequence of arguments.  This is similar
to a `fold_left<F>(...)` call, except that `F` is expected to be a boolean predicate
corresponding to a less-than comparison between each pair of arguments, and conversion
to a common type is deferred until the end of the fold.  The return type is deduced as
the common type for all elements, assuming such a type exists.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of arguments, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, typename... Ts>
    requires (sizeof...(Ts) > 1)
[[nodiscard]] constexpr decltype(auto) min(Ts&&... args)
    noexcept(impl::min<F, Ts...>::nothrow)
    requires(impl::min<F, Ts...>::enable)
{
    return (impl::_min<Ts...>{}(F{}, std::forward<Ts>(args)...));
}


/* Left-fold to obtain the minimum value within a non-empty, tuple-like container that
is indexable via `get<I>()` (as a member method, ADL function, or `std::get<I>()`).
This is similar to a `fold_left<F>(...)` call, except that `F` is expected to be a
boolean predicate corresponding to a less-than comparison between each pair of
elements, and conversion to a common type is deferred until the end of the fold.  The
return type is deduced as the common type for all elements, assuming such a type
exists.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of elements, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, meta::tuple_like T>
    requires (meta::tuple_size<T> > 0)
[[nodiscard]] constexpr decltype(auto) min(T&& t)
    noexcept(meta::nothrow::apply<
        typename meta::tuple_types<T>::template eval<impl::_min>,
        F,
        T
    >)
    requires(meta::apply<
        typename meta::tuple_types<T>::template eval<impl::_min>,
        F,
        T
    >)
{
    return (apply(
        typename meta::tuple_types<T>::template eval<impl::_min>{},
        F{},
        std::forward<T>(t)
    ));
}


/* Left-fold to obtain the minimum value within an iterable range.  This is similar to
a `fold_left<F>(...)` call, except that `F` is expected to be a boolean predicate
corresponding to a less-than comparison between each pair of elements, and conversion
to a common type is deferred until the end of the fold.  The return type deduces to
`Optional<T>`, where `T` is the common type between each invocation, assuming one
exists.  The empty state corresponds to an empty range, which cannot be reduced.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of elements, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, meta::iterable T>
    requires (!meta::tuple_like<T>)
[[nodiscard]] constexpr auto min(T&& r)
    noexcept(
        noexcept(bool(std::ranges::empty(r))) &&
        noexcept(Optional<decltype(std::ranges::min(std::forward<T>(r), F{}))>(
            std::ranges::min(std::forward<T>(r), F{})
        ))
    )
    -> Optional<decltype(std::ranges::min(std::forward<T>(r), F{}))>
    requires(requires{
        { std::ranges::empty(r) } -> meta::explicitly_convertible_to<bool>;
        { std::ranges::min(std::forward<T>(r), F{}) } -> meta::convertible_to<
            Optional<decltype(std::ranges::min(std::forward<T>(r), F{}))>
        >;
    })
{
    if (std::ranges::empty(r)) {
        return None;
    }
    return std::ranges::min(std::forward<T>(r), F{});
}


/* Left-fold to obtain the maximum value over a sequence of arguments.  This is similar
to a `fold_left<F>(...)` call, except that `F` is expected to be a boolean predicate
corresponding to a less-than comparison between each pair of arguments, and conversion
to a common type is deferred until the end of the fold.  The return type is deduced as
the common type for all elements, assuming such a type exists.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of arguments, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, typename... Ts>
    requires (sizeof...(Ts) > 1)
[[nodiscard]] constexpr decltype(auto) max(Ts&&... args)
    noexcept(impl::max<F, Ts...>::nothrow)
    requires(impl::max<F, Ts...>::enable)
{
    return (impl::_max<Ts...>{}(F{}, std::forward<Ts>(args)...));
}


/* Left-fold to obtain the maximum value within a non-empty, tuple-like container that
is indexable via `get<I>()` (as a member method, ADL function, or `std::get<I>()`).
This is similar to a `fold_left<F>(...)` call, except that `F` is expected to be a
boolean predicate corresponding to a less-than comparison between each pair of
elements, and conversion to a common type is deferred until the end of the fold.  The
return type is deduced as the common type for all elements, assuming such a type
exists.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of elements, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, meta::tuple_like T>
    requires (meta::tuple_size<T> > 0)
[[nodiscard]] constexpr decltype(auto) max(T&& t)
    noexcept(meta::nothrow::apply<
        typename meta::tuple_types<T>::template eval<impl::_max>,
        F,
        T
    >)
    requires(meta::apply<
        typename meta::tuple_types<T>::template eval<impl::_max>,
        F,
        T
    >)
{
    return (apply(
        typename meta::tuple_types<T>::template eval<impl::_max>{},
        F{},
        std::forward<T>(t)
    ));
}


/* Left-fold to obtain the maximum value within an iterable range.  This is similar to
a `fold_left<F>(...)` call, except that `F` is expected to be a boolean predicate
corresponding to a less-than comparison between each pair of elements, and conversion
to a common type is deferred until the end of the fold.  The return type deduces to
`Optional<T>`, where `T` is the common type between each invocation, assuming one
exists.  The empty state corresponds to an empty range, which cannot be reduced.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of elements, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, meta::iterable T>
    requires (!meta::tuple_like<T>)
[[nodiscard]] constexpr auto max(T&& r)
    noexcept(
        noexcept(bool(std::ranges::empty(r))) &&
        noexcept(Optional<decltype(std::ranges::max(std::forward<T>(r), F{}))>(
            std::ranges::max(std::forward<T>(r), F{})
        ))
    )
    -> Optional<decltype(std::ranges::max(std::forward<T>(r), F{}))>
    requires(requires{
        { std::ranges::empty(r) } -> meta::explicitly_convertible_to<bool>;
        { std::ranges::max(std::forward<T>(r), F{}) } -> meta::convertible_to<
            Optional<decltype(std::ranges::max(std::forward<T>(r), F{}))>
        >;
    })
{
    if (std::ranges::empty(r)) {
        return None;
    }
    return std::ranges::max(std::forward<T>(r), F{});
}


/* Left-fold to obtain the minimum and maximum values over a sequence of arguments
simultaneously.  This is similar to a `fold_left<F>(...)` call, except that `F` is
expected to be a boolean predicate corresponding to a less-than comparison between each
pair of arguments, and conversion to a common type is deferred until the end of the
fold.  The return type is `std::pair<T, T>`, where `T` is the common type for all
elements, assuming such a type exists.  The first element of the pair is the minimum
value, and the second element is the maximum value.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of arguments, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, typename... Ts>
    requires (sizeof...(Ts) > 1)
[[nodiscard]] constexpr decltype(auto) minmax(Ts&&... args)
    noexcept(impl::minmax<F, Ts...>::nothrow)
    requires(impl::minmax<F, Ts...>::enable)
{
    return (impl::_minmax<Ts...>{}(
        F{},
        meta::unpack_arg<0>(std::forward<Ts>(args)...),
        std::forward<Ts>(args)...
    ));
}


/* Left-fold to obtain the minimum and maximum values within a non-empty, tuple-like
container that is indexable via `get<I>()` (as a member method, ADL function, or
`std::get<I>()`).  This is similar to a `fold_left<F>(...)` call, except that `F` is
expected to be a boolean predicate corresponding to a less-than comparison between each
pair of elements, and conversion to a common type is deferred until the end of the
fold.  The return type is `std::pair<T, T>`, where `T` is the common type for all
elements, assuming such a type exists.  The first element of the pair is the minimum
value, and the second element is the maximum value.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of elements, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, meta::tuple_like T>
    requires (meta::tuple_size<T> > 0)
[[nodiscard]] constexpr decltype(auto) minmax(T&& t)
    noexcept(meta::nothrow::apply<
        typename meta::tuple_types<T>::template eval<impl::minmax_helper>,
        F,
        T
    >)
    requires(meta::apply<
        typename meta::tuple_types<T>::template eval<impl::minmax_helper>,
        F,
        T
    >)
{
    return (apply(
        typename meta::tuple_types<T>::template eval<impl::minmax_helper>{},
        F{},
        std::forward<T>(t)
    ));
}


/* Left-fold to obtain the minimum and maximum values within an iterable range.  This
is similar to a `fold_left<F>(...)` call, except that `F` is expected to be a boolean
predicate corresponding to a less-than comparison between each pair of elements, and
conversion to a common type is deferred until the end of the fold.  The return type is
`Optional<std::pair<T, T>>`, where `T` is the common type for all elements, assuming
such a type exists.  The first element of the pair is the minimum value, and the second
element is the maximum value.  The empty state corresponds to an empty range, which
cannot be reduced.

User-defined predicates can be provided to customize the comparison as long as they are
default constructible, invocable with each pair of elements, and return a contextually
convertible boolean value.  The function will fail to compile if any of these
requirements are not met. */
template <meta::default_constructible F = impl::Less, meta::iterable T>
    requires (!meta::tuple_like<T>)
[[nodiscard]] constexpr auto minmax(T&& r)
    noexcept(
        noexcept(bool(std::ranges::empty(r))) &&
        noexcept(Optional<decltype(std::ranges::minmax(std::forward<T>(r), F{}))>(
            std::ranges::minmax(std::forward<T>(r), F{})
        ))
    )
    -> Optional<decltype(std::ranges::minmax(std::forward<T>(r), F{}))>
    requires(requires{
        { std::ranges::empty(r) } -> meta::explicitly_convertible_to<bool>;
        { std::ranges::minmax(std::forward<T>(r), F{}) } -> meta::convertible_to<
            Optional<decltype(std::ranges::minmax(std::forward<T>(r), F{}))>
        >;
    })
{
    if (std::ranges::empty(r)) {
        return None;
    }
    return std::ranges::minmax(std::forward<T>(r), F{});
}


namespace meta {

    template <typename Less, typename Begin, typename End>
    concept iter_sortable =
        meta::iterator<Begin> &&
        meta::sentinel_for<End, Begin> &&
        meta::copyable<Begin> &&
        meta::output_iterator<Begin, meta::as_rvalue<meta::dereference_type<Begin>>> &&
        meta::movable<meta::remove_reference<meta::dereference_type<Begin>>> &&
        meta::move_assignable<meta::remove_reference<meta::dereference_type<Begin>>> &&
        meta::destructible<meta::remove_reference<meta::dereference_type<Begin>>> &&
        (
            (
                meta::member_object_of<
                    Less,
                    meta::remove_reference<meta::dereference_type<Begin>>
                > &&
                meta::has_lt<meta::remove_member<Less>, meta::remove_member<Less>>
            ) || (
                meta::member_function_of<
                    Less,
                    meta::remove_reference<meta::dereference_type<Begin>>
                > &&
                meta::callable<Less, meta::dereference_type<Begin>> &&
                meta::has_lt<
                    meta::call_type<Less, meta::dereference_type<Begin>>,
                    meta::call_type<Less, meta::dereference_type<Begin>>
                >
            ) || (
                !meta::member<Less> &&
                meta::callable<
                    meta::as_lvalue<Less>,
                    meta::dereference_type<Begin>,
                    meta::dereference_type<Begin>
                > &&
                meta::explicitly_convertible_to<
                    meta::call_type<
                        meta::as_lvalue<Less>,
                        meta::dereference_type<Begin>,
                        meta::dereference_type<Begin>
                    >,
                    bool
                >
            )
        );

    template <typename Less, typename Range>
    concept sortable =
        meta::iterable<Range> &&
        iter_sortable<Less, meta::begin_type<Range>, meta::end_type<Range>>;

}


namespace impl {

    /* A stable, adaptive, k-way merge sort algorithm for arbitrary input/output ranges
    based on work by Gelling, Nebel, Smith, and Wild ("Multiway Powersort", 2023),
    requiring O(n) scratch space for rotations.  A full description of the algorithm
    and its benefits can be found at:

        [1] https://www.wild-inter.net/publications/html/cawley-gelling-nebel-smith-wild-2023.pdf.html

    An earlier, 2-way version of this algorithm is currently in use as the default
    CPython sorting backend for the `sorted()` operator and `list.sort()` method as of
    Python 3.11.  A more complete introduction to that algorithm and how it relates to
    the newer 4-way version can be found in Munro, Wild ("Nearly-Optimal Mergesorts:
    Fast, Practical Sorting Methods That Optimally Adapt to Existing Runs", 2018):

        [2] https://www.wild-inter.net/publications/html/munro-wild-2018.pdf.html

    A full reference implementation for both of these algorithms is available at:

        [3] https://github.com/sebawild/powersort

    The k-way version presented here is adapted from the above implementations with the
    following changes:

        a)  The algorithm works on arbitrary ranges, not just random access iterators.
            If the iterator type does not support O(1) distance calculations, then a
            `std::ranges::distance()` call will be used to determine the initial size
            of the range.  All other iterator operations will be done in constant time.
        b)  A custom `less_than` predicate can be provided to the algorithm, which
            allows for sorting based on custom comparison functions, including
            lambdas, user-defined comparators, and pointers to members.
        c)  Merges are safe against exceptions thrown by the comparison function, and
            will attempt to transfer partially-sorted runs back into the output range
            via RAII.
        d)  Proper move semantics are used to transfer objects to and from the scratch
            space, instead of requiring the type to be default constructible and/or
            copyable.
        e)  The algorithm is generalized to arbitrary `k >= 2`, with a default value of
            4, in accordance with [2].  Higher `k` will asymptotically reduce the
            number of comparisons needed to sort the array by a factor of `log2(k)`, at
            the expense of deeper tournament trees.  There is likely an architecture-
            dependent sweet spot based on the size of the data and the cost of
            comparisons for a given type.  Further investigation is needed to determine
            the optimal value of `k` for a given situation, as well as possibly allow
            dynamic tuning based on the input data.
        f)  All tournament trees are swapped from winner trees to loser trees, which
            reduces branching in the inner loop and simplifies the implementation.
        g)  Sentinel values will be used by default if
            `std::numeric_limits<T>::has_infinity == true`, which maximizes performance
            as demonstrated in [2].

    Otherwise, the algorithm is designed to be a drop-in replacement for `std::sort`
    and `std::stable_sort`, and is generally competitive with or better than those
    algorithms, sometimes by a significant margin if any of the following conditions
    are true:

        1.  The data is already partially sorted, or is naturally ordered in
            ascending/descending runs.
        2.  Data movement and/or comparisons are expensive, such as for strings or
            user-defined types.
        3.  The data has a sentinel value, expressed as
            `std::numeric_limits<T>::infinity()`.

    NOTE: the `min_run` template parameter dictates the minimum run length under
    which insertion sort will be used to grow the run.  [2] sets this to a default of
    24, which is replicated here.  Like `k`, it can be tuned at compile time. */
    template <size_t k = 4, size_t min_run = 24> requires (k >= 2 && min_run > 0)
    struct powersort {
    private:
        template <typename Begin>
        using value_type = meta::remove_reference<meta::dereference_type<Begin>>;
        template <typename Begin>
        using pointer = meta::as_pointer<value_type<Begin>>;

        /* The less-than comparator may be given as either a boolean predicate function
        or a simple pointer to member, which will be wrapped in a boolean predicate. */
        template <typename Less> requires (!meta::rvalue<Less>)
        struct sort_by {
            Less member;

            constexpr bool operator()(auto&& l, auto&& r) const noexcept {
                if constexpr (meta::member_function<Less>) {
                    return ((l.*member)()) < ((r.*member)());
                } else {
                    return (l.*member) < (r.*member);
                }
            }

            template <meta::is<Less> L>
            static constexpr decltype(auto) fn(L&& less_than) noexcept {
                return (std::forward<L>(less_than));
            }

            template <meta::is<Less> L> requires (meta::member<Less>)
            static constexpr sort_by fn(L&& less_than)
                noexcept(meta::nothrow::convertible_to<L, Less>)
            {
                return {std::forward<L>(less_than)};
            }
        };

        /* Scratch space is allocated as a raw buffer using
        `std::allocator<T>::allocate()` and `std::allocator<T>::deallocate()  so as
        not to impose default constructibility on `value_type`, while still allowing
        the sort to be done at compile time via transient allocation. */
        template <typename Begin> requires (!meta::reference<Begin>)
        struct scratch {
            using value_type = powersort::value_type<Begin>;
            using pointer = powersort::pointer<Begin>;
            struct deleter {
                size_t size;
                constexpr void operator()(pointer p) noexcept {
                    std::allocator<value_type>{}.deallocate(p, size);
                }
            };
            using buffer = std::unique_ptr<value_type, deleter>;
            buffer data;

            constexpr scratch() noexcept = default;
            constexpr scratch(size_t size) : data(
                std::allocator<value_type>{}.allocate(size + k + 1),
                deleter{size + k + 1}
            ) {}

            constexpr void allocate(size_t size) {
                data = buffer{
                    std::allocator<value_type>{}.allocate(size + k + 1),
                    deleter{size + k + 1}
                };
            }
        };

        template <typename Begin, typename Less>
        struct run {
            using value_type = powersort::value_type<Begin>;
            using pointer = powersort::pointer<Begin>;
            Begin iter;  // iterator to start of run
            size_t start;  // first index of the run
            size_t stop = start;  // one past last index of the run
            size_t power = 0;

            /* Detect the next run beginning at `start` and not exceeding `size`.
            `iter` is an iterator to the start index, which will be advanced to
            the end of the detected run as an out parameter.

            If the run is strictly decreasing, then it will also be reversed in-place.
            If it is less than the minimum run length, then it will be grown to that
            length or to the end of the range using insertion sort.  After this method
            is called, `stop` will be an index one past the last element of the run.
            
            Forward-only iterators require the scratch space to be allocated early,
            so that it can be used for reversal and insertion sort rotations.  This can
            be avoided in the bidirectional case as an optimization. */
            constexpr void detect(
                Less& less_than,
                Begin& iter,
                size_t size,
                powersort::scratch<Begin>& scratch  // initialized to null
            )
                requires (!meta::bidirectional_iterator<Begin>)
            {
                if (stop < size && ++stop < size) {
                    Begin next = iter;
                    ++next;
                    if (less_than(*next, *iter)) {  // strictly decreasing
                        do {
                            ++iter;
                            ++next;
                        } while (++stop < size && less_than(*next, *iter));
                        ++iter;

                        // otherwise, if the iterator is forward-only, we have to do an
                        // O(2 * n) move into scratch space and then move back.
                        scratch.allocate(size);  // lazy initialization
                        pointer begin = scratch;
                        pointer end = scratch + (stop - start);
                        Begin i = this->iter;
                        while (begin < end) {
                            std::construct_at(begin++, std::move(*i++));
                        }
                        Begin j = this->iter;
                        while (end-- > scratch) {
                            *j = std::move(*(end));
                            destroy(end);
                            ++j;
                        }

                    } else {  // weakly increasing
                        do {
                            ++iter;
                            ++next;
                        } while (++stop < size && !less_than(*next, *iter));
                        ++iter;
                    }
                }

                if (stop == size) {
                    return;
                }
                if (scratch.data == nullptr) {
                    scratch.allocate(size);  // lazy initialization
                }

                // Grow the run to the minimum length using insertion sort.  If the
                // iterator is forward-only, then we have to scan the sorted portion
                // from left to right and move it into scratch space to do a proper
                // rotation.
                size_t limit = bertrand::min(start + min_run, size);
                while (stop < limit) {
                    // scan sorted portion for insertion point
                    Begin curr = this->iter;
                    size_t idx = start;
                    while (idx < stop) {
                        // stop at the first element that is strictly greater
                        // than the unsorted element
                        if (less_than(*iter, *curr)) {
                            // move subsequent elements into scratch space
                            Begin temp = curr;
                            pointer p = scratch;
                            pointer p2 = scratch + stop - idx;
                            while (p < p2) {
                                std::construct_at(
                                    p++,
                                    std::move(*temp++)
                                );
                            }

                            // move unsorted element to insertion point
                            *curr++ = std::move(*iter);

                            // move intervening elements back
                            p = scratch;
                            p2 = scratch + stop - idx;
                            while (p < p2) {
                                *curr++ = std::move(*p);
                                destroy(p);
                                ++p;
                            }
                            break;
                        }
                        ++curr;
                        ++idx;
                    }
                    ++iter;
                    ++stop;
                }
            }
        };

        template <meta::bidirectional_iterator Begin, typename Less>
        struct run<Begin, Less> {
            using value_type = powersort::value_type<Begin>;
            using pointer = powersort::pointer<Begin>;
            Begin iter;  // iterator to start of run
            size_t start;  // first index of the run
            size_t stop = start;  // one past last index of the run
            size_t power = 0;

            /* Detect the next run beginning at `start` and not exceeding `size`.
            `iter` is an iterator to the start index, which will be advanced to
            the end of the detected run as an out parameter.

            If the run is strictly decreasing, then it will also be reversed in-place.
            If it is less than the minimum run length, then it will be grown to that
            length or to the end of the range using insertion sort.  After this method
            is called, `stop` will be an index one past the last element of the run. */
            constexpr void detect(Less& less_than, Begin& iter, size_t size) {
                if (stop < size && ++stop < size) {
                    Begin next = iter;
                    ++next;
                    if (less_than(*next, *iter)) {  // strictly decreasing
                        do {
                            ++iter;
                            ++next;
                        } while (++stop < size && less_than(*next, *iter));
                        ++iter;

                        // if the iterator is bidirectional, then we can do an O(n / 2)
                        // pairwise swap
                        std::ranges::reverse(this->iter, iter);

                    } else {  // weakly increasing
                        do {
                            ++iter;
                            ++next;
                        } while (++stop < size && !less_than(*next, *iter));
                        ++iter;
                    }
                }

                // Grow the run to the minimum length using insertion sort.  If the
                // iterator is bidirectional, then we can rotate in-place from right to
                // left to avoid any extra allocations.
                size_t limit = bertrand::min(start + min_run, size);
                while (stop < limit) {
                    // if the unsorted element is less than the previous
                    // element, we need to rotate it into the correct position
                    Begin prev = iter;
                    --prev;
                    if (less_than(*iter, *prev)) {
                        size_t idx = stop;
                        Begin curr = iter;
                        value_type temp = std::move(*curr);

                        // rotate hole to the left until we find a proper
                        // insertion point.
                        while (true) {
                            *curr = std::move(*prev);
                            --curr;
                            try {
                                if (--idx == start) {
                                    break;
                                }
                                --prev;
                                if (!less_than(temp, *prev)) {
                                    break;  // found insertion point
                                }
                            } catch (...) {
                                *curr = std::move(temp);  // fill hole
                                throw;
                            }
                        };

                        // fill hole at insertion point
                        *curr = std::move(temp);
                    }
                    ++iter;
                    ++stop;
                }
            }
        };

        template <typename Begin, typename Less>
            requires (!meta::reference<Begin> && !meta::reference<Less>)
        struct merge_tree {
        private:
            using value_type = powersort::value_type<Begin>;
            using pointer = powersort::pointer<Begin>;
            using run = powersort::run<Begin, Less>;
            using numeric = std::numeric_limits<meta::unqualify<value_type>>;
            static constexpr size_t empty = std::numeric_limits<size_t>::max();

            static constexpr size_t ceil_logk(size_t n) noexcept {
                // fast path where `k` is a power of two
                if constexpr (impl::is_power2(k)) {
                    return (impl::log2(n - 1) / std::countr_zero(k)) + 1;
    
                // otherwise, we repeatedly multiply until we reach or exceed `n`,
                // which is O(log(n)), but negligible compared to the sort.
                } else {
                    size_t m = 0;
                    size_t product = 1;
                    while (product < n) {
                        ++m;
                        if (product > std::numeric_limits<size_t>::max() / k) {
                            break;  // k^m > any representable n
                        }
                        product *= k;
                    }
                    return m;
                }
            }

            static constexpr size_t get_power(
                size_t size,
                const run& curr,
                const run& next
            ) noexcept {
                /// NOTE: this implementation is taken straight from the reference.
                /// The only generalization is the use of `std::countl_zero` instead
                /// of compiler intrinsics, and converting to guaranteed 64-bit
                /// arithmetic for the shift.
                size_t l = curr.start + curr.stop;
                size_t r = next.start + next.stop;
                size_t a = (uint64_t(l) << 30) / size;
                size_t b = (uint64_t(r) << 30) / size;
                return ((std::countl_zero(a ^ b) - 1) >> 1) + 1;
            }

            /* Construction/destruction is done through `std::construct_at()` and
            `std::destroy_at()` so as to allow use in constexpr contexts compared to
            placement new and inplace destructor calls. */
            static constexpr void destroy(pointer p)
                noexcept(meta::nothrow::destructible<value_type>)
                requires(meta::destructible<value_type>)
            {
                if constexpr (!meta::trivially_destructible<value_type>) {
                    std::destroy_at(p);
                }
            }

            /* An exception-safe tournament tree generalized to arbitrary `N >= 2`.  If
            an error occurs during a comparison, then all runs will be transferred back
            into the output range in partially-sorted order via RAII. */
            template <size_t N, bool = numeric::has_infinity> requires (N >= 2)
            struct tournament_tree {
                static constexpr size_t M = N + (N % 2);
                Less& less_than;
                pointer scratch;
                Begin output;
                size_t size;
                std::array<pointer, M> begin;  // begin iterators for each run
                std::array<pointer, M> end;  // end iterators for each run
                std::array<size_t, M - 1> internal;  // internal nodes of tree
                size_t winner;  // leaf index of overall winner

                /// NOTE: the tree is represented as a loser tree, meaning the internal
                /// nodes store the leaf index of the losing run for that subtree, and
                /// the winner is bubbled up to the next level of the tree.  The root
                /// of the tree (runner-up) is always the first element of the
                /// `internal` buffer, and each subsequent level is compressed into the
                /// next 2^i elements, from left to right.  The last layer will be
                /// incomplete if `N` is not a power of two.

                template <meta::is<run>... Runs> requires (sizeof...(Runs) == N)
                constexpr tournament_tree(
                    Less& less_than,
                    pointer scratch,
                    Runs&... runs
                ) :
                    less_than(less_than),
                    scratch(scratch),
                    output(meta::unpack_arg<0>(runs...).iter),
                    size((0 + ... + (runs.stop - runs.start))),
                    begin(get_begin(std::make_index_sequence<N>{}, runs...)),
                    end(begin)
                {
                    construct(runs...);
                }

                /* Perform the merge. */
                constexpr void operator()() {
                    // Initialize the tournament tree
                    //                internal[0]               internal nodes store
                    //            /                \            losing leaf indices.
                    //      internal[1]          internal[2]    Leaf nodes store
                    //      /       \             /       \     scratch iterators
                    //         ...                   ...
                    // begin[0]   begin[1]   begin[2]   begin[3]   ...
                    for (size_t i = 0; i < M; ++i) {
                        winner = i;
                        size_t node = (M - 1) + winner;  // phantom leaf index
                        do {
                            size_t parent = (node - 1) / 2;  // direct parent
                            size_t loser = internal[parent];

                            // parent may be uninitialized, in which case the current
                            // node automatically loses
                            if (loser == empty) {
                                internal[parent] = winner;
                                break;  // no need to check ancestors
                            }

                            // otherwise, if the current winner loses against the
                            // parent, then we swap it and continue bubbling up
                            if (less_than(*begin[loser], *begin[winner])) {
                                internal[parent] = winner;
                                winner = loser;
                            }
                            node = parent;
                        } while (node > 0);
                    }

                    // merge runs according to tournament tree
                    for (size_t i = 0; i < size; ++i) {
                        // move the overall winner into the output range
                        *output++ = std::move(*begin[winner]);
                        destroy(begin[winner]++);

                        // bubble up next winner
                        size_t node = (M - 1) + winner;  // phantom leaf index
                        do {
                            size_t parent = (node - 1) / 2;  // direct parent
                            size_t loser = internal[parent];

                            // if next value in winner loses against the parent, then
                            // we swap them and continue bubbling up
                            if (less_than(*begin[loser], *begin[winner])) {
                                internal[parent] = winner;
                                winner = loser;
                            }
                            node = parent;
                        } while (node > 0);
                    }
                }

                /* If an error occurs during comparison, attempt to move the
                unprocessed portions of each run back into the output range and destroy
                sentinels. */
                constexpr ~tournament_tree() {
                    for (size_t i = 0; i < begin.size(); ++i) {
                        while (begin[i] != end[i]) {
                            *output++ = std::move(*begin[i]);
                            destroy(begin[i]);
                            ++begin[i];
                        }
                        destroy(end[i]);
                    }
                }

            private:

                /* If the number of runs to merge is odd, then the tournament tree will
                have exactly one unbalanced node at the end of the array.  In order to
                correct for this, we insert a phantom sentinel to ensure that every
                internal node has precisely two children.  The sentinel branch will
                just never be chosen. */
                template <size_t... Is, typename... Runs>
                constexpr std::array<pointer, M> get_begin(
                    std::index_sequence<Is...>,
                    Runs&... runs
                ) const {
                    run& first = meta::unpack_arg<0>(runs...);
                    if constexpr (N % 2) {
                        return {
                            (scratch + (runs.start - first.start) + Is)...,
                            (scratch + size + N)  // extra sentinel
                        };
                    } else {
                        return {(scratch + (runs.start - first.start) + Is)...};
                    }
                }

                /* move all runs into scratch space.  Afterwards, the end iterators
                will point to the sentinel values for each run */
                template <size_t I = 0, typename... Runs>
                constexpr void construct(Runs&... runs) {
                    if constexpr (I < M - 1) {
                        internal[I] = empty;  // nodes are initialized to empty value
                    }

                    if constexpr (I < N) {
                        run& r = meta::unpack_arg<I>(runs...);
                        for (size_t i = r.start; i < r.stop; ++i) {
                            std::construct_at(
                                end[I]++,
                                std::move(*r.iter++)
                            );
                        }
                        std::construct_at(
                            end[I],
                            numeric::infinity()
                        );
                        construct<I + 1>(runs...);

                    // if `N` is odd, then we have to insert an additional
                    // sentinel at the end of the scratch space to give each
                    // internal node exactly two children
                    } else if constexpr (I < M) {
                        std::construct_at(
                            end[I],
                            numeric::infinity()
                        );
                    }
                }

            };

            /* A specialized tournament tree for when the underlying type does not
            have a +inf sentinel value to guard comparisons.  Instead, this uses extra
            boundary checks and merges in stages, where `N` steadily decreases as runs
            are fully consumed. */
            template <size_t N> requires (N >= 2)
            struct tournament_tree<N, false> {
                Less& less_than;
                pointer scratch;
                Begin output;
                size_t size;
                std::array<pointer, N> begin;  // begin iterators for each run
                std::array<pointer, N> end;  // end iterators for each run
                std::array<size_t, N - 1> internal;  // internal nodes of tree
                size_t winner;  // leaf index of overall winner
                size_t smallest = std::numeric_limits<size_t>::max();  // length of smallest non-empty run

                /// NOTE: this specialization plays tournaments in `N - 1` distinct
                /// stages, where each stage ends when `smallest` reaches zero.  At
                /// At that point, empty runs are removed, and a smaller tournament
                /// tree is constructed with the remaining runs.  This continues until
                /// `N == 2`, in which case we proceed as for a binary merge.

                /// NOTE: because we can't pad the runs with an extra sentinel if N is
                /// odd, the last node in the `internal` array may be unbalanced, with
                /// only a single child.  This is mitigated by simply omitting that
                /// node and causing the leaf that would have been its only child to
                /// skip it during initialization/update of the tournament tree.

                template <meta::is<run>... Runs> requires (sizeof...(Runs) == N)
                constexpr tournament_tree(
                    Less& less_than,
                    pointer scratch,
                    Runs&... runs
                ) :
                    less_than(less_than),
                    scratch(scratch),
                    output(meta::unpack_arg<0>(runs...).iter),
                    size((0 + ... + (runs.stop - runs.start))),
                    begin{(scratch + (runs.start - meta::unpack_arg<0>(runs...).start))...},
                    end(begin)
                {
                    construct(runs...);
                }

                /* Perform the merge for stage `M`, where `M` indicates the current
                size of the tournament tree. */
                template <size_t M = N>
                constexpr void operator()() {
                    if constexpr (M > 2) {
                        initialize<M>();  // build tournament tree for this stage
                        merge<M>();  // continue until a run is exhausted
                        advance<M>();  // pop empty run for next stage
                        operator()<M - 1>();  // recur

                    // finish with a binary merge
                    } else {
                        while (begin[0] != end[0] && begin[1] != end[1]) {
                            bool less = static_cast<bool>(less_than(*begin[1], *begin[0]));
                            *output++ = std::move(*begin[less]);
                            destroy(begin[less]);
                            ++begin[less];
                        }
                        while (begin[0] != end[0]) {
                            *output++ = std::move(*begin[0]);
                            destroy(begin[0]);
                            ++begin[0];
                        }
                        while (begin[1] != end[1]) {
                            *output++ = std::move(*begin[1]);
                            destroy(begin[1]);
                            ++begin[1];
                        }
                    }
                }

                constexpr ~tournament_tree() {
                    for (size_t i = 0; i < begin.size(); ++i) {
                        while (begin[i] != end[i]) {
                            *output = std::move(*begin[i]);
                            ++output;
                            destroy(begin[i]);
                            ++begin[i];
                        }
                    }
                }

            private:

                /* Move all runs into scratch space, without any extra sentinels.
                Record the minimum length of each run for the the first stage. */
                template <size_t I = 0, typename... Runs>
                constexpr void construct(Runs&... runs) {
                    if constexpr (I < N) {
                        if constexpr (I < N - 1) {
                            internal[I] = empty;  // internal nodes start empty
                        }
                        run& r = meta::unpack_arg<I>(runs...);
                        for (size_t i = r.start; i < r.stop; ++i) {
                            std::construct_at(
                                end[I]++,
                                std::move(*r.iter++)
                            );
                        }
                        smallest = bertrand::min(smallest, r.stop - r.start);
                        construct<I + 1>(runs...);
                    }
                }

                /* Regenerate the tournament tree for the next stage. */
                template <size_t M>
                constexpr void initialize() {
                    for (size_t i = 0; i < M; ++i) {
                        winner = i;
                        size_t node = (M - 1) + winner;  // phantom leaf index
                        do {
                            size_t parent = (node - 1) / 2;  // direct parent
                            size_t loser = internal[parent];

                            // parent may be empty, in which case the current winner
                            // automatically loses
                            if (loser == empty) {
                                internal[parent] = winner;
                                break;  // no need to check ancestors
                            }

                            // otherwise, if the current winner loses against the
                            // parent, then we swap it and continue bubbling up
                            if (less_than(*begin[loser], *begin[winner])) {
                                internal[parent] = winner;
                                winner = loser;
                            }
                            node = parent;
                        } while (node > 0);
                    }
                }

                /* Move the winner of the tournament tree into output and update the
                tree. */
                template <size_t M>
                constexpr void merge() {
                    while (true) {
                        // move the overall winner into the output range
                        *output++ = std::move(*begin[winner]);
                        destroy(begin[winner]++);

                        // we can safely do `smallest` iterations before needing to
                        // check bounds
                        if (--smallest == 0) {
                            // update `smallest` to the minimum length of all non-empty
                            // runs
                            smallest = empty;
                            for (size_t i = 0; i < M; ++i) {
                                smallest = bertrand::min(
                                    smallest,
                                    size_t(end[i] - begin[i])
                                );
                            }

                            // if the result is zero, then it marks the end of the
                            // current stage
                            if (smallest == 0) {
                                break;
                            }
                        }

                        // bubble up next winner
                        size_t node = (M - 1) + winner;  // phantom leaf index
                        do {
                            size_t parent = (node - 1) / 2;  // direct parent
                            size_t loser = internal[parent];

                            // if next value in winner loses against the parent, then
                            // we swap them and continue bubbling up
                            if (less_than(*begin[loser], *begin[winner])) {
                                internal[parent] = winner;
                                winner = loser;
                            }
                            node = parent;
                        } while (node > 0);
                    }
                }

                /* End a merge stage by pruning empty runs, resetting the tournament
                tree, and recomputing the minimum length for the next stage.  */
                template <size_t M>
                constexpr void advance() {
                    smallest = empty;
                    for (size_t i = 0; i < M - 1; ++i) {
                        size_t len = end[i] - begin[i];

                        // if empty, pop from the array and left shift subsequent runs
                        if (!len) {
                            for (size_t j = i; j < M - 1; ++j) {
                                begin[j] = begin[j + 1];
                                end[j] = end[j + 1];
                            }
                            begin[M - 1] = end[M - 1];
                            len = end[i] - begin[i];
                        }

                        // record the minimum length and reset internal nodes
                        smallest = bertrand::min(smallest, len);
                        internal[i] = empty;
                    }
                }
            };

            /* An optimized tournament tree for binary merges with sentinel values. */
            template <>
            struct tournament_tree<2, true> {
                Less& less_than;
                pointer scratch;
                Begin output;
                size_t size;
                std::array<pointer, 2> begin;
                std::array<pointer, 2> end;

                constexpr tournament_tree(
                    Less& less_than,
                    pointer scratch,
                    run& left,
                    run& right
                ) :
                    less_than(less_than),
                    scratch(scratch),
                    output(left.iter),
                    size((left.stop - left.start) + (right.stop - right.start)),
                    begin{scratch, scratch + (left.stop - left.start) + 1},
                    end(begin)
                {
                    for (size_t i = left.start; i < left.stop; ++i) {
                        std::construct_at(
                            end[0]++,
                            std::move(*left.iter++)
                        );
                    }
                    std::construct_at(end[0], numeric::infinity());

                    for (size_t i = right.start; i < right.stop; ++i) {
                        std::construct_at(
                            end[1]++,
                            std::move(*right.iter++)
                        );
                    }
                    std::construct_at(end[1], numeric::infinity());
                }

                constexpr void operator()() {
                    for (size_t i = 0; i < size; ++i) {
                        bool less = static_cast<bool>(less_than(*begin[1], *begin[0]));
                        *output++ = std::move(*begin[less]);
                        destroy(begin[less]);
                        ++begin[less];
                    }
                }

                constexpr ~tournament_tree() {
                    for (size_t i = 0; i < begin.size(); ++i) {
                        while (begin[i] != end[i]) {
                            *output++ = std::move(*begin[i]);
                            destroy(begin[i]);
                            ++begin[i];
                        }
                        destroy(end[i]);
                    }
                }
            };

            /* An optimized tournament tree for binary merges without sentinel values. */
            template <>
            struct tournament_tree<2, false> {
                Less& less_than;
                pointer scratch;
                Begin output;
                size_t size;
                std::array<pointer, 2> begin;
                std::array<pointer, 2> end;

                constexpr tournament_tree(
                    Less& less_than,
                    pointer scratch,
                    run& left,
                    run& right
                ) :
                    less_than(less_than),
                    scratch(scratch),
                    output(left.iter),
                    size((left.stop - left.start) + (right.stop - right.start)),
                    begin{scratch, scratch + (left.stop - left.start)},
                    end(begin)
                {
                    for (size_t i = left.start; i < left.stop; ++i) {
                        std::construct_at(
                            end[0]++,
                            std::move(*left.iter++)
                        );
                    }
                    for (size_t i = right.start; i < right.stop; ++i) {
                        std::construct_at(
                            end[1]++,
                            std::move(*right.iter++)
                        );
                    }
                }

                constexpr void operator()() {
                    while (begin[0] < end[0] && begin[1] < end[1]) {
                        bool less = static_cast<bool>(less_than(*begin[1], *begin[0]));
                        *output++ = std::move(*begin[less]);
                        destroy(begin[less]++);
                    }
                    while (begin[0] < end[0]) {
                        *output++ = std::move(*begin[0]);
                        destroy(begin[0]);
                        ++begin[0];
                    }
                    while (begin[1] < end[1]) {
                        *output++ = std::move(*begin[1]);
                        destroy(begin[1]);
                        ++begin[1];
                    }
                }

                constexpr ~tournament_tree() {
                    for (size_t i = 0; i < begin.size(); ++i) {
                        while (begin[i] != end[i]) {
                            *output++ = std::move(*begin[i]);
                            destroy(begin[i]);
                            ++begin[i];
                        }
                        destroy(end[i]);
                    }
                }
            };

            std::vector<run> stack;

        public:
            /* Allocate stack space for a range of the given size. */
            constexpr merge_tree(size_t size) {
                stack.reserve((k - 1) * (ceil_logk(size) + 1));
            }

            /* Execute the sorting algorithm. */
            constexpr void operator()(
                Less& less_than,
                Begin& begin,
                size_t size,
                run& curr,
                powersort::scratch<Begin>& scratch
            ) {
                // build run stack according to powersort policy
                stack.emplace_back(begin, 0, size);  // power zero as sentinel
                do {
                    run next {begin, curr.stop};
                    if constexpr (meta::bidirectional_iterator<Begin>) {
                        next.detect(less_than, begin, size);
                    } else {
                        next.detect(less_than, begin, size, scratch);
                    }

                    // compute previous run's power with respect to next run
                    curr.power = get_power(size, curr, next);

                    // invariant: powers on stack weakly increase from bottom to top.
                    // If violated, merge runs with equal power into `curr` until
                    // invariant is restored.  Only at most the top `k - 1` runs will
                    // meet this criteria due to the structure of the merge tree and
                    // the definition of the power function.
                    while (stack.back().power > curr.power) {
                        /// NOTE: there is a small discrepancy from the description in
                        /// [1](3.2), where it states that there are at most `k - 1`
                        /// equal powers on the stack at any time.  This is technically
                        /// incorrect, as a pathological case can occur where `k - 1`
                        /// equal-power runs are followed by a run with greater power,
                        /// which does not trigger a merge, only for the next run after
                        /// that to have the same power as the prior `k - 1` runs.
                        /// This would trigger a 2-way merge with the larger power, and
                        /// then we would end up with `k` runs of equal power at the
                        /// top of the stack, against the paper's assurances.  This bug
                        /// does not occur in the provided reference implementation,
                        /// however, seemingly due to a fluke in the if ladder for the
                        /// merges, with the `else` clause converting the pathological
                        /// case into a single `k`-way merge, ignoring the oldest of
                        /// the `k` topmost runs.  This is replicated here by
                        /// terminating the detection loop early when it reaches
                        /// `k - 1`.  None of the other logic is affected, and the
                        /// merge tree remains optimal despite this minor caveat.
                        run* top = &stack.back();
                        size_t i = 1;
                        while (((top - i)->power == top->power) && (i < k - 1)) {
                            ++i;
                        }
                        using F = void(*)(Less&, pointer, std::vector<run>&, run&);
                        using VTable = std::array<F, k - 1>;
                        static constexpr VTable vtable = []<size_t... Is>(
                            std::index_sequence<Is...>
                        ) {
                            // 0: 2-way
                            // 1: 3-way
                            // 2: 4-way
                            // ...
                            // (k-2): k-way
                            return VTable{[]<size_t... Js>(std::index_sequence<Js...>) {
                                // Is... => [0, k - 2] (inclusive)
                                // Js... => [0, Is] (inclusive)
                                return +[](
                                    Less& less_than,
                                    pointer scratch,
                                    std::vector<run>& stack,
                                    run& curr
                                ) {
                                    constexpr size_t I = Is;
                                    Begin temp = stack[stack.size() - I - 1].iter;
                                    tournament_tree<I + 2>{
                                        less_than,
                                        scratch,
                                        stack[stack.size() - I + Js - 1]...,
                                        curr
                                    }();
                                    curr.iter = std::move(temp);
                                    curr.start = stack[stack.size() - I - 1].start;
                                };
                            }(std::make_index_sequence<Is + 1>{})...};
                        }(std::make_index_sequence<k - 1>{});

                        // merge runs with equal power by dispatching to vtable
                        vtable[i - 1](less_than, scratch.data.get(), stack, curr);
                        stack.erase(stack.end() - i, stack.end());  // pop merged runs
                    }

                    // push next run onto stack
                    stack.emplace_back(std::move(curr));
                    curr = std::move(next);
                } while (curr.stop < size);

                // Because runs typically increase in size exponentially as the stack
                // is emptied, we can manually merge the first few such that the stack
                // size is reduced to a multiple of `k - 1`, so that we can do `k`-way
                // merges the rest of the way.  This maximizes the benefit of the
                // tournament tree and minimizes total data movement/comparisons.
                using F = void(*)(Less&, pointer, std::vector<run>&, run&);
                using VTable = std::array<F, k - 1>;
                static constexpr VTable vtable = []<size_t... Is>(
                    std::index_sequence<Is...>
                ) {
                    return VTable{
                        // 0: do nothing
                        +[](
                            Less& less_than,
                            pointer scratch,
                            std::vector<run>& stack,
                            run& curr
                        ) {},
                        // 1: 2-way
                        // 2: 3-way,
                        // 3: 4-way,
                        // ...
                        // (k-2): (k-1)-way
                        []<size_t... Js>(std::index_sequence<Js...>) {
                            /// Is... => [0, k - 2] (inclusive)
                            // Js... => [0, Is] (inclusive)
                            return +[](
                                Less& less_than,
                                pointer scratch,
                                std::vector<run>& stack,
                                run& curr
                            ) {
                                constexpr size_t I = Is;
                                Begin temp = stack[stack.size() - I - 1].iter;
                                tournament_tree<I + 2>{
                                    less_than,
                                    scratch,
                                    stack[stack.size() - I + Js - 1]...,
                                    curr
                                }();
                                curr.iter = std::move(temp);
                                curr.start = stack[stack.size() - I - 1].start;
                                stack.erase(stack.end() - I - 1, stack.end());  // pop merged runs
                            };
                        }(std::make_index_sequence<Is + 1>{})...
                    };
                }(std::make_index_sequence<k - 2>{});

                // vtable is only consulted for the first merge, after which we
                // devolve to purely k-way merges
                vtable[(stack.size() - 1) % (k - 1)](
                    less_than,
                    scratch.data.get(),
                    stack,
                    curr
                );
                [&]<size_t... Is>(std::index_sequence<Is...>) {
                    while (stack.size() > 1) {
                        Begin temp = stack[stack.size() - (k - 1)].iter;
                        tournament_tree<k>{
                            less_than,
                            scratch.data.get(),
                            stack[stack.size() - (k - 1) + Is]...,
                            curr
                        }();
                        curr.iter = std::move(temp);
                        curr.start = stack[stack.size() - (k - 1)].start;
                        stack.erase(stack.end() - (k - 1), stack.end());  // pop merged runs
                    }
                }(std::make_index_sequence<k - 1>{});
            }
        };

    public:
        /* Execute the sort algorithm using unsorted values in the range [begin, end)
        and placing the result back into the same range.

        The `less_than` comparison function is used to determine the order of the
        elements.  It may be a pointer to an arbitrary member of the iterator's value
        type, in which case only that member will be compared.  Otherwise, it must be a
        function with the signature `bool(const T&, const T&)` where `T` is the value
        type of the iterator.  If no comparison function is given, it will default to a
        transparent `<` operator for each element.

        If an exception occurs during a comparison, the input range will be left in a
        valid but unspecified state, and may be partially sorted.  Any other exception
        (e.g. in a move constructor/assignment operator, destructor, or iterator
        operation) may result in undefined behavior. */
        template <typename Begin, typename End, typename Less = impl::Less>
            requires (meta::iter_sortable<Less, Begin, End>)
        static constexpr void operator()(Begin begin, End end, Less&& less_than = {}) {
            // get overall length of range (possibly O(n) if iterators do not support
            // O(1) distance)
            auto length = std::ranges::distance(begin, end);
            if (length < 2) {
                return;  // trivially sorted
            }
            size_t size = size_t(length);
            decltype(auto) compare = sort_by<meta::remove_rvalue<Less>>::fn(
                std::forward<Less>(less_than)
            );

            using B = meta::remove_reference<Begin>;
            using L = meta::remove_reference<decltype(compare)>;

            // identify first run and early return if trivially sorted.  Delay the
            // scratch allocation as long as possible.
            run<B, L> curr {begin, 0};
            if constexpr (meta::bidirectional_iterator<B>) {
                curr.detect(compare, begin, size);
                if (curr.stop == size) {
                    return;
                }
                scratch<B> buffer(size);
                merge_tree<B, L>{size}(compare, begin, size, curr, buffer);
            } else {
                scratch<B> buffer;
                curr.detect(compare, begin, size, buffer);
                if (curr.stop == size) {
                    return;
                }
                if (buffer.data == nullptr) {
                    // if no scratch space was needed for run detection, allocate it
                    // here before proceeding to build the merge tree.
                    buffer.allocate(size);
                }
                merge_tree<B, L>{size}(compare, begin, size, curr, buffer);
            }
        }

        /* An equivalent of the iterator-based call operator that accepts a range and
        uses its `size()` to deduce the length of the range. */
        template <typename Range, typename Less = impl::Less>
            requires (meta::sortable<Less, Range>)
        static constexpr void operator()(Range& range, Less&& less_than = {}) {
            // get overall length of range (possibly O(n) if the range is not
            // explicitly sized and iterators do not support O(1) distance)
            auto length = std::ranges::distance(range);
            if (length < 2) {
                return;  // trivially sorted
            }
            auto begin = std::ranges::begin(range);
            size_t size = size_t(length);
            decltype(auto) compare = sort_by<meta::remove_rvalue<Less>>::fn(
                std::forward<Less>(less_than)
            );

            using B = meta::remove_reference<meta::begin_type<Range>>;
            using L = meta::remove_reference<decltype(compare)>;

            // identify first run and early return if trivially sorted.  Delay the
            // scratch allocation as long as possible.
            run<B, L> curr {begin, 0};
            if constexpr (meta::bidirectional_iterator<B>) {
                curr.detect(compare, begin, size);
                if (curr.stop == size) {
                    return;
                }
                scratch<B> buffer(size);
                merge_tree<B, L>{size}(compare, begin, size, curr, buffer);
            } else {
                scratch<B> buffer;
                curr.detect(compare, begin, size, buffer);
                if (curr.stop == size) {
                    return;
                }
                if (buffer.data == nullptr) {
                    // if no scratch space was needed for run detection, allocate it
                    // here before proceeding to build the merge tree.
                    buffer.allocate(size);
                }
                merge_tree<B, L>{size}(compare, begin, size, curr, buffer);
            }
        }
    };

}


/* Sort an arbitrary range using an optimized, implementation-specific sorting
algorithm.

If the input range has a member `.sort()` method, this function will invoke it with the
given arguments.  Otherwise, it will fall back to a generalized sorting algorithm that
works on arbitrary output ranges, sorting them in-place.  The generalized algorithm
accepts an optional `less_than` comparison function, which can be used to provide
custom sorting criteria.  Such a function can be supplied as a function pointer,
lambda, or custom comparator type with the signature `bool(const T&, const T&)`
where `T` is the value type of the range.  Alternatively, it can also be supplied as
a pointer to a member of the value type or a member function that is callable without
arguments (i.e. a getter), in which case only that member will be considered for
comparison.  If no comparison function is given, it will default to a transparent `<`
operator for each pair of elements.

Currently, the default sorting algorithm is implemented as a heavily optimized,
run-adaptive, stable merge sort variant with a `k`-way powersort policy.  It requires
best case O(n) time due to optimal run detection and worst case O(n log n) time thanks
to a tournament tree that minimizes comparisons.  It needs O(n) extra scratch space,
and can work on arbitrary input ranges.  It is generally faster than `std::sort()` in
most cases, and has far fewer restrictions on its use.  Users should only need to
implement a custom member `.sort()` method if there is a better algorithm for a
particular type (which should be rare), or if they wish to embed it as a member method
for convenience.  In the latter case, users should call the powersort implemtation
directly to guard against infinite recursion, as follows:

```cpp

    template <bertrand::meta::sortable<MyType> Less = bertrand::impl::Less>
    void MyType::sort(Less&& less_than = {}) {
        bertrand::impl::powersort<k, min_run_length>{}(*this, std::forward<Less>(less));
    }

```

The `meta::sortable<Less, MyType>` concept encapsulates all of the requirements for
sorting based on any of the predicates described above, and enforces them at compile
time, while `impl::Less` (equivalent to `std::less<void>`) defaults to a transparent
comparison. */
template <meta::default_constructible Less = impl::Less, typename Range>
    requires (meta::sortable<Less, Range>)
constexpr void sort(Range&& range)
    noexcept(noexcept(impl::powersort{}(std::forward<Range>(range), Less{})))
    requires(
        !requires{std::forward<Range>(range).sort();} &&
        !requires{std::forward<Range>(range).template sort<Less>();}
    )
{
    impl::powersort{}(std::forward<Range>(range), Less{});
}


/* ADL version of `sort()`, which delegates to an implementation-specific
`range.sort<Less>(args...)` method.  All other arguments as well as the return type
(if any) will be perfectly forwarded to that method.  This version accepts an explicit
`Less` function type as a template parameter which will also be forwarded to the ADL
method */
template <meta::default_constructible Less = impl::Less, typename Range, typename... Args>
constexpr decltype(auto) sort(Range&& range, Args&&... args)
    noexcept(noexcept(
        std::forward<Range>(range).template sort<Less>(std::forward<Args>(args)...)
    ))
    requires(
        !requires{std::forward<Range>(range).sort(std::forward<Args>(args)...);} &&
        requires{
            std::forward<Range>(range).template sort<Less>(std::forward<Args>(args)...);
        }
    )
{
    return (std::forward<Range>(range).template sort<Less>(std::forward<Args>(args)...));
}


/* ADL version of `sort()`, which delegates to an implementation-specific
`range.sort<Less>(args...)` method.  All other arguments as well as the return type
(if any) will be perfectly forwarded to that method.  This version does not accept
any template parameters, and only forwards the arguments. */
template <typename Range, typename... Args>
constexpr decltype(auto) sort(Range&& range, Args&&... args)
    noexcept(noexcept(std::forward<Range>(range).sort(std::forward<Args>(args)...)))
    requires(requires{std::forward<Range>(range).sort(std::forward<Args>(args)...);})
{
    return (std::forward<Range>(range).sort(std::forward<Args>(args)...));
}


/* Iterator-based `sort()`, which always uses the fallback powersort implementation.
If the iterators do not support O(1) distance, the length of the range will be
computed in O(n) time before starting the sort algorithm. */
template <meta::default_constructible Less = impl::Less, typename Begin, typename End>
    requires (meta::iter_sortable<Less, Begin, End>)
constexpr void sort(Begin&& begin, End&& end)
    noexcept(noexcept(impl::powersort{}(
        std::forward<Begin>(begin),
        std::forward<End>(end),
        Less{}
    )))
{
    impl::powersort{}(std::forward<Begin>(begin), std::forward<End>(end), Less{});
}


}


namespace std {

    /// TODO: remember to do enable borrowed ranges for all ranges

    /* Specializing `std::ranges::enable_borrowed_range` ensures that iterators over
    slices are not tied to the lifetime of the slice itself, but rather to that of the
    underlying container. */
    template <bertrand::meta::slice T>
    constexpr bool ranges::enable_borrowed_range<T> = true;

}


#endif