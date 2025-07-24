#ifndef BERTRAND_ITER_H
#define BERTRAND_ITER_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/union.h"


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
    struct sequence_tag {};
    struct tuple_storage_tag {};

    template <typename T>
    concept strictly_positive =
        meta::unsigned_integer<T> ||
        !requires(meta::as_const_ref<T> t) {{t < 0} -> meta::explicitly_convertible_to<bool>;};

    template <typename Start, typename Stop, typename Step>
    concept iota_spec =
        meta::unqualified<Start> &&
        meta::unqualified<Stop> &&
        meta::copyable<Start> &&
        meta::copyable<Stop> &&
        (meta::lt_returns<bool, const Start&, const Stop&> || meta::None<Stop>) &&
        ((
            meta::is_void<Step> &&
            meta::has_preincrement<Start&>
        ) || (
            meta::not_void<Step> &&
            meta::unqualified<Step> &&
            meta::copyable<Step> &&
            (meta::has_iadd<Start&, const Step&> || meta::has_preincrement<Start&>) &&
            (strictly_positive<Step> || (
                meta::gt_returns<bool, const Start&, const Stop&> &&
                (meta::has_iadd<Start&, const Step&> || meta::has_predecrement<Start&>)
            ))
        ));

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


template <meta::iterator Begin, typename Count>
    requires (
        !meta::sentinel_for<Begin, Count> &&
        !impl::iota_spec<Begin, Count, void> &&
        meta::integer<Count>
    )
range(Begin, Count) -> range<std::ranges::subrange<
    std::counted_iterator<Begin>,
    std::default_sentinel_t
>>;


template <typename Start, typename Stop, typename Step>
    requires (impl::iota_spec<Start, Stop, Step>)
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
    concept sequence = inherits<T, impl::sequence_tag> && (is_void<R> || yields<T, R>);

    template <typename T, typename R = void>
    concept unpack = detail::unpack<unqualify<T>> && (is_void<R> || yields<T, R>);

    template <typename T, typename... Rs>
    concept unpack_to = detail::unpack<unqualify<T>> && (
        (tuple_like<T> && tuple_size<T> == sizeof...(Rs) && detail::unpack_convert<T, Rs...>) ||
        (!tuple_like<T> && ... && yields<T, Rs>)
    );

    template <typename T>
    concept slice = detail::slice<unqualify<T>>;


    // template <typename T>
    // concept comprehension = inherits<T, impl::comprehension_tag>;

    template <typename T>
    concept tuple_storage = inherits<T, impl::tuple_storage_tag>;

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

    template <typename C>
    constexpr decltype(auto) range_subscript(C&& container, size_t i)
        noexcept (requires{{std::forward<C>(container)[i]} noexcept;} || meta::tuple_like<C>)
        requires (requires{{std::forward<C>(container)[i]};} || meta::tuple_like<C>)
    {
        if constexpr (requires{{std::forward<C>(container)[i]};}) {
            return (std::forward<C>(container)[i]);
        } else {
            return typename impl::tuple_array<meta::forward<C>>::dispatch{i}(
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

    [[no_unique_address]] impl::store<__type> __value;

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

    /* CTAD constructor for counted ranges, which consist of a begin iterator and an
    integer size. */
    template <meta::iterator Begin, typename Count>
        requires (
            !meta::sentinel_for<Begin, Count> &&
            !impl::iota_spec<Begin, Count, void> &&
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
        std::ranges::swap(__value.value, other.__value.value);
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
        noexcept (requires{{meta::to_arrow(__value.value)} noexcept;})
        requires (requires{{meta::to_arrow(__value.value)};})
    {
        return meta::to_arrow(__value.value);
    }

    /* Indirectly access a member of the wrapped container. */
    [[nodiscard]] constexpr auto operator->() const
        noexcept (requires{{meta::to_arrow(__value.value)} noexcept;})
        requires (requires{{meta::to_arrow(__value.value)};})
    {
        return meta::to_arrow(__value.value);
    }

    /* Forwarding `size()` operator for the underlying container, provided the
    container supports it. */
    [[nodiscard]] constexpr auto size() const
        noexcept (meta::nothrow::has_size<C> || meta::tuple_like<C>)
        requires (meta::has_size<C> || meta::tuple_like<C>)
    {
        if constexpr (meta::has_size<C>) {
            return std::ranges::size(__value.value);
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
            return std::ranges::ssize(__value.value);
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
            return std::ranges::empty(__value.value);
        } else {
            return meta::tuple_size<C> == 0;
        }
    }

    /* Forwarding `get<I>()` accessor, provided the underlying container is
    tuple-like.  Automatically applies Python-style wraparound for negative indices. */
    template <ssize_t I, typename Self>
    constexpr decltype(auto) get(this Self&& self)
        noexcept (requires{{meta::unpack_tuple<I>(std::forward<Self>(self).__value.value)} noexcept;})
        requires (requires{{meta::unpack_tuple<I>(std::forward<Self>(self).__value.value)};})
    {
        return (meta::unpack_tuple<I>(std::forward<Self>(self).__value.value));
    }

    /* Integer indexing operator.  Accepts a single signed integer and retrieves the
    corresponding element from the underlying container after applying Python-style
    wraparound for negative indices.  If the container does not support indexing, but
    is otherwise tuple-like, then a vtable will be synthesized to back this
    operator. */
    template <typename Self>
    constexpr decltype(auto) operator[](this Self&& self, ssize_t i)
        noexcept (requires{{impl::range_subscript(
            std::forward<Self>(self).__value.value,
            size_t(impl::normalize_index(self.ssize(), i))
        )} noexcept;})
        requires (requires{{impl::range_subscript(
            std::forward<Self>(self).__value.value,
            size_t(impl::normalize_index(self.ssize(), i))
        )};})
    {
        return (impl::range_subscript(
            std::forward<Self>(self).__value.value,
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
        noexcept (requires{{impl::make_range_iterator{__value.value}.begin()} noexcept;})
        requires (requires{{impl::make_range_iterator{__value.value}.begin()};})
    {
        return (impl::make_range_iterator{__value.value}.begin());
    }

    /* Get a forward iterator to the start of the range. */
    [[nodiscard]] constexpr decltype(auto) begin() const
        noexcept (requires{{impl::make_range_iterator{__value.value}.begin()} noexcept;})
        requires (requires{{impl::make_range_iterator{__value.value}.begin()};})
    {
        return (impl::make_range_iterator{__value.value}.begin());
    }

    /* Get a forward iterator to the start of the range. */
    [[nodiscard]] constexpr decltype(auto) cbegin() const
        noexcept (requires{{impl::make_range_iterator{__value.value}.begin()} noexcept;})
        requires (requires{{impl::make_range_iterator{__value.value}.begin()};})
    {
        return (impl::make_range_iterator{__value.value}.begin());
    }

    /* Get a forward iterator to one past the last element of the range. */
    [[nodiscard]] constexpr decltype(auto) end()
        noexcept (requires{{impl::make_range_iterator{__value.value}.end()} noexcept;})
        requires (requires{{impl::make_range_iterator{__value.value}.end()};})
    {
        return (impl::make_range_iterator{__value.value}.end());
    }

    /* Get a forward iterator to one past the last element of the range. */
    [[nodiscard]] constexpr decltype(auto) end() const
        noexcept (requires{{impl::make_range_iterator{__value.value}.end()} noexcept;})
        requires (requires{{impl::make_range_iterator{__value.value}.end()};})
    {
        return (impl::make_range_iterator{__value.value}.end());
    }

    /* Get a forward iterator to one past the last element of the range. */
    [[nodiscard]] constexpr decltype(auto) cend() const
        noexcept (requires{{impl::make_range_iterator{__value.value}.end()} noexcept;})
        requires (requires{{impl::make_range_iterator{__value.value}.end()};})
    {
        return (impl::make_range_iterator{__value.value}.end());
    }

    /* Get a reverse iterator to the last element of the range. */
    [[nodiscard]] constexpr decltype(auto) rbegin()
        noexcept (requires{{impl::make_range_reversed{__value.value}.begin()} noexcept;})
        requires (requires{{impl::make_range_reversed{__value.value}.begin()};})
    {
        return (impl::make_range_reversed{__value.value}.begin());
    }

    /* Get a reverse iterator to the last element of the range. */
    [[nodiscard]] constexpr decltype(auto) rbegin() const
        noexcept (requires{{impl::make_range_reversed{__value.value}.begin()} noexcept;})
        requires (requires{{impl::make_range_reversed{__value.value}.begin()};})
    {
        return (impl::make_range_reversed{__value.value}.begin());
    }

    /* Get a reverse iterator to the last element of the range. */
    [[nodiscard]] constexpr decltype(auto) crbegin() const
        noexcept (requires{{impl::make_range_reversed{__value.value}.begin()} noexcept;})
        requires (requires{{impl::make_range_reversed{__value.value}.begin()};})
    {
        return (impl::make_range_reversed{__value.value}.begin());
    }

    /* Get a reverse iterator to one before the first element of the range. */
    [[nodiscard]] constexpr decltype(auto) rend()
        noexcept (requires{{impl::make_range_reversed{__value.value}.end()} noexcept;})
        requires (requires{{impl::make_range_reversed{__value.value}.end()};})
    {
        return (impl::make_range_reversed{__value.value}.end());
    }

    /* Get a reverse iterator to one before the first element of the range. */
    [[nodiscard]] constexpr decltype(auto) rend() const
        noexcept (requires{{impl::make_range_reversed{__value.value}.end()} noexcept;})
        requires (requires{{impl::make_range_reversed{__value.value}.end()};})
    {
        return (impl::make_range_reversed{__value.value}.end());
    }

    /* Get a reverse iterator to one before the first element of the range. */
    [[nodiscard]] constexpr decltype(auto) crend() const
        noexcept (requires{{impl::make_range_reversed{__value.value}.end()} noexcept;})
        requires (requires{{impl::make_range_reversed{__value.value}.end()};})
    {
        return (impl::make_range_reversed{__value.value}.end());
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


struct Foo {
    int x = 0;

    constexpr Foo& operator++() {
        ++x;
        return *this;
    }
    constexpr Foo& operator--() {
        --x;
        return *this;
    }
    constexpr auto operator<=>(const Foo& other) const {
        return x <=> other.x;
    }
};


// static_assert(impl::iota_spec<Foo, Foo, int>);

static_assert([] {
    for (auto&& v : range(Foo{}, Foo{3}, 2)) {
        if (v.x != 0 && v.x != 1 && v.x != 2) {
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
        requires (requires{this->__value.value.swap(other.__value.value);})
    {
        this->__value.value.swap(other.__value.value);
    }

    /* True if the underlying container supports `size()` checks.  False otherwise. */
    [[nodiscard]] bool has_size() const noexcept
        requires (requires{this->__value.has_size();})
    {
        return this->__value.value.has_size();
    }

    /* Return the current size of the sequence, assuming `has_size()` evaluates to
    true.  If `has_size()` is false, and the program is compiled in debug mode, then
    this function will throw a TypeError. */
    [[nodiscard]] constexpr auto size() const {
        return this->__value.value.size();
    }

    /* Identical to `size()`, except that the result is a signed integer. */
    [[nodiscard]] constexpr auto ssize() const {
        return this->__value.value.ssize();
    }

    /* True if the underlying container supports `empty()` checks.  False otherwise. */
    [[nodiscard]] constexpr bool has_empty() const noexcept {
        return this->__value.value.has_empty();
    }

    /* Returns true if the underlying container is empty or false otherwise, assuming
    `has_empty()` evaluates to true.  If `has_empty()` is false, and the program is
    compiled in debug mode, then this function will throw a TypeError. */
    [[nodiscard]] constexpr bool empty() const {
        return this->__value.value.empty();
    }

    /* True if the underlying container supports `operator[]` accessing.  False
    otherwise. */
    [[nodiscard]] constexpr bool has_subscript() const noexcept {
        return this->__value.value.has_subscript();
    }

    /* Index into the sequence, applying Python-style wraparound for negative
    indices if the sequence has a known size.  Otherwise, the index must be
    non-negative, and will be converted to `size_type`.  If the index is out of
    bounds after normalizing, and the program is compiled in debug mode, then this
    function will throw a TypeError. */
    [[nodiscard]] constexpr T operator[](__type::index_type i) const {
        return this->__value.value[i];
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


///////////////////////
////    REVERSE    ////
///////////////////////


namespace impl {

    /// TODO: add a swap method for reversed containers, and similar for slices and
    /// all other range adaptors.  I may also need swap operators for the public
    /// helpers, such as bertrand::slice{}, transform{}, and so on.

    /* An adaptor for a container that causes `range<impl::reversed<C>>` to reverse
    iterate over the container `C` instead of forward iterating.  This equates to
    swapping all of the `begin()` and `end()` methods with their reversed counterparts,
    and modifying the indexing logic to map index `i` to index `-i - 1`, which
    triggers Python-style wraparound. */
    template <typename C> requires (meta::reverse_iterable<C> || meta::tuple_like<C>)
    struct reverse {
        using __type = meta::remove_rvalue<C>;

        [[no_unique_address]] impl::store<__type> __value;

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{meta::to_arrow(__value.value)} noexcept;})
            requires (requires{{meta::to_arrow(__value.value)};})
        {
            return meta::to_arrow(__value.value);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{meta::to_arrow(__value.value)} noexcept;})
            requires (requires{{meta::to_arrow(__value.value)};})
        {
            return meta::to_arrow(__value.value);
        }

        [[nodiscard]] constexpr auto size() const
            noexcept (meta::nothrow::has_size<C> || meta::tuple_like<C>)
            requires (meta::has_size<C> || meta::tuple_like<C>)
        {
            if constexpr (meta::has_size<C>) {
                return std::ranges::size(__value.value);
            } else {
                return meta::tuple_size<C>;
            }
        }

        [[nodiscard]] constexpr auto ssize() const
            noexcept (meta::nothrow::has_ssize<C> || meta::tuple_like<C>)
            requires (meta::has_ssize<C> || meta::tuple_like<C>)
        {
            if constexpr (meta::has_ssize<C>) {
                return std::ranges::ssize(__value.value);
            } else {
                return meta::to_signed(meta::tuple_size<C>);
            }
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (meta::nothrow::has_empty<C> || meta::tuple_like<C>)
            requires (meta::has_empty<C> || meta::tuple_like<C>)
        {
            if constexpr (meta::has_empty<C>) {
                return std::ranges::empty(__value.value);
            } else {
                return meta::tuple_size<C> == 0;
            }
        }

        template <ssize_t I, typename Self>
        constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{
                {meta::unpack_tuple<-I - 1>(std::forward<Self>(self).__value.value)} noexcept;
            })
            requires (requires{
                {meta::unpack_tuple<-I - 1>(std::forward<Self>(self).__value.value)};
            })
        {
            return (meta::unpack_tuple<-I - 1>(std::forward<Self>(self).__value.value));
        }

        template <typename Self>
        constexpr decltype(auto) operator[](this Self&& self, ssize_t i)
            noexcept (requires{{std::forward<Self>(self).__value.value[
                size_t(impl::normalize_index(self.ssize(), -i - 1))]
            } noexcept;})
            requires (requires{{std::forward<Self>(self).__value.value[
                size_t(impl::normalize_index(self.ssize(), -i - 1))]
            };})
        {
            return (std::forward<Self>(self).__value.value[
                size_t(impl::normalize_index(self.ssize(), -i - 1))
            ]);
        }

        [[nodiscard]] constexpr decltype(auto) begin()
            noexcept (requires{{impl::make_range_reversed{__value.value}.begin()} noexcept;})
            requires (requires{{impl::make_range_reversed{__value.value}.begin()};})
        {
            return (impl::make_range_reversed{__value.value}.begin());
        }

        [[nodiscard]] constexpr decltype(auto) begin() const
            noexcept (requires{{impl::make_range_reversed{__value.value}.begin()} noexcept;})
            requires (requires{{impl::make_range_reversed{__value.value}.begin()};})
        {
            return (impl::make_range_reversed{__value.value}.begin());
        }

        [[nodiscard]] constexpr decltype(auto) end()
            noexcept (requires{{impl::make_range_reversed{__value.value}.end()} noexcept;})
            requires (requires{{impl::make_range_reversed{__value.value}.end()};})
        {
            return (impl::make_range_reversed{__value.value}.end());
        }

        [[nodiscard]] constexpr decltype(auto) end() const
            noexcept (requires{{impl::make_range_reversed{__value.value}.end()} noexcept;})
            requires (requires{{impl::make_range_reversed{__value.value}.end()};})
        {
            return (impl::make_range_reversed{__value.value}.end());
        }

        [[nodiscard]] constexpr decltype(auto) rbegin()
            noexcept  (requires{{impl::make_range_iterator{__value.value}.begin()} noexcept;})
            requires (requires{{impl::make_range_iterator{__value.value}.begin()};})
        {
            return (impl::make_range_iterator{__value.value}.begin());
        }

        [[nodiscard]] constexpr decltype(auto) rbegin() const
            noexcept (requires{{impl::make_range_iterator{__value.value}.begin()} noexcept;})
            requires (requires{{impl::make_range_iterator{__value.value}.begin()};})
        {
            return (impl::make_range_iterator{__value.value}.begin());
        }

        [[nodiscard]] constexpr decltype(auto) rend()
            noexcept (requires{{impl::make_range_iterator{__value.value}.end()} noexcept;})
            requires  (requires{{impl::make_range_iterator{__value.value}.end()};})
        {
            return (impl::make_range_iterator{__value.value}.end());
        }

        [[nodiscard]] constexpr decltype(auto) rend() const
            noexcept (requires{{impl::make_range_iterator{__value.value}.end()} noexcept;})
            requires (requires{{impl::make_range_iterator{__value.value}.end()};})
        {
            return (impl::make_range_iterator{__value.value}.end());
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


///////////////////
////    ZIP    ////
///////////////////


/// TODO: I'll need a whole bunch of compile-time nonsense to detect whether a function
/// is a valid visitor for a zip{} class, etc.


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







/// TODO: mostly implemented in func.h, and should be transferred here and made to
/// play with the underlying range interface.
/// -> This requires me to implement the `impl::tuple_storage` class up here, which
/// is necessary to store the relevant functions as an overload set, and to store the
/// arguments within the returned range adaptor, which can then be referenced by the
/// iterator.  The iterator would collect these and form them into a function
/// signature, where the default function perfectly forwards them as a `Tuple`.


namespace impl {

    /* Unless they happen to all consist of a single type, tuples use recursive
    inheritance */
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


    /// TODO: container adaptor for zip ranges

    template <typename F, typename... A>
    struct zip {
        using function_type = meta::unqualify<F>;
        using size_type = size_t;
        using index_type = ssize_t;

        /// TODO: value_type etc becomes the type that you get when you invoke the
        /// zip function with the reconstructed arguments, which is where all the
        /// super tough compile-time junk comes to.

    private:
        size_type m_size = std::numeric_limits<size_type>::max();
        meta::remove_rvalue<F> m_func;


    public:



    };



}


template <typename... Fs>
struct zip {


    /// TODO: constructor would form an overload set using `impl::tuple_storage`.

    /// TODO: call operator would take a number of arguments and form a zipped range
    /// with the given overload set.  Iterating over the range would gather the
    /// arguments and then invoke the overload set with the gathered arguments.
    /// If it is empty, then we simply package the gathered arguments into a
    /// tuple and yield that.

    /// TODO: It may be the case that no ranges are provided to the call operator,
    /// in which case we produce a range with only a single element.


};


/// TODO: the default case can be a separate specialization where the overload set
/// is fixed at compile time (and therefore doesn't need to be constructed), but
/// otherwise works the same way.


template <typename... Fs>
zip(Fs&&...) -> zip<meta::remove_rvalue<Fs>...>;



// /* A special case of `transform` that does not apply a function over the zipped
// contents, and instead simply yields them as tuples.

// This is identical to the default case of `transform{}`, but is provided with a more
// familiar spelling for users who are used to the `zip()` function from Python and other
// languages.  It is functionally identical to `transform{}` with no arguments. */
// inline constexpr transform<> zip;


/// TODO: enumerate might also just be a special case of `transform`.



/////////////////////
////    SLICE    ////
/////////////////////


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
        using type = meta::remove_rvalue<C>;
        using start_type = meta::remove_rvalue<Start>;
        using stop_type = meta::remove_rvalue<Stop>;
        using step_type = meta::remove_rvalue<Step>;
        using size_type = size_t;
        using index_type = ssize_t;

    private:
        [[no_unique_address]] impl::store<type> ref;
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
            return (std::forward<Self>(self).ref.value);
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
        [[no_unique_address]] impl::store<type> ref;
        [[no_unique_address]] impl::store<start_type> _start;
        [[no_unique_address]] impl::store<stop_type> _stop;
        [[no_unique_address]] impl::store<step_type> _step;

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


static constexpr std::array arr {1, 2, 3};
static constexpr range r(arr);
// static constexpr auto s = slice{{}, {}, 2}(arr);
static constexpr auto s = r[slice{{}, {}, 2}];
static_assert(s[0] == 1);
static_assert(s[1] == 3);




//////////////////////
////    REPEAT    ////
//////////////////////


/// TODO: maybe a repeat{N} monad that repeats a range `N` times, by simply
/// encapsulating a begin iterator and resetting it to the start after each iteration.
/// That's the behavior when used on ranges, but when used on non-ranges, it will
/// simply form a range out of them that effectively broadcasts.




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

    /// TODO: remember to do enable borrowed ranges for all ranges

    /* Specializing `std::ranges::enable_borrowed_range` ensures that iterators over
    slices are not tied to the lifetime of the slice itself, but rather to that of the
    underlying container. */
    template <bertrand::meta::slice T>
    constexpr bool ranges::enable_borrowed_range<T> = true;

}


#endif