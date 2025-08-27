#ifndef BERTRAND_ITER_REPEAT_H
#define BERTRAND_ITER_REPEAT_H


#include "bertrand/common.h"
#include "bertrand/iter/range.h"


namespace bertrand {


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


namespace iter {

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
        using range = iter::range<container<C>>;

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
        using range = iter::range<container<C>>;

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

}


}


#endif  // BERTRAND_ITER_REPEAT_H