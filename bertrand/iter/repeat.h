#ifndef BERTRAND_ITER_REPEAT_H
#define BERTRAND_ITER_REPEAT_H


#include "bertrand/common.h"
#include "bertrand/iter/range.h"


namespace bertrand {


namespace impl {

    /* Repeat iterator implementations are tailored to the capabilities of the
    underlying iterator type.  The base specialization is chosen for forward-only
    iterators, which simply reset to the begin iterator once a full repetition has been
    completed. */
    template <meta::iterator Begin, meta::sentinel_for<Begin> End>
    struct repeat_iterator {
        using iterator_category = meta::iterator_category<Begin>;
        using difference_type = meta::iterator_difference<Begin>;
        using value_type = meta::iterator_value<Begin>;
        using reference = meta::iterator_reference<Begin>;
        using pointer = meta::iterator_pointer<Begin>;

        [[no_unique_address]] Begin begin;
        [[no_unique_address]] End end;
        [[no_unique_address]] size_t count = 0;  // repetition count
        [[no_unique_address]] Begin iter = begin;  // current iterator

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).iter} noexcept;})
            requires (requires{{*std::forward<Self>(self).iter};})
        {
            return (*std::forward<Self>(self).iter);
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{impl::arrow{**this}} noexcept;})
            requires (requires{{impl::arrow{**this}};})
        {
            return impl::arrow{**this};
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow{**this}} noexcept;})
            requires (requires{{impl::arrow{**this}};})
        {
            return impl::arrow{**this};
        }

        constexpr repeat_iterator& operator++()
            noexcept (requires{
                {++iter} noexcept;
                {iter == end} noexcept -> meta::nothrow::truthy;
                {iter = begin} noexcept;
            })
            requires (requires{
                {++iter};
                {iter == end} -> meta::truthy;
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
            noexcept (requires{
                {repeat_iterator{*this}} noexcept;
                {++*this} noexcept;
            })
            requires (requires{
                {repeat_iterator{*this}};
                {++*this};
            })
        {
            repeat_iterator tmp = *this;
            ++*this;
            return tmp;
        }

        [[nodiscard]] constexpr bool operator==(const repeat_iterator& other) const
            noexcept (requires{{iter == other.iter} noexcept -> meta::nothrow::truthy;})
            requires (requires{{iter == other.iter} -> meta::truthy;})
        {
            return count == other.count && bool(iter == other.iter);
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
            noexcept (requires{{iter != other.iter} noexcept -> meta::nothrow::truthy;})
            requires (requires{{iter != other.iter} -> meta::truthy;})
        {
            return count != other.count || bool(iter != other.iter);
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

    /* Bidirectional iterators must also store an iterator to the last item of the
    previous repetition so that it can decrement backwards across the boundary. */
    template <meta::bidirectional_iterator Begin, meta::sentinel_for<Begin> End>
    struct repeat_iterator<Begin, End> {
        using iterator_category = meta::iterator_category<Begin>;
        using difference_type = meta::iterator_difference<Begin>;
        using value_type = meta::iterator_value<Begin>;
        using reference = meta::iterator_reference<Begin>;
        using pointer = meta::iterator_pointer<Begin>;

        [[no_unique_address]] Begin begin;
        [[no_unique_address]] End end;
        [[no_unique_address]] size_t count = 0;  // repetition count
        [[no_unique_address]] Begin iter = begin;  // current iterator
        [[no_unique_address]] Begin last = begin;  // last iterator

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).iter} noexcept;})
            requires (requires{{*std::forward<Self>(self).iter};})
        {
            return (*std::forward<Self>(self).iter);
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{impl::arrow{**this}} noexcept;})
            requires (requires{{impl::arrow{**this}};})
        {
            return impl::arrow{**this};
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow{**this}} noexcept;})
            requires (requires{{impl::arrow{**this}};})
        {
            return impl::arrow{**this};
        }

        constexpr repeat_iterator& operator++()
            noexcept (requires{
                {++iter} noexcept;
                {iter == end} noexcept -> meta::nothrow::truthy;
                {iter = begin} noexcept;
            })
            requires (requires{
                {++iter};
                {iter == end} -> meta::truthy;
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
            noexcept (requires{
                {repeat_iterator{*this}} noexcept;
                {++*this} noexcept;
            })
            requires (requires{
                {repeat_iterator{*this}};
                {++*this};
            })
        {
            repeat_iterator tmp = *this;
            ++*this;
            return tmp;
        }

        constexpr repeat_iterator& operator--()
            noexcept (requires{
                {--iter} noexcept;
                {iter == begin} noexcept -> meta::nothrow::truthy;
                {iter = last} noexcept;
            })
            requires (requires{
                {--iter};
                {iter == begin} -> meta::truthy;
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
            noexcept (requires{
                {repeat_iterator{*this}} noexcept;
                {--*this} noexcept;
            })
            requires (requires{
                {repeat_iterator{*this}} ;
                {--*this};
            })
        {
            repeat_iterator tmp = *this;
            --*this;
            return tmp;
        }

        [[nodiscard]] constexpr bool operator==(const repeat_iterator& other) const
            noexcept (requires{{iter == other.iter} noexcept -> meta::nothrow::truthy;})
            requires (requires{{iter == other.iter} -> meta::truthy;})
        {
            return count == other.count && bool(iter == other.iter);
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
            noexcept (requires{{iter != other.iter} noexcept -> meta::nothrow::truthy;})
            requires (requires{{iter != other.iter} -> meta::truthy;})
        {
            return count != other.count || bool(iter != other.iter);
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

    /* Random access iterators must also store the size of the range and current index
    in order to project indices onto the repeated range. */
    template <meta::random_access_iterator Begin, meta::sentinel_for<Begin> End>
    struct repeat_iterator<Begin, End> {
        using iterator_category = meta::iterator_category<Begin>;
        using difference_type = meta::iterator_difference<Begin>;
        using value_type = meta::iterator_value<Begin>;
        using reference = meta::iterator_reference<Begin>;
        using pointer = meta::iterator_pointer<Begin>;

        [[no_unique_address]] Begin begin;
        [[no_unique_address]] End end;
        [[no_unique_address]] size_t count = 0;  // repetition count
        [[no_unique_address]] difference_type size = std::ranges::distance(begin, end);  // size of each repetition
        [[no_unique_address]] difference_type index = 0;  // index in current repetition
        [[no_unique_address]] Begin iter = begin;  // current iterator

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).iter} noexcept;})
            requires (requires{{*std::forward<Self>(self).iter};})
        {
            return (*std::forward<Self>(self).iter);
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{impl::arrow{**this}} noexcept;})
            requires (requires{{impl::arrow{**this}};})
        {
            return impl::arrow{**this};
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow{**this}} noexcept;})
            requires (requires{{impl::arrow{**this}};})
        {
            return impl::arrow{**this};
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, difference_type n)
            noexcept (requires{
                {std::forward<Self>(self).begin[(self.index + n) % self.size]} noexcept;
            })
            requires (requires{
                {std::forward<Self>(self).begin[(self.index + n) % self.size]};
            })
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
            noexcept (requires{
                {repeat_iterator{*this}} noexcept;
                {++*this} noexcept;
            })
            requires (requires{
                {repeat_iterator{*this}};
                {++*this};
            })
        {
            repeat_iterator tmp = *this;
            ++*this;
            return tmp;
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

        [[nodiscard]] constexpr bool operator!=(const repeat_iterator& other) const
            noexcept (requires{{iter != other.iter} noexcept;})
            requires (requires{{iter != other.iter};})
        {
            return count != other.count || iter != other.iter;
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

        [[nodiscard]] constexpr auto operator<=>(const repeat_iterator& other) const
            noexcept (requires{{iter <=> other.iter} noexcept;})
            requires (requires{{iter <=> other.iter};})
        {
            if (auto cmp = other.count <=> count; cmp != 0) {
                return cmp;
            }
            return iter <=> other.iter;
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

    /// NOTE: if the underlying iterator is random access and the range has a
    /// compatible size, then we can avoid an extra `distance()` call by passing
    /// the size directly to the repeat iterator.

    template <typename T>
    [[nodiscard]] constexpr auto repeat_begin(T& self)
        noexcept ((
            meta::random_access_iterator<decltype(self->begin())> &&
            requires{{self->ssize()} -> meta::convertible_to<
                meta::iterator_difference<decltype(self->begin())>
            >;}
        ) ?
            requires{{repeat_iterator<decltype(self->begin()), decltype(self->end())>{
                .begin = self->begin(),
                .end = self->end(),
                .count = self.reps(),
                .size = self->ssize()
            }} noexcept;} :
            requires{{repeat_iterator<decltype(self->begin()), decltype(self->end())>{
                .begin = self->begin(),
                .end = self->end(),
                .count = self.reps()
            }} noexcept;}
        )
        requires (requires{{repeat_iterator<decltype(self->begin()), decltype(self->end())>{
            .begin = self->begin(),
            .end = self->end(),
            .count = self.reps()
        }};})
    {
        if constexpr (
            meta::random_access_iterator<decltype(self->begin())> &&
            requires{{self->ssize()} -> meta::convertible_to<
                meta::iterator_difference<decltype(self->begin())>
            >;}
        ) {
            return repeat_iterator<decltype(self->begin()), decltype(self->end())>{
                .begin = self->begin(),
                .end = self->end(),
                .count = self.reps(),
                .size = self->ssize(),
            };
        } else {
            return repeat_iterator<decltype(self->begin()), decltype(self->end())>{
                .begin = self->begin(),
                .end = self->end(),
                .count = self.reps(),
            };
        }
    }

    template <typename T>
    [[nodiscard]] constexpr auto repeat_rbegin(T& self)
        noexcept ((
            meta::random_access_iterator<decltype(self->rbegin())> &&
            requires{{self->ssize()} -> meta::convertible_to<
                meta::iterator_difference<decltype(self->rbegin())>
            >;}
        ) ?
            requires{{repeat_iterator<decltype(self->rbegin()), decltype(self->rend())>{
                .begin = self->rbegin(),
                .end = self->rend(),
                .count = self.reps(),
                .size = self->ssize()
            }} noexcept;} :
            requires{{repeat_iterator<decltype(self->rbegin()), decltype(self->rend())>{
                .begin = self->rbegin(),
                .end = self->rend(),
                .count = self.reps()
            }} noexcept;}
        )
        requires (requires{{repeat_iterator<decltype(self->rbegin()), decltype(self->rend())>{
            .begin = self->rbegin(),
            .end = self->rend(),
            .count = self.reps()
        }};})
    {
        if constexpr (
            meta::random_access_iterator<decltype(self->rbegin())> &&
            requires{{self->ssize()} -> meta::convertible_to<
                meta::iterator_difference<decltype(self->rbegin())>
            >;}
        ) {
            return repeat_iterator<decltype(self->rbegin()), decltype(self->rend())>{
                .begin = self->rbegin(),
                .end = self->rend(),
                .count = self.reps(),
                .size = self->ssize(),
            };
        } else {
            return repeat_iterator<decltype(self->rbegin()), decltype(self->rend())>{
                .begin = self->rbegin(),
                .end = self->rend(),
                .count = self.reps(),
            };
        }
    }


    /* If the repetition count is known at compile time, then we can emit an optimized
    range that omits the count member and retains tuple-like access. */
    template <meta::not_rvalue T, Optional<size_t> N> requires (meta::range<T>)
    struct repeat {
    private:
        [[no_unique_address]] impl::ref<T> m_value;
        static constexpr size_t m_reps = *N;

    public:
        [[nodiscard]] constexpr repeat(meta::forward<T> r)
            noexcept (requires{{impl::ref<T>(std::forward<T>(r))} noexcept;})
            requires (requires{{impl::ref<T>(std::forward<T>(r))};})
        :
            m_value(std::forward<T>(r))
        {}

        [[nodiscard]] static constexpr size_t reps() noexcept {
            return m_reps;
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).m_value} noexcept;})
            requires (requires{{*std::forward<Self>(self).m_value};})
        {
            return (*std::forward<Self>(self).m_value);
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{std::addressof(**this)} noexcept;})
            requires (requires{{std::addressof(**this)};})
        {
            return std::addressof(**this);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{std::addressof(**this)} noexcept;})
            requires (requires{{std::addressof(**this)};})
        {
            return std::addressof(**this);
        }

        [[nodiscard]] constexpr decltype(auto) size() const
            noexcept (requires{{reps() * (**this).size()} noexcept;})
            requires (requires{{reps() * (**this).size()};})
        {
            return (reps() * (**this).size());
        }

        [[nodiscard]] constexpr decltype(auto) ssize() const
            noexcept (requires{{reps() * (**this).ssize()} noexcept;})
            requires (requires{{reps() * (**this).ssize()};})
        {
            return (reps() * (**this).ssize());
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (reps() == 0 || requires{{(**this).empty()} noexcept;})
            requires (reps() == 0 || requires{{(**this).empty()};})
        {
            if constexpr (reps() == 0) {
                return true;
            } else {
                return (**this).empty();
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self)
            noexcept (requires{{(*std::forward<Self>(self)).front()} noexcept;})
            requires (requires{{(*std::forward<Self>(self)).front()};})
        {
            return ((*std::forward<Self>(self)).front());
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) back(this Self&& self)
            noexcept (requires{{(*std::forward<Self>(self)).back()} noexcept;})
            requires (requires{{(*std::forward<Self>(self)).back()};})
        {
            return ((*std::forward<Self>(self)).back());
        }

        template <ssize_t I, typename Self>
            requires (meta::tuple_like<T> && impl::valid_index<meta::tuple_size<T> * reps(), I>)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{meta::get<size_t(
                impl::normalize_index<meta::tuple_size<T> * reps(), I>() % meta::tuple_size<T>
            )>(*std::forward<Self>(self))} noexcept;})
            requires (requires{{meta::get<size_t(
                impl::normalize_index<meta::tuple_size<T> * reps(), I>() % meta::tuple_size<T>
            )>(*std::forward<Self>(self))};})
        {
            return (meta::get<size_t(
                impl::normalize_index<meta::tuple_size<T> * reps(), I>() % meta::tuple_size<T>
            )>(*std::forward<Self>(self)));
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, ssize_t i)
            noexcept (requires{{(*std::forward<Self>(self))[size_t(
                impl::normalize_index(self.ssize(), i) % self->ssize()
            )]} noexcept;})
            requires (requires{{(*std::forward<Self>(self))[size_t(
                impl::normalize_index(self.ssize(), i) % self->ssize()
            )]};})
        {
            return ((*std::forward<Self>(self))[size_t(
                impl::normalize_index(self.ssize(), i) % self->ssize()
            )]);
        }

        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{repeat_begin(*this)} noexcept;})
            requires (requires{{repeat_begin(*this)};})
        {
            return repeat_begin(*this);
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{repeat_begin(*this)} noexcept;})
            requires (requires{{repeat_begin(*this)};})
        {
            return repeat_begin(*this);
        }

        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }

        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{repeat_rbegin(*this)} noexcept;})
            requires (requires{{repeat_rbegin(*this)};})
        {
            return repeat_rbegin(*this);
        }

        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{repeat_rbegin(*this)} noexcept;})
            requires (requires{{repeat_rbegin(*this)};})
        {
            return repeat_rbegin(*this);
        }

        [[nodiscard]] static constexpr NoneType rend() noexcept { return {}; }
    };

    /* Runtime repetitions necessitate an extra `count` member and clobber the tuple
    interface.  Additionally, providing `None` as a count initializer indicates an
    infinite range.  For performance reasons, this may be downgraded to simply a max
    `size_t` repetition count, meaning infinite repetitions are technically bounded,
    but with a very large size. */
    template <meta::not_rvalue T>
    struct repeat<T, None> {
    private:
        [[no_unique_address]] impl::ref<T> m_value;
        [[no_unique_address]] size_t m_reps = 0;

    public:
        [[nodiscard]] constexpr repeat(meta::forward<T> range, size_t count)
            noexcept (requires{{impl::ref<T>(std::forward<T>(range))} noexcept;})
            requires (requires{{impl::ref<T>(std::forward<T>(range))};})
        :
            m_value(std::forward<T>(range)),
            m_reps(count)
        {}

        [[nodiscard]] constexpr size_t reps() const noexcept {
            return m_reps;
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self)
            noexcept (requires{{*std::forward<Self>(self).m_value} noexcept;})
            requires (requires{{*std::forward<Self>(self).m_value};})
        {
            return (*std::forward<Self>(self).m_value);
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{std::addressof(**this)} noexcept;})
            requires (requires{{std::addressof(**this)};})
        {
            return std::addressof(**this);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{std::addressof(**this)} noexcept;})
            requires (requires{{std::addressof(**this)};})
        {
            return std::addressof(**this);
        }

        [[nodiscard]] constexpr decltype(auto) size() const
            noexcept (requires{{reps() * (**this).size()} noexcept;})
            requires (requires{{reps() * (**this).size()};})
        {
            return (reps() * (**this).size());
        }

        [[nodiscard]] constexpr decltype(auto) ssize() const
            noexcept (requires{{reps() * (**this).ssize()} noexcept;})
            requires (requires{{reps() * (**this).ssize()};})
        {
            return (reps() * (**this).ssize());
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{reps() == 0 || (**this).empty()} noexcept;})
            requires (requires{{reps() == 0 || (**this).empty()};})
        {
            return reps() == 0 || (**this).empty();
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) front(this Self&& self)
            noexcept (requires{{(*std::forward<Self>(self)).front()} noexcept;})
            requires (requires{{(*std::forward<Self>(self)).front()};})
        {
            return ((*std::forward<Self>(self)).front());
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) back(this Self&& self)
            noexcept (requires{{(*std::forward<Self>(self)).back()} noexcept;})
            requires (requires{{(*std::forward<Self>(self)).back()};})
        {
            return ((*std::forward<Self>(self)).back());
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, ssize_t i)
            noexcept (requires{{(*std::forward<Self>(self))[size_t(
                impl::normalize_index(self.ssize(), i) % self->ssize()
            )]} noexcept;})
            requires (requires{{(*std::forward<Self>(self))[size_t(
                impl::normalize_index(self.ssize(), i) % self->ssize()
            )]};})
        {
            return ((*std::forward<Self>(self))[size_t(
                impl::normalize_index(self.ssize(), i) % self->ssize()
            )]);
        }


        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{repeat_begin(*this)} noexcept;})
            requires (requires{{repeat_begin(*this)};})
        {
            return repeat_begin(*this);
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{repeat_begin(*this)} noexcept;})
            requires (requires{{repeat_begin(*this)};})
        {
            return repeat_begin(*this);
        }

        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }

        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{repeat_rbegin(*this)} noexcept;})
            requires (requires{{repeat_rbegin(*this)};})
        {
            return repeat_rbegin(*this);
        }

        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{repeat_rbegin(*this)} noexcept;})
            requires (requires{{repeat_rbegin(*this)};})
        {
            return repeat_rbegin(*this);
        }

        [[nodiscard]] static constexpr NoneType rend() noexcept { return {}; }
    };

}


namespace iter {

    /* A function object that repeats the contents of an incoming container a given number
    of times, concatenating the results into a single range.

    This class comes in 2 flavors: one that encodes the repetition count at compile time
    and retains tuple-like access to the repeated range, and the other that only knows the
    repetition count at run time.  Other than tuple-like access, both cases behave the
    same, and produce the same results when iterated over or indexed. */
    template <Optional<size_t> N = None>
    struct repeat {
        static constexpr size_t reps = N == None ? 0 : *N;

        template <typename T>
        [[nodiscard]] constexpr auto operator()(T&& v) noexcept requires (reps == 0) {
            return range<impl::empty_range<meta::yield_type<meta::as_range_or_scalar<T>>>>{};
        }

        template <typename T>
        [[nodiscard]] constexpr decltype(auto) operator()(T&& v)
            noexcept (requires{{meta::to_range_or_scalar(std::forward<T>(v))} noexcept;})
            requires (reps == 1 && requires{{meta::to_range_or_scalar(std::forward<T>(v))};})
        {
            return (meta::to_range_or_scalar(std::forward<T>(v)));
        }

        template <typename T>
        [[nodiscard]] constexpr auto operator()(T&& v)
            noexcept (requires{{range<impl::repeat<meta::as_range_or_scalar<T>, N>>{
                std::forward<T>(v)
            }} noexcept;})
            requires (reps > 1 && requires{{range<impl::repeat<meta::as_range_or_scalar<T>, N>>{
                std::forward<T>(v)
            }};})
        {
            return range<impl::repeat<meta::as_range_or_scalar<T>, N>>{std::forward<T>(v)};
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
        size_t reps = std::numeric_limits<size_t>::max();

        /* Invoking the repeat adaptor produces a corresponding range type. */
        template <typename T>
        [[nodiscard]] constexpr auto operator()(T&& v)
            noexcept (requires{{range<impl::repeat<meta::as_range_or_scalar<T>, None>>{
                std::forward<T>(v),
                reps
        }} noexcept;})
            requires (requires{{range<impl::repeat<meta::as_range_or_scalar<T>, None>>{
                std::forward<T>(v),
                reps
            }};})
        {
            return range<impl::repeat<meta::as_range_or_scalar<T>, None>>{
                std::forward<T>(v),
                reps
            };
        }
    };

    template <typename T>
    repeat(T n) -> repeat<None>;

}


}  // namespace bertrand


_LIBCPP_BEGIN_NAMESPACE_STD

    namespace ranges {

        template <typename T, bertrand::Optional N>
        constexpr bool enable_borrowed_range<bertrand::impl::repeat<T, N>> = 
            enable_borrowed_range<bertrand::meta::unqualify<T>>;

    }

    template <bertrand::meta::tuple_like T, bertrand::Optional N> requires (N != bertrand::None)
    struct tuple_size<bertrand::impl::repeat<T, N>> : integral_constant<
        size_t,
        tuple_size<bertrand::meta::unqualify<T>>::value * (*N)
    > {};

    template <size_t I, bertrand::meta::tuple_like T, bertrand::Optional N>
        requires (N != bertrand::None && I < tuple_size<bertrand::impl::repeat<T, N>>::value)
    struct tuple_element<I, bertrand::impl::repeat<T, N>> {
        using type = tuple_element<
            I % tuple_size<bertrand::meta::unqualify<T>>::value,
            bertrand::meta::remove_reference<T>
        >::type;
    };

_LIBCPP_END_NAMESPACE_STD


#endif  // BERTRAND_ITER_REPEAT_H