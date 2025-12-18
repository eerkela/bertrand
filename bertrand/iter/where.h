#ifndef BERTRAND_ITER_WHERE_H
#define BERTRAND_ITER_WHERE_H

#include "bertrand/iter/range.h"


namespace bertrand {


namespace impl {

    template <range_direction Dir, typename Self>
    struct binary_filter_iterator {
        using function_type = meta::remove_reference<decltype((std::declval<Self>().filter()))>;
        using begin_type = decltype(Dir::begin(std::declval<Self>().if_true()));
        using end_type = decltype(Dir::end(std::declval<Self>().if_true()));
        using category = std::conditional_t<
            meta::inherits<meta::iterator_category<begin_type>, std::bidirectional_iterator_tag>,
            std::bidirectional_iterator_tag,
            std::forward_iterator_tag
        >;
        using difference_type = meta::iterator_difference<begin_type>;
        using reference = meta::dereference_type<meta::as_const_ref<begin_type>>;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<value_type>;

        [[no_unique_address]] meta::as_const<function_type>* func = nullptr;
        [[no_unique_address]] difference_type index = 0;
        [[no_unique_address]] begin_type begin;
        [[no_unique_address]] end_type end;

        [[nodiscard]] constexpr binary_filter_iterator() = default;
        [[nodiscard]] constexpr binary_filter_iterator(meta::forward<Self> self)
            noexcept (requires{
                {std::addressof(self.filter())} noexcept;
                {Dir::begin(std::forward<Self>(self).if_true())} noexcept;
                {Dir::end(std::forward<Self>(self).if_true())} noexcept;
                {begin != end} noexcept -> meta::nothrow::truthy;
                {(*func)(*begin)} noexcept -> meta::nothrow::truthy;
                {++begin} noexcept;
            })
            requires (std::same_as<Dir, range_forward> && requires{
                {std::addressof(self.filter())};
                {Dir::begin(std::forward<Self>(self).if_true())};
                {Dir::end(std::forward<Self>(self).if_true())};
                {begin != end} -> meta::truthy;
                {(*func)(*begin)} -> meta::truthy;
                {++begin};
            })
        :
            func(std::addressof(self.filter())),
            index(0),
            begin(Dir::begin(std::forward<Self>(self).if_true())),
            end(Dir::end(std::forward<Self>(self).if_true()))
        {
            while (begin != end) {
                if ((*func)(*begin)) {
                    break;
                }
                ++begin;
                ++index;
            }
        }
        [[nodiscard]] constexpr binary_filter_iterator(meta::forward<Self> self, size_t size)
            noexcept (requires{
                {std::addressof(self.filter())} noexcept;
                {Dir::begin(std::forward<Self>(self).if_true())} noexcept;
                {Dir::end(std::forward<Self>(self).if_true())} noexcept;
                {begin != end} noexcept -> meta::nothrow::truthy;
                {(*func)(*begin)} noexcept -> meta::nothrow::truthy;
                {++begin} noexcept;
                {begin += (end - begin) - size} noexcept;
            })
            requires (std::same_as<Dir, range_reverse> && requires{
                {std::addressof(self.filter())};
                {Dir::begin(std::forward<Self>(self).if_true())};
                {Dir::end(std::forward<Self>(self).if_true())};
                {begin != end} -> meta::truthy;
                {(*func)(*begin)} -> meta::truthy;
                {++begin};
                {begin += (end - begin) - size};
            })
        :
            func(std::addressof(self.filter())),
            index(0),
            begin(Dir::begin(std::forward<Self>(self).if_true())),
            end(Dir::end(std::forward<Self>(self).if_true()))
        {
            begin += (end - begin) - size;
            while (begin != end) {
                if ((*func)(*begin)) {
                    break;
                }
                ++begin;
                ++index;
            }
        }

        [[nodiscard]] constexpr reference operator*() const
            noexcept (requires{{*begin} noexcept;})
            requires (requires{{*begin};})
        {
            return (*begin);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow{**this}} noexcept;})
            requires (requires{{impl::arrow{**this}};})
        {
            return impl::arrow{**this};
        }

        constexpr binary_filter_iterator& operator++()
            noexcept (requires{
                {++begin} noexcept;
                {begin != end} noexcept -> meta::nothrow::truthy;
                {(*func)(*begin)} noexcept -> meta::nothrow::truthy;
            })
            requires (requires{
                {++begin};
                {begin != end} -> meta::truthy;
                {(*func)(*begin)} -> meta::truthy;
            })
        {
            ++begin;
            ++index;
            while (begin != end) {
                if ((*func)(*begin)) {
                    break;
                }
                ++begin;
                ++index;
            }
            return *this;
        }

        [[nodiscard]] constexpr binary_filter_iterator operator++(int)
            noexcept (requires{
                {binary_filter_iterator{*this}} noexcept;
                {++*this} noexcept;
            })
            requires (requires{
                {binary_filter_iterator{*this}};
                {++*this};
            })
        {
            binary_filter_iterator temp = *this;
            ++*this;
            return temp;
        }

        constexpr binary_filter_iterator& operator--()
            noexcept (requires{
                {--begin} noexcept;
                {(*func)(*begin)} noexcept -> meta::nothrow::truthy;
            })
            requires (requires{
                {--begin};
                {(*func)(*begin)} -> meta::truthy;
            })
        {
            --begin;
            --index;
            while (index >= 0) {
                if ((*func)(*begin)) {
                    break;
                }
                --begin;
                --index;
            }
            return *this;
        }

        [[nodiscard]] constexpr binary_filter_iterator operator--(int)
            noexcept (requires{
                {binary_filter_iterator{*this}} noexcept;
                {--*this} noexcept;
            })
            requires (requires{
                {binary_filter_iterator{*this}};
                {--*this};
            })
        {
            binary_filter_iterator temp = *this;
            --*this;
            return temp;
        }

        [[nodiscard]] constexpr bool operator==(const binary_filter_iterator& other) const noexcept {
            return index == other.index;
        }

        [[nodiscard]] constexpr auto operator<=>(const binary_filter_iterator& other) const noexcept {
            return index <=> other.index;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const binary_filter_iterator& self,
            impl::sentinel
        )
            noexcept (requires{
                {self.begin == self.end} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {self.begin == self.end} -> meta::convertible_to<bool>;
            })
        {
            return self.begin == self.end;
        }

        [[nodiscard]] friend constexpr bool operator==(
            impl::sentinel,
            const binary_filter_iterator& self
        )
            noexcept (requires{
                {self.begin == self.end} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {self.begin == self.end} -> meta::convertible_to<bool>;
            })
        {
            return self.begin == self.end;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const binary_filter_iterator& self,
            impl::sentinel
        )
            noexcept (requires{
                {self.begin != self.end} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {self.begin != self.end} -> meta::convertible_to<bool>;
            })
        {
            return self.begin != self.end;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            impl::sentinel,
            const binary_filter_iterator& self
        )
            noexcept (requires{
                {self.begin != self.end} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {self.begin != self.end} -> meta::convertible_to<bool>;
            })
        {
            return self.begin != self.end;
        }
    };
    template <range_direction Dir, typename Self>
        requires (meta::range<decltype(std::declval<Self>().filter()), bool>)
    struct binary_filter_iterator<Dir, Self> {
        using mask_begin_type = decltype(Dir::begin(std::declval<Self>().filter()));
        using mask_end_type = decltype(Dir::end(std::declval<Self>().filter()));
        using begin_type = decltype(Dir::begin(std::declval<Self>().if_true()));
        using end_type = decltype(Dir::end(std::declval<Self>().if_true()));
        using category = std::conditional_t<
            meta::inherits<
                meta::common_type<
                    meta::iterator_category<begin_type>,
                    meta::iterator_category<mask_begin_type>
                >,
                std::bidirectional_iterator_tag
            >,
            std::bidirectional_iterator_tag,
            std::forward_iterator_tag
        >;
        using difference_type = meta::common_type<
            meta::iterator_difference<begin_type>,
            meta::iterator_difference<mask_begin_type>
        >;
        using reference = meta::dereference_type<meta::as_const_ref<begin_type>>;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<value_type>;

        [[no_unique_address]] mask_begin_type mask_begin;
        [[no_unique_address]] mask_end_type mask_end;
        [[no_unique_address]] difference_type index = 0;
        [[no_unique_address]] begin_type begin;
        [[no_unique_address]] end_type end;

        [[nodiscard]] constexpr binary_filter_iterator() = default;
        [[nodiscard]] constexpr binary_filter_iterator(meta::forward<Self> self)
            noexcept (requires{
                {Dir::begin(std::forward<Self>(self).filter())} noexcept;
                {Dir::end(std::forward<Self>(self).filter())} noexcept;
                {Dir::begin(std::forward<Self>(self).if_true())} noexcept;
                {Dir::end(std::forward<Self>(self).if_true())} noexcept;
                {begin != end && mask_begin != mask_end} noexcept -> meta::nothrow::truthy;
                {*mask_begin} noexcept -> meta::nothrow::truthy;
                {++begin} noexcept;
                {++mask_begin} noexcept;
            })
            requires (std::same_as<Dir, range_forward> && requires{
                {Dir::begin(std::forward<Self>(self).filter())};
                {Dir::end(std::forward<Self>(self).filter())};
                {Dir::begin(std::forward<Self>(self).if_true())};
                {Dir::end(std::forward<Self>(self).if_true())};
                {begin != end && mask_begin != mask_end} -> meta::truthy;
                {*mask_begin} -> meta::truthy;
                {++begin};
                {++mask_begin};
            })
        :
            mask_begin(Dir::begin(std::forward<Self>(self).filter())),
            mask_end(Dir::end(std::forward<Self>(self).filter())),
            index(0),
            begin(Dir::begin(std::forward<Self>(self).if_true())),
            end(Dir::end(std::forward<Self>(self).if_true()))
        {
            while (begin != end && mask_begin != mask_end) {
                if (*mask_begin) {
                    break;
                }
                ++begin;
                ++mask_begin;
                ++index;
            }
        }
        [[nodiscard]] constexpr binary_filter_iterator(meta::forward<Self> self, size_t size)
            noexcept (requires{
                {Dir::begin(std::forward<Self>(self).filter())} noexcept;
                {Dir::end(std::forward<Self>(self).filter())} noexcept;
                {Dir::begin(std::forward<Self>(self).if_true())} noexcept;
                {Dir::end(std::forward<Self>(self).if_true())} noexcept;
                {begin != end && mask_begin != mask_end} noexcept -> meta::nothrow::truthy;
                {*mask_begin} noexcept -> meta::nothrow::truthy;
                {++begin} noexcept;
                {++mask_begin} noexcept;
                {mask_begin += (mask_end - mask_begin) - size} noexcept;
                {begin += (end - begin) - size} noexcept;
            })
            requires (std::same_as<Dir, range_reverse> && requires{
                {Dir::begin(std::forward<Self>(self).filter())};
                {Dir::end(std::forward<Self>(self).filter())};
                {Dir::begin(std::forward<Self>(self).if_true())};
                {Dir::end(std::forward<Self>(self).if_true())};
                {begin != end && mask_begin != mask_end} -> meta::truthy;
                {*mask_begin} -> meta::truthy;
                {++begin};
                {++mask_begin};
                {mask_begin += (mask_end - mask_begin) - size};
                {begin += (end - begin) - size};
            })
        :
            mask_begin(Dir::begin(std::forward<Self>(self).filter())),
            mask_end(Dir::end(std::forward<Self>(self).filter())),
            index(0),
            begin(Dir::begin(std::forward<Self>(self).if_true())),
            end(Dir::end(std::forward<Self>(self).if_true()))
        {
            mask_begin += (mask_end - mask_begin) - size;
            begin += (end - begin) - size;
            while (begin != end && mask_begin != mask_end) {
                if (*mask_begin) {
                    break;
                }
                ++begin;
                ++mask_begin;
                ++index;
            }
        }

        [[nodiscard]] constexpr reference operator*() const
            noexcept (requires{{*begin} noexcept;})
            requires (requires{{*begin};})
        {
            return (*begin);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow{**this}} noexcept;})
            requires (requires{{impl::arrow{**this}};})
        {
            return impl::arrow{**this};
        }

        constexpr binary_filter_iterator& operator++()
            noexcept (requires{
                {++begin} noexcept;
                {++mask_begin} noexcept;
                {begin != end && mask_begin != mask_end} noexcept -> meta::nothrow::truthy;
                {*mask_begin} noexcept -> meta::nothrow::truthy;
            })
            requires (requires{
                {++begin};
                {++mask_begin};
                {begin != end && mask_begin != mask_end} -> meta::truthy;
                {*mask_begin} -> meta::truthy;
            })
        {
            ++begin;
            ++mask_begin;
            ++index;
            while (begin != end && mask_begin != mask_end) {
                if (*mask_begin) {
                    break;
                }
                ++begin;
                ++mask_begin;
                ++index;
            }
            return *this;
        }

        [[nodiscard]] constexpr binary_filter_iterator operator++(int)
            noexcept (requires{
                {binary_filter_iterator{*this}} noexcept;
                {++*this} noexcept;
            })
            requires (requires{
                {binary_filter_iterator{*this}};
                {++*this};
            })
        {
            binary_filter_iterator temp = *this;
            ++*this;
            return temp;
        }

        constexpr binary_filter_iterator& operator--()
            noexcept (requires{
                {--begin} noexcept;
                {--mask_begin} noexcept;
                {*mask_begin} noexcept -> meta::nothrow::truthy;
            })
            requires (requires{
                {--begin};
                {--mask_begin};
                {*mask_begin} -> meta::truthy;
            })
        {
            --begin;
            --mask_begin;
            --index;
            while (index >= 0) {
                if (*mask_begin) {
                    break;
                }
                --begin;
                --mask_begin;
                --index;
            }
            return *this;
        }

        [[nodiscard]] constexpr binary_filter_iterator operator--(int)
            noexcept (requires{
                {binary_filter_iterator{*this}} noexcept;
                {--*this} noexcept;
            })
            requires (requires{
                {binary_filter_iterator{*this}};
                {--*this};
            })
        {
            binary_filter_iterator temp = *this;
            --*this;
            return temp;
        }

        [[nodiscard]] constexpr bool operator==(const binary_filter_iterator& other) const noexcept {
            return index == other.index;
        }

        [[nodiscard]] constexpr auto operator<=>(const binary_filter_iterator& other) const noexcept {
            return index <=> other.index;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const binary_filter_iterator& self,
            impl::sentinel
        )
            noexcept (requires{{
                self.begin == self.end || self.mask_begin == self.mask_end
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                self.begin == self.end || self.mask_begin == self.mask_end
            } -> meta::convertible_to<bool>;})
        {
            return self.begin == self.end || self.mask_begin == self.mask_end;
        }

        [[nodiscard]] friend constexpr bool operator==(
            impl::sentinel,
            const binary_filter_iterator& self
        )
            noexcept (requires{{
                self.begin == self.end || self.mask_begin == self.mask_end
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                self.begin == self.end || self.mask_begin == self.mask_end
            } -> meta::convertible_to<bool>;})
        {
            return self.begin == self.end || self.mask_begin == self.mask_end;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const binary_filter_iterator& self,
            impl::sentinel
        )
            noexcept (requires{{
                self.begin != self.end && self.mask_begin != self.mask_end
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                self.begin != self.end && self.mask_begin != self.mask_end
            } -> meta::convertible_to<bool>;})
        {
            return self.begin != self.end && self.mask_begin != self.mask_end;
        }
    };

    /* Binary `where{}` expressions correspond to a filter that omits values from a
    range where the predicate or mask evaluates to `false`.  Because this cannot be
    known generically ahead of time, the resulting range is unsized, not a tuple, and
    not indexable.  Its iterators may only satisfy forward or bidirectional modes, and
    will never be common. */
    template <meta::not_rvalue Filter, meta::not_rvalue True> requires (meta::range<True>)
    struct binary_filter {
        using filter_type = Filter;
        using true_type = True;

        [[no_unique_address]] impl::ref<filter_type> m_filter;
        [[no_unique_address]] impl::ref<true_type> m_true;

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) filter(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_filter);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) if_true(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_true);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto begin(this Self&& self)
            noexcept (requires{{binary_filter_iterator<range_forward, Self>{
                std::forward<Self>(self)
            }} noexcept;})
            requires (requires{{binary_filter_iterator<range_forward, Self>{
                std::forward<Self>(self)
            }};})
        {
            return binary_filter_iterator<range_forward, Self>{std::forward<Self>(self)};
        }

        [[nodiscard]] static constexpr impl::sentinel end() noexcept { return {}; }

        template <typename Self>
        [[nodiscard]] constexpr auto rbegin(this Self&& self)
            noexcept (requires(size_t size) {
                {binary_filter_iterator<range_reverse, Self>{
                    std::forward<Self>(self),
                    size
                }} noexcept;
                {self.if_true().size()} noexcept -> meta::nothrow::convertible_to<size_t>;
            } && (!meta::range<meta::as_const_ref<Filter>, bool> || requires{
                {self.filter().size()} noexcept -> meta::nothrow::convertible_to<size_t>;
            }))
            requires (requires(size_t size) {
                {binary_filter_iterator<range_reverse, Self>{
                    std::forward<Self>(self),
                    size
                }};
                {self.if_true().size()} -> meta::convertible_to<size_t>;
            } && (!meta::range<meta::as_const_ref<Filter>, bool> || requires{
                {self.filter().size()} -> meta::convertible_to<size_t>;
            }))
        {
            size_t size = self.if_true().size();
            if constexpr (meta::range<meta::as_const_ref<Filter>, bool>) {
                size = iter::min{}(size, size_t(self.filter().size()));
            }
            return binary_filter_iterator<range_reverse, Self>{
                std::forward<Self>(self),
                size
            };
        }

        [[nodiscard]] static constexpr impl::sentinel rend() noexcept { return {}; }
    };
    template <typename Filter, typename True>
    binary_filter(Filter&&, True&&) -> binary_filter<
        meta::remove_rvalue<Filter>,
        meta::remove_rvalue<True>
    >;

    template <meta::not_rvalue Filter, meta::not_rvalue True, meta::not_rvalue False>
    struct ternary_filter;

    /* In order to simplify broadcasting of ternary `where` iterators with scalar
    operands, they will be wrapped in a trivial iterator type that infinitely repeats a
    const reference to them when accessed, so that nothing needs to specialize for that
    case. */
    template <meta::lvalue T> requires (meta::is_const<T>)
    struct trivial_repeat_iterator {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using reference = T;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<value_type>;

        [[no_unique_address]] impl::ref<T> value;

        [[nodiscard]] constexpr reference operator*() const noexcept {
            return *value;
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow{**this}} noexcept;})
            requires (requires{{impl::arrow{**this}};})
        {
            return impl::arrow{**this};
        }

        [[nodiscard]] constexpr reference operator[](difference_type) const noexcept {
            return *value;
        }

        constexpr trivial_repeat_iterator& operator++() noexcept {
            return *this;
        }

        [[nodiscard]] constexpr trivial_repeat_iterator operator++(int) noexcept {
            return *this;
        }

        constexpr trivial_repeat_iterator& operator+=(difference_type) noexcept {
            return *this;
        }

        [[nodiscard]] friend constexpr trivial_repeat_iterator operator+(
            const trivial_repeat_iterator& self,
            difference_type
        ) noexcept {
            return self;
        }

        [[nodiscard]] friend constexpr trivial_repeat_iterator operator+(
            difference_type,
            const trivial_repeat_iterator& self
        ) noexcept {
            return self;
        }

        constexpr trivial_repeat_iterator& operator--() noexcept {
            return *this;
        }

        [[nodiscard]] constexpr trivial_repeat_iterator operator--(int) noexcept {
            return *this;
        }

        constexpr trivial_repeat_iterator& operator-=(difference_type) noexcept {
            return *this;
        }

        [[nodiscard]] friend constexpr trivial_repeat_iterator operator-(
            const trivial_repeat_iterator& self,
            difference_type
        ) noexcept {
            return self;
        }

        [[nodiscard]] constexpr difference_type operator-(
            const trivial_repeat_iterator& other
        ) const noexcept {
            return std::numeric_limits<difference_type>::max();
        }

        /// NOTE: these comparisons are chosen to not affect the logical conjunctions
        /// in `ternary_filter_iterator`.

        [[nodiscard]] constexpr bool operator<(
            const trivial_repeat_iterator& other
        ) const noexcept {
            return true;
        }

        [[nodiscard]] constexpr bool operator<=(
            const trivial_repeat_iterator& other
        ) const noexcept {
            return true;
        }

        [[nodiscard]] constexpr bool operator==(
            const trivial_repeat_iterator& other
        ) const noexcept {
            return false;
        }

        [[nodiscard]] constexpr bool operator!=(
            const trivial_repeat_iterator& other
        ) const noexcept {
            return true;
        }

        [[nodiscard]] constexpr bool operator>=(
            const trivial_repeat_iterator& other
        ) const noexcept {
            return true;
        }

        [[nodiscard]] constexpr bool operator>(
            const trivial_repeat_iterator& other
        ) const noexcept {
            return true;
        }
    };
    template <typename T>
    trivial_repeat_iterator(T&) -> trivial_repeat_iterator<meta::as_const_ref<T>>;

    template <typename Filter, typename True, typename False>
    struct ternary_filter_iterator {
        static constexpr bool mask = !meta::specialization_of<Filter, trivial_repeat_iterator>;
        using category = std::conditional_t<
            meta::inherits<
                meta::common_type<
                    meta::iterator_category<Filter>,
                    meta::iterator_category<True>,
                    meta::iterator_category<False>
                >,
                std::random_access_iterator_tag
            >,
            std::random_access_iterator_tag,
            std::conditional_t<
                meta::inherits<
                    meta::common_type<
                        meta::iterator_category<Filter>,
                        meta::iterator_category<True>,
                        meta::iterator_category<False>
                    >,
                    std::forward_iterator_tag
                >,
                meta::common_type<
                    meta::iterator_category<Filter>,
                    meta::iterator_category<True>,
                    meta::iterator_category<False>
                >,
                std::forward_iterator_tag
            >
        >;
        using difference_type = meta::common_type<
            meta::iterator_difference<Filter>,
            meta::iterator_difference<True>,
            meta::iterator_difference<False>
        >;
        using reference = meta::make_union<
            meta::dereference_type<meta::as_const_ref<True>>,
            meta::dereference_type<meta::as_const_ref<False>>
        >;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<value_type>;

        [[no_unique_address]] Filter filter;  // iterator over predicate function or boolean mask
        [[no_unique_address]] True if_true;  // iterator to yield from if filter evaluates true
        [[no_unique_address]] False if_false;  // iterator to yield from if filter evaluates false

    private:
        template <typename T> requires (!mask)
        constexpr reference predicate(T&& value) const
            noexcept (requires{
                {(*filter)(value)} noexcept -> meta::nothrow::truthy;
                {std::forward<T>(value)} noexcept -> meta::nothrow::convertible_to<reference>;
                {*if_false} noexcept -> meta::nothrow::convertible_to<reference>;
            })
            requires (requires{
                {(*filter)(value)} -> meta::truthy;
                {std::forward<T>(value)} -> meta::convertible_to<reference>;
                {*if_false} -> meta::convertible_to<reference>;
            })
        {
            if ((*filter)(value)) {
                return std::forward<T>(value);
            } else {
                return *if_false;
            }
        }

        template <typename T> requires (!mask)
        constexpr reference predicate(T&& value, difference_type n) const
            noexcept (requires{
                {(*filter)(value)} noexcept -> meta::nothrow::truthy;
                {std::forward<T>(value)} noexcept -> meta::nothrow::convertible_to<reference>;
                {if_false[n]} noexcept -> meta::nothrow::convertible_to<reference>;
            })
            requires (requires{
                {(*filter)(value)} -> meta::truthy;
                {std::forward<T>(value)} -> meta::convertible_to<reference>;
                {if_false[n]} -> meta::convertible_to<reference>;
            })
        {
            if ((*filter)(value)) {
                return std::forward<T>(value);
            } else {
                return if_false[n];
            }
        }

    public:
        [[nodiscard]] constexpr reference operator*() const
            noexcept (mask ?
                requires{
                    {*filter} noexcept -> meta::nothrow::truthy;
                    {*if_true} noexcept -> meta::nothrow::convertible_to<reference>;
                    {*if_false} noexcept -> meta::nothrow::convertible_to<reference>;
                } :
                requires{{predicate(*if_true)} noexcept;}
            )
            requires ((
                mask &&
                requires{
                    {*filter} -> meta::truthy;
                    {*if_true} -> meta::convertible_to<reference>;
                    {*if_false} -> meta::convertible_to<reference>;
                }
            ) || (
                !mask &&
                requires{{predicate(*if_true)};}
            ))
        {
            if constexpr (mask) {
                if (*filter) {
                    return *if_true;
                } else {
                    return *if_false;
                }
            } else {
                return predicate(*if_true);
            }
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow{**this}} noexcept;})
            requires (requires{{impl::arrow{**this}};})
        {
            return impl::arrow{**this};
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const
            noexcept (mask ?
                requires{
                    {filter[n]} noexcept -> meta::nothrow::truthy;
                    {if_true[n]} noexcept -> meta::nothrow::convertible_to<reference>;
                    {if_false[n]} noexcept -> meta::nothrow::convertible_to<reference>;
                } :
                requires{{predicate(if_true[n], n)} noexcept;}
            )
            requires ((
                mask &&
                requires{
                    {filter[n]} -> meta::truthy;
                    {if_true[n]} -> meta::convertible_to<reference>;
                    {if_false[n]} -> meta::convertible_to<reference>;
                }
            ) || (
                !mask &&
                requires{{predicate(if_true[n], n)};}
            ))
        {
            if constexpr (mask) {
                if (filter[n]) {
                    return if_true[n];
                } else {
                    return if_false[n];
                }
            } else {
                return predicate(if_true[n], n);
            }
        }

        constexpr ternary_filter_iterator& operator++()
            noexcept (requires{
                {++filter} noexcept;
                {++if_true} noexcept;
                {++if_false} noexcept;
            })
            requires (requires{
                {++filter};
                {++if_true};
                {++if_false};
            })
        {
            ++filter;
            ++if_true;
            ++if_false;
            return *this;
        }

        [[nodiscard]] constexpr ternary_filter_iterator operator++(int)
            noexcept (requires{{ternary_filter_iterator{
                filter++,
                if_true++,
                if_false++
            }} noexcept;})
            requires (requires{{ternary_filter_iterator{
                filter++,
                if_true++,
                if_false++
            }};})
        {
            return ternary_filter_iterator{
                filter++,
                if_true++,
                if_false++
            };
        }

        constexpr ternary_filter_iterator& operator+=(difference_type n)
            noexcept (requires{
                {filter += n} noexcept;
                {if_true += n} noexcept;
                {if_false += n} noexcept;
            })
            requires (requires{
                {filter += n};
                {if_true += n};
                {if_false += n};
            })
        {
            filter += n;
            if_true += n;
            if_false += n;
            return *this;
        }

        [[nodiscard]] friend constexpr ternary_filter_iterator operator+(
            const ternary_filter_iterator& self,
            difference_type n
        )
            noexcept (requires{{ternary_filter_iterator{
                self.filter + n,
                self.if_true + n,
                self.if_false + n
            }} noexcept;})
            requires (requires{{ternary_filter_iterator{
                self.filter + n,
                self.if_true + n,
                self.if_false + n
            }};})
        {
            return ternary_filter_iterator{
                self.filter + n,
                self.if_true + n,
                self.if_false + n
            };
        }

        [[nodiscard]] friend constexpr ternary_filter_iterator operator+(
            difference_type n,
            const ternary_filter_iterator& self
        )
            noexcept (requires{{ternary_filter_iterator{
                self.filter + n,
                self.if_true + n,
                self.if_false + n
            }} noexcept;})
            requires (requires{{ternary_filter_iterator{
                self.filter + n,
                self.if_true + n,
                self.if_false + n
            }};})
        {
            return ternary_filter_iterator{
                self.filter + n,
                self.if_true + n,
                self.if_false + n
            };
        }

        constexpr ternary_filter_iterator& operator--()
            noexcept (requires{
                {--filter} noexcept;
                {--if_true} noexcept;
                {--if_false} noexcept;
            })
            requires (requires{
                {--filter};
                {--if_true};
                {--if_false};
            })
        {
            --filter;
            --if_true;
            --if_false;
            return *this;
        }

        [[nodiscard]] constexpr ternary_filter_iterator operator--(int)
            noexcept (requires{{ternary_filter_iterator{
                filter--,
                if_true--,
                if_false--
            }} noexcept;})
            requires (requires{{ternary_filter_iterator{
                filter--,
                if_true--,
                if_false--
            }};})
        {
            return ternary_filter_iterator{
                filter--,
                if_true--,
                if_false--
            };
        }

        constexpr ternary_filter_iterator& operator-=(difference_type n)
            noexcept (requires{
                {filter -= n} noexcept;
                {if_true -= n} noexcept;
                {if_false -= n} noexcept;
            })
            requires (requires{
                {filter -= n};
                {if_true -= n};
                {if_false -= n};
            })
        {
            filter -= n;
            if_true -= n;
            if_false -= n;
            return *this;
        }

        [[nodiscard]] constexpr ternary_filter_iterator operator-(difference_type n) const
            noexcept (requires{{ternary_filter_iterator{
                filter - n,
                if_true - n,
                if_false - n
            }} noexcept;})
            requires (requires{{ternary_filter_iterator{
                filter - n,
                if_true - n,
                if_false - n
            }};})
        {
            return ternary_filter_iterator{
                filter - n,
                if_true - n,
                if_false - n
            };
        }

        [[nodiscard]] constexpr difference_type operator-(const ternary_filter_iterator& other) const
            noexcept (requires{{iter::min{}(
                difference_type(filter - other.filter),
                difference_type(if_true - other.if_true),
                difference_type(if_false - other.if_false)
            )} noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (requires{{iter::min{}(
                difference_type(filter - other.filter),
                difference_type(if_true - other.if_true),
                difference_type(if_false - other.if_false)
            )} -> meta::convertible_to<difference_type>;})
        {
            if constexpr (mask) {
                if constexpr (
                    meta::specialization_of<True, trivial_repeat_iterator> &&
                    meta::specialization_of<False, trivial_repeat_iterator>
                ) {
                    return difference_type(filter - other.filter);
                } else if constexpr (meta::specialization_of<True, trivial_repeat_iterator>) {
                    return iter::min{}(
                        difference_type(filter - other.filter),
                        difference_type(if_false - other.if_false)
                    );
                } else if constexpr (meta::specialization_of<False, trivial_repeat_iterator>) {
                    return iter::min{}(
                        difference_type(filter - other.filter),
                        difference_type(if_true - other.if_true)
                    );
                } else {
                    return iter::min{}(
                        difference_type(filter - other.filter),
                        difference_type(if_true - other.if_true),
                        difference_type(if_false - other.if_false)
                    );
                }
            } else if constexpr (meta::specialization_of<True, trivial_repeat_iterator>) {
                return difference_type(if_false - other.if_false);
            } else if constexpr (meta::specialization_of<False, trivial_repeat_iterator>) {
                return difference_type(if_true - other.if_true);
            } else {
                return iter::min{}(
                    difference_type(if_true - other.if_true),
                    difference_type(if_false - other.if_false)
                );
            }
        }

        [[nodiscard]] constexpr bool operator<(const ternary_filter_iterator& other) const
            noexcept (requires{{
                filter < other.filter && if_true < other.if_true && if_false < other.if_false
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                filter < other.filter && if_true < other.if_true && if_false < other.if_false
            } -> meta::convertible_to<bool>;})
        {
            return filter < other.filter && if_true < other.if_true && if_false < other.if_false;
        }

        [[nodiscard]] constexpr bool operator<=(const ternary_filter_iterator& other) const
            noexcept (requires{{
                filter <= other.filter && if_true <= other.if_true && if_false <= other.if_false
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                filter <= other.filter && if_true <= other.if_true && if_false <= other.if_false
            } -> meta::convertible_to<bool>;})
        {
            return filter <= other.filter && if_true <= other.if_true && if_false <= other.if_false;
        }

        [[nodiscard]] constexpr bool operator==(const ternary_filter_iterator& other) const
            noexcept (requires{{
                filter == other.filter || if_true == other.if_true || if_false == other.if_false
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                filter == other.filter || if_true == other.if_true || if_false == other.if_false
            } -> meta::convertible_to<bool>;})
        {
            return filter == other.filter || if_true == other.if_true || if_false == other.if_false;
        }

        [[nodiscard]] constexpr bool operator!=(const ternary_filter_iterator& other) const
            noexcept (requires{{
                filter != other.filter && if_true != other.if_true && if_false != other.if_false
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                filter != other.filter && if_true != other.if_true && if_false != other.if_false
            } -> meta::convertible_to<bool>;})
        {
            return filter != other.filter && if_true != other.if_true && if_false != other.if_false;
        }

        [[nodiscard]] constexpr bool operator>=(const ternary_filter_iterator& other) const
            noexcept (requires{{
                filter >= other.filter && if_true >= other.if_true && if_false >= other.if_false
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                filter >= other.filter && if_true >= other.if_true && if_false >= other.if_false
            } -> meta::convertible_to<bool>;})
        {
            return filter >= other.filter && if_true >= other.if_true && if_false >= other.if_false;
        }

        [[nodiscard]] constexpr bool operator>(const ternary_filter_iterator& other) const
            noexcept (requires{{
                filter > other.filter && if_true > other.if_true && if_false > other.if_false
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                filter > other.filter && if_true > other.if_true && if_false > other.if_false
            } -> meta::convertible_to<bool>;})
        {
            return filter > other.filter && if_true > other.if_true && if_false > other.if_false;
        }
    };
    template <typename Mask, typename True, typename False>
    ternary_filter_iterator(Mask, True, False) -> ternary_filter_iterator<Mask, True, False>;

    template <typename T>
    constexpr size_t ternary_filter_size(T&& value)
        noexcept (!meta::range<T> || requires{
            {size_t(std::forward<T>(value).size())} noexcept;
        })
        requires (!meta::range<T> || requires{
            {size_t(std::forward<T>(value).size())};
        })
    {
        if constexpr (meta::range<T>) {
            return size_t(std::forward<T>(value).size());
        } else {
            return std::numeric_limits<size_t>::max();
        }
    }

    template <typename T>
    constexpr ssize_t ternary_filter_ssize(T&& value)
        noexcept (!meta::range<T> || requires{
            {ssize_t(std::forward<T>(value).ssize())} noexcept;
        })
        requires (!meta::range<T> || requires{
            {ssize_t(std::forward<T>(value).ssize())};
        })
    {
        if constexpr (meta::range<T>) {
            return ssize_t(std::forward<T>(value).ssize());
        } else {
            return std::numeric_limits<ssize_t>::max();
        }
    }

    template <typename T>
    constexpr bool ternary_filter_empty(T&& value)
        noexcept (!meta::range<T> || requires{
            {std::forward<T>(value).empty()} noexcept -> meta::nothrow::convertible_to<bool>;
        })
        requires (!meta::range<T> || requires{
            {std::forward<T>(value).empty()} -> meta::convertible_to<bool>;
        })
    {
        if constexpr (meta::range<T>) {
            return std::forward<T>(value).empty();
        } else {
            return false;
        }
    }

    template <typename T>
    struct _ternary_filter_front_type { using type = T; };
    template <meta::range T>
    struct _ternary_filter_front_type<T> { using type = meta::front_type<T>; };
    template <typename Self>
    using ternary_filter_front_type = meta::make_union<
        typename _ternary_filter_front_type<decltype((std::declval<Self>().if_true()))>::type,
        typename _ternary_filter_front_type<decltype((std::declval<Self>().if_false()))>::type
    >;

    template <typename T>
    constexpr decltype(auto) ternary_filter_front(T&& value)
        noexcept (!meta::range<T> || requires{{std::forward<T>(value).front()} noexcept;})
        requires (!meta::range<T> || requires{{std::forward<T>(value).front()};})
    {
        if constexpr (meta::range<T>) {
            return (std::forward<T>(value).front());
        } else {
            return (std::forward<T>(value));
        }
    }

    template <typename T>
    struct _ternary_filter_subscript_type { using type = T; };
    template <meta::range T>
    struct _ternary_filter_subscript_type<T> { using type = meta::subscript_type<T, size_t>; };
    template <typename Self>
    using ternary_filter_subscript_type = meta::make_union<
        typename _ternary_filter_subscript_type<decltype((std::declval<Self>().if_true()))>::type,
        typename _ternary_filter_subscript_type<decltype((std::declval<Self>().if_false()))>::type
    >;

    template <typename T>
    constexpr decltype(auto) ternary_filter_subscript(T&& value, size_t index)
        noexcept (!meta::range<T> || requires{{std::forward<T>(value)[index]} noexcept;})
        requires (!meta::range<T> || requires{{std::forward<T>(value)[index]};})
    {
        if constexpr (meta::range<T>) {
            return (std::forward<T>(value)[index]);
        } else {
            return (std::forward<T>(value));
        }
    }

    template <typename Self>
    constexpr size_t ternary_filter_tuple_size = 0;
    template <meta::specialization_of<ternary_filter> Self>
        requires (meta::unqualified<Self> && Self::tuple_like && Self::mask)
    constexpr size_t ternary_filter_tuple_size<Self> = iter::min{}(
        meta::tuple_size<meta::as_range_or_scalar<typename Self::filter_type>>,
        meta::range<typename Self::true_type> ?
            meta::tuple_size<meta::as_range_or_scalar<typename Self::true_type>> :
            std::numeric_limits<size_t>::max(),
        meta::range<typename Self::false_type> ?
            meta::tuple_size<meta::as_range_or_scalar<typename Self::false_type>> :
            std::numeric_limits<size_t>::max()
    );
    template <meta::specialization_of<ternary_filter> Self>
        requires (meta::unqualified<Self> && Self::tuple_like && !Self::mask)
    constexpr size_t ternary_filter_tuple_size<Self> = iter::min{}(
        meta::tuple_size<meta::as_range_or_scalar<typename Self::true_type>>,
        meta::range<typename Self::false_type> ?
            meta::tuple_size<meta::as_range_or_scalar<typename Self::false_type>> :
            std::numeric_limits<size_t>::max()
    );

    template <size_t I, typename T>
    struct _ternary_filter_get_type { using type = T; };
    template <size_t I, meta::tuple_like T>
    struct _ternary_filter_get_type<I, T> { using type = meta::get_type<T, I>; };
    template <size_t I, typename Self>
    using ternary_filter_get_type = meta::make_union<
        typename _ternary_filter_get_type<I, decltype((std::declval<Self>().if_true()))>::type,
        typename _ternary_filter_get_type<I, decltype((std::declval<Self>().if_false()))>::type
    >;

    template <size_t I, typename T>
    constexpr decltype(auto) ternary_filter_get(T&& value)
        noexcept (!meta::range<T> || requires{
            {std::forward<T>(value).template get<I>()} noexcept;
        })
        requires (!meta::range<T> || requires{
            {std::forward<T>(value).template get<I>()};
        })
    {
        if constexpr (meta::range<T>) {
            return (std::forward<T>(value).template get<I>());
        } else {
            return (std::forward<T>(value));
        }
    }

    template <bool trivial = false, typename T>
    constexpr auto ternary_filter_begin(T&& value)
        noexcept (!meta::range<T> || requires{{std::forward<T>(value).begin()} noexcept;})
        requires (!meta::range<T> || requires{{std::forward<T>(value).begin()};})
    {
        if constexpr (meta::range<T>) {
            return std::forward<T>(value).begin();
        } else {
            return impl::trivial_repeat_iterator(value);
        }
    }

    template <typename T>
    constexpr auto ternary_filter_end(T&& value)
        noexcept (!meta::range<T> || requires{{std::forward<T>(value).end()} noexcept;})
        requires (!meta::range<T> || requires{{std::forward<T>(value).end()};})
    {
        if constexpr (meta::range<T>) {
            return std::forward<T>(value).end();
        } else {
            return impl::trivial_repeat_iterator(value);
        }
    }

    template <typename T>
    constexpr auto ternary_filter_rbegin(T&& value, ssize_t size)
        noexcept (!meta::range<T> || requires(meta::rbegin_type<T> it) {
            {std::forward<T>(value).rbegin()} noexcept;
            {it += value.ssize() - size} noexcept;
        })
        requires (!meta::range<T> || requires(meta::rbegin_type<T> it) {
            {std::forward<T>(value).rbegin()};
            {it += value.ssize() - size};
        })
    {
        if constexpr (meta::range<T>) {
            auto it = std::forward<T>(value).rbegin();
            it += value.ssize() - size;
            return it;
        } else {
            return impl::trivial_repeat_iterator(value);
        }
    }

    template <typename T>
    constexpr auto ternary_filter_rend(T&& value)
        noexcept (!meta::range<T> || requires{{std::forward<T>(value).rend()} noexcept;})
        requires (!meta::range<T> || requires{{std::forward<T>(value).rend()};})
    {
        if constexpr (meta::range<T>) {
            return std::forward<T>(value).rend();
        } else {
            return impl::trivial_repeat_iterator(value);
        }
    }

    /* Ternary `where{}` expressions may filter based on a boolean mask or predicate,
    in which case the result is a range whose length equals the minimum length of the 3
    operands, with scalars being infinitely repeated.  If none of the operands are
    ranges, then the result will be a single value.  If a predicate is given, then it
    will be called with each value from the `True` operand to determine whether to
    select that value or the corresponding value from the `False` operand.  If either
    `True` or `False` is not a range, then it implies broadcasting of that value across
    the length of the other operands.  A special case exists for a boolean predicate
    with a non-range `True` value, which is treated as a range of length 1 or 0 (if
    `False` is an empty range).

    Since the size can be known, the resulting range may be tuple-like and indexed if
    all of the operands support these operations. */
    template <meta::not_rvalue Filter, meta::not_rvalue True, meta::not_rvalue False>
    struct ternary_filter {
        static constexpr bool mask = meta::range<meta::as_const_ref<Filter>, bool>;
        using filter_type = std::conditional_t<mask, meta::as_range<Filter>, Filter>;
        using true_type = True;
        using false_type = False;

        static constexpr bool tuple_like =
            meta::tuple_like<meta::as_range_or_scalar<filter_type>> &&
            meta::tuple_like<meta::as_range_or_scalar<true_type>> &&
            meta::tuple_like<meta::as_range_or_scalar<false_type>>;

        [[no_unique_address]] impl::ref<filter_type> m_filter;
        [[no_unique_address]] impl::ref<true_type> m_true;
        [[no_unique_address]] impl::ref<false_type> m_false;

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) filter(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_filter);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) if_true(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_true);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) if_false(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_false);
        }

    private:
        template <ssize_t I>
        static constexpr size_t normalize = impl::normalize_index<
            ternary_filter_tuple_size<ternary_filter>,
            I
        >();

        template <typename Self>
        using front_type = ternary_filter_front_type<Self>;
        template <typename Self>
        using subscript_type = ternary_filter_subscript_type<Self>;
        template <ssize_t I, typename Self>
        using get_type = ternary_filter_get_type<normalize<I>, Self>;

        template <typename Self, typename T> requires (!mask)
        static constexpr front_type<Self> predicate_front(Self&& self, T&& value)
            noexcept (requires{
                {self.filter()(value)} noexcept -> meta::nothrow::truthy;
                {
                    std::forward<T>(value)
                } noexcept -> meta::nothrow::convertible_to<front_type<Self>>;
                {
                    ternary_filter_front(std::forward<Self>(self).if_false())
                } noexcept -> meta::nothrow::convertible_to<front_type<Self>>;
            })
        {
            if (self.filter()(value)) {
                return std::forward<T>(value);
            } else {
                return ternary_filter_front(std::forward<Self>(self).if_false());
            }
        }

        template <typename Self, typename T> requires (!mask)
        static constexpr subscript_type<Self> predicate_subscript(
            Self&& self,
            T&& value,
            size_t index
        )
            noexcept (requires{
                {self.filter()(value)} noexcept -> meta::nothrow::truthy;
                {
                    std::forward<T>(value)
                } noexcept -> meta::nothrow::convertible_to<subscript_type<Self>>;
                {
                    ternary_filter_subscript(std::forward<Self>(self).if_false(), index)
                } noexcept -> meta::nothrow::convertible_to<subscript_type<Self>>;
            })
        {
            if (self.filter()(value)) {
                return std::forward<T>(value);
            } else {
                return ternary_filter_subscript(std::forward<Self>(self).if_false(), index);
            }
        }

        template <size_t I, typename Self, typename T>
        static constexpr get_type<I, Self> predicate_get(Self&& self, T&& value)
            noexcept (requires{
                {self.filter()(value)} noexcept -> meta::nothrow::truthy;
                {
                    std::forward<T>(value)
                } noexcept -> meta::nothrow::convertible_to<get_type<I, Self>>;
                {
                    ternary_filter_get<I>(std::forward<Self>(self).if_false())
                } noexcept -> meta::nothrow::convertible_to<get_type<I, Self>>;
            })
        {
            if (self.filter()(value)) {
                return std::forward<T>(value);
            } else {
                return ternary_filter_get<I>(std::forward<Self>(self).if_false());
            }
        }

    public:
        [[nodiscard]] constexpr size_t size() const
            noexcept (requires{{iter::min{}(
                ternary_filter_size(filter()),
                ternary_filter_size(if_true()),
                ternary_filter_size(if_false())
            )} noexcept;})
            requires (requires{{iter::min{}(
                ternary_filter_size(filter()),
                ternary_filter_size(if_true()),
                ternary_filter_size(if_false())
            )};})
        {
            if constexpr (tuple_like) {
                return ternary_filter_tuple_size<ternary_filter>;
            } else if constexpr (mask) {
                return iter::min{}(
                    ternary_filter_size(filter()),
                    ternary_filter_size(if_true()),
                    ternary_filter_size(if_false())
                );
            } else {
                return iter::min{}(
                    meta::range<true_type> ? ternary_filter_size(if_true()) : 1,
                    ternary_filter_size(if_false())
                );
            }
        }

        [[nodiscard]] constexpr ssize_t ssize() const
            noexcept (requires{{iter::min{}(
                ternary_filter_ssize(filter()),
                ternary_filter_ssize(if_true()),
                ternary_filter_ssize(if_false())
            )} noexcept;})
            requires (requires{{iter::min{}(
                ternary_filter_ssize(filter()),
                ternary_filter_ssize(if_true()),
                ternary_filter_ssize(if_false())
            )};})
        {
            if constexpr (tuple_like) {
                return ssize_t(ternary_filter_tuple_size<ternary_filter>);
            } else if constexpr (mask) {
                return iter::min{}(
                    ternary_filter_ssize(filter()),
                    ternary_filter_ssize(if_true()),
                    ternary_filter_ssize(if_false())
                );
            } else {
                return iter::min{}(
                    meta::range<true_type> ? ternary_filter_ssize(if_true()) : 1,
                    ternary_filter_ssize(if_false())
                );
            }
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{
                ternary_filter_empty(filter()) ||
                ternary_filter_empty(if_true()) ||
                ternary_filter_empty(if_false())
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                ternary_filter_empty(filter()) ||
                ternary_filter_empty(if_true()) ||
                ternary_filter_empty(if_false())
            } -> meta::convertible_to<bool>;})
        {
            if constexpr (tuple_like) {
                return ternary_filter_tuple_size<ternary_filter> == 0;
            } else {
                return
                    ternary_filter_empty(filter()) ||
                    ternary_filter_empty(if_true()) ||
                    ternary_filter_empty(if_false());
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr front_type<Self> front(this Self&& self)
            noexcept (mask ?
                requires{
                    {self.filter().front()} noexcept -> meta::nothrow::truthy;
                    {ternary_filter_front(
                        std::forward<Self>(self).if_true()
                    )} noexcept -> meta::nothrow::convertible_to<front_type<Self>>;
                    {ternary_filter_front(
                        std::forward<Self>(self).if_false()
                    )} noexcept -> meta::nothrow::convertible_to<front_type<Self>>;
                } :
                requires{{predicate_front(
                    std::forward<Self>(self),
                    ternary_filter_front(std::forward<Self>(self).if_true())
                )} noexcept;}
            )
            requires ((
                mask &&
                requires{
                    {self.filter().front()} -> meta::truthy;
                    {ternary_filter_front(
                        std::forward<Self>(self).if_true()
                    )} -> meta::convertible_to<front_type<Self>>;
                    {ternary_filter_front(
                        std::forward<Self>(self).if_false()
                    )} -> meta::convertible_to<front_type<Self>>;
                }
            ) || (
                !mask &&
                requires(decltype((ternary_filter_front(
                    std::forward<Self>(self).if_true()
                ))) value) {
                    {self.filter()(value)} -> meta::truthy;
                    {
                        std::forward<decltype(value)>(value)
                    } -> meta::convertible_to<front_type<Self>>;
                    {ternary_filter_front(
                        std::forward<Self>(self).if_false()
                    )} -> meta::convertible_to<front_type<Self>>;
                }
            ))
        {
            if constexpr (mask) {
                if (self.filter().front()) {
                    return ternary_filter_front(std::forward<Self>(self).if_true());
                } else {
                    return ternary_filter_front(std::forward<Self>(self).if_false());
                }
            } else {
                return predicate_front(
                    std::forward<Self>(self),
                    ternary_filter_front(std::forward<Self>(self).if_true())
                );
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr subscript_type<Self> back(this Self&& self)
            noexcept (requires{
                {self.size() - 1} noexcept -> meta::nothrow::convertible_to<size_t>;
            } && (mask ?
                requires(size_t m) {
                    {self.filter()[m]} noexcept -> meta::nothrow::truthy;
                    {ternary_filter_subscript(
                        std::forward<Self>(self).if_true(),
                        m
                    )} noexcept -> meta::nothrow::convertible_to<front_type<Self>>;
                    {ternary_filter_subscript(
                        std::forward<Self>(self).if_true(),
                        m
                    )} noexcept -> meta::nothrow::convertible_to<front_type<Self>>;
                } :
                requires(size_t m) {{predicate_subscript(
                    std::forward<Self>(self),
                    ternary_filter_subscript(
                        std::forward<Self>(self).if_true(),
                        m
                    ),
                    m
                )} noexcept;}
            ))
            requires (requires{
                {self.size() - 1} -> meta::convertible_to<size_t>;
            } && (
                mask &&
                requires(size_t m) {
                    {self.filter()[m]} -> meta::truthy;
                    {ternary_filter_subscript(
                        std::forward<Self>(self).if_true(),
                        m
                    )} -> meta::convertible_to<subscript_type<Self>>;
                    {ternary_filter_subscript(
                        std::forward<Self>(self).if_true(),
                        m
                    )} -> meta::convertible_to<subscript_type<Self>>;
                }
            ) || (
                !mask &&
                requires(decltype((ternary_filter_subscript(
                    std::forward<Self>(self).if_true(),
                    self.size() - 1
                ))) value, size_t m) {
                    {self.filter()(value)} -> meta::truthy;
                    {
                        std::forward<decltype(value)>(value)
                    } -> meta::convertible_to<subscript_type<Self>>;
                    {ternary_filter_subscript(
                        std::forward<Self>(self).if_false(),
                        m
                    )} -> meta::convertible_to<subscript_type<Self>>;
                }
            ))
        {
            size_t m = self.size() - 1;
            if constexpr (mask) {
                if (self.filter()[m]) {
                    return ternary_filter_subscript(
                        std::forward<Self>(self).if_true(),
                        m
                    );
                } else {
                    return ternary_filter_subscript(
                        std::forward<Self>(self).if_false(),
                        m
                    );
                }
            } else {
                return predicate_subscript(
                    std::forward<Self>(self),
                    ternary_filter_subscript(
                        std::forward<Self>(self).if_true(),
                        m
                    ),
                    m
                );
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr subscript_type<Self> operator[](this Self&& self, ssize_t n)
            noexcept (requires{
                {size_t(impl::normalize_index(self.ssize(), n))} noexcept;
            } && (mask ?
                requires(size_t m) {
                    {self.filter()[m]} noexcept -> meta::nothrow::truthy;
                    {ternary_filter_subscript(
                        std::forward<Self>(self).if_true(),
                        m
                    )} noexcept -> meta::nothrow::convertible_to<subscript_type<Self>>;
                    {ternary_filter_subscript(
                        std::forward<Self>(self).if_false(),
                        m
                    )} noexcept -> meta::nothrow::convertible_to<subscript_type<Self>>;
                } :
                requires(size_t m) {{predicate_subscript(
                    std::forward<Self>(self),
                    ternary_filter_subscript(
                        std::forward<Self>(self).if_true(),
                        m
                    ),
                    m
                )} noexcept;}
            ))
            requires (requires{
                {impl::normalize_index(self.ssize(), n)} -> meta::explicitly_convertible_to<size_t>;
            } && (
                mask &&
                requires(size_t m) {
                    {self.filter()[m]} -> meta::truthy;
                    {ternary_filter_subscript(
                        std::forward<Self>(self).if_true(),
                        m
                    )} -> meta::convertible_to<subscript_type<Self>>;
                    {ternary_filter_subscript(
                        std::forward<Self>(self).if_true(),
                        m
                    )} -> meta::convertible_to<subscript_type<Self>>;
                }
            ) || (
                !mask &&
                requires(decltype((ternary_filter_subscript(
                    std::forward<Self>(self).if_true(),
                    size_t(impl::normalize_index(self.ssize(), n))
                ))) value, size_t m) {
                    {self.filter()(value)} -> meta::truthy;
                    {
                        std::forward<decltype(value)>(value)
                    } -> meta::convertible_to<subscript_type<Self>>;
                    {ternary_filter_subscript(
                        std::forward<Self>(self).if_false(),
                        m
                    )} -> meta::convertible_to<subscript_type<Self>>;
                }
            ))
        {
            size_t m = size_t(impl::normalize_index(self.ssize(), n));
            if constexpr (mask) {
                if (self.filter()[m]) {
                    return ternary_filter_subscript(
                        std::forward<Self>(self).if_true(),
                        m
                    );
                } else {
                    return ternary_filter_subscript(
                        std::forward<Self>(self).if_false(),
                        m
                    );
                }
            } else {
                return predicate_subscript(
                    std::forward<Self>(self),
                    ternary_filter_subscript(
                        std::forward<Self>(self).if_true(),
                        m
                    ),
                    m
                );
            }
        }

        template <ssize_t I, typename Self>
            requires (
                meta::tuple_like<meta::as_range_or_scalar<filter_type>> &&
                meta::tuple_like<meta::as_range_or_scalar<true_type>> &&
                meta::tuple_like<meta::as_range_or_scalar<false_type>> &&
                impl::valid_index<ternary_filter_tuple_size<ternary_filter>, I>
            )
        [[nodiscard]] constexpr get_type<I, Self> get(this Self&& self)
            noexcept (mask ?
                requires{
                    {self.filter().template get<normalize<I>>()} noexcept -> meta::nothrow::truthy;
                    {ternary_filter_get<normalize<I>>(
                        std::forward<Self>(self).if_true()
                    )} noexcept -> meta::nothrow::convertible_to<get_type<I, Self>>;
                    {ternary_filter_get<normalize<I>>(
                        std::forward<Self>(self).if_false()
                    )} noexcept -> meta::nothrow::convertible_to<get_type<I, Self>>;
                } :
                requires{{predicate_get<normalize<I>>(
                    std::forward<Self>(self),
                    ternary_filter_get<normalize<I>>(
                        std::forward<Self>(self).if_true()
                    )
                )} noexcept;}
            )
            requires ((
                mask &&
                requires{
                    {self.filter().template get<normalize<I>>()} -> meta::truthy;
                    {ternary_filter_get<normalize<I>>(
                        std::forward<Self>(self).if_true()
                    )} -> meta::convertible_to<get_type<I, Self>>;
                    {ternary_filter_get<normalize<I>>(
                        std::forward<Self>(self).if_false()
                    )} -> meta::convertible_to<get_type<I, Self>>;
                }
            ) || (
                !mask &&
                requires(decltype((ternary_filter_get<normalize<I>>(
                    std::forward<Self>(self).if_true()
                ))) value) {
                    {self.filter()(value)} -> meta::truthy;
                    {
                        std::forward<decltype(value)>(value)
                    } -> meta::convertible_to<get_type<I, Self>>;
                    {ternary_filter_get<normalize<I>>(
                        std::forward<Self>(self).if_false()
                    )} -> meta::convertible_to<get_type<I, Self>>;
                }
            ))
        {
            if constexpr (mask) {
                if (self.filter().template get<normalize<I>>()) {
                    return ternary_filter_get<normalize<I>>(
                        std::forward<Self>(self).if_true()
                    );
                } else {
                    return ternary_filter_get<normalize<I>>(
                        std::forward<Self>(self).if_false()
                    );
                }
            } else {
                return predicate_get<normalize<I>>(
                    std::forward<Self>(self),
                    ternary_filter_get<normalize<I>>(
                        std::forward<Self>(self).if_true()
                    )
                );
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr auto begin(this Self&& self)
            noexcept (requires{{ternary_filter_iterator{
                .filter = ternary_filter_begin(std::forward<Self>(self).filter()),
                .if_true = ternary_filter_begin(std::forward<Self>(self).if_true()),
                .if_false = ternary_filter_begin(std::forward<Self>(self).if_false()),
            }} noexcept;})
            requires (requires{{ternary_filter_iterator{
                .filter = ternary_filter_begin(std::forward<Self>(self).filter()),
                .if_true = ternary_filter_begin(std::forward<Self>(self).if_true()),
                .if_false = ternary_filter_begin(std::forward<Self>(self).if_false()),
            }};})
        {
            if constexpr (mask || meta::range<true_type>) {
                return ternary_filter_iterator{
                    .filter = ternary_filter_begin(std::forward<Self>(self).filter()),
                    .if_true = ternary_filter_begin(std::forward<Self>(self).if_true()),
                    .if_false = ternary_filter_begin(std::forward<Self>(self).if_false()),
                };
            } else {
                return ternary_filter_iterator{
                    .filter = ternary_filter_begin(std::forward<Self>(self).filter()),
                    .if_true = impl::trivial_iterator(std::forward<Self>(self).if_true()),
                    .if_false = ternary_filter_begin(std::forward<Self>(self).if_false()),
                };
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr auto end(this Self&& self)
            noexcept (requires{{ternary_filter_iterator{
                .filter = ternary_filter_end(std::forward<Self>(self).filter()),
                .if_true = ternary_filter_end(std::forward<Self>(self).if_true()),
                .if_false = ternary_filter_end(std::forward<Self>(self).if_false()),
            }} noexcept;})
            requires (requires{{ternary_filter_iterator{
                .filter = ternary_filter_end(std::forward<Self>(self).filter()),
                .if_true = ternary_filter_end(std::forward<Self>(self).if_true()),
                .if_false = ternary_filter_end(std::forward<Self>(self).if_false()),
            }};})
        {
            if constexpr (mask || meta::range<true_type>) {
                return ternary_filter_iterator{
                    .filter = ternary_filter_end(std::forward<Self>(self).filter()),
                    .if_true = ternary_filter_end(std::forward<Self>(self).if_true()),
                    .if_false = ternary_filter_end(std::forward<Self>(self).if_false()),
                };
            } else {
                return ternary_filter_iterator{
                    .filter = ternary_filter_end(std::forward<Self>(self).filter()),
                    .if_true = impl::trivial_iterator(std::forward<Self>(self).if_true()) + 1,
                    .if_false = ternary_filter_end(std::forward<Self>(self).if_false()),
                };
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr auto rbegin(this Self&& self)
            noexcept (requires{{ternary_filter_iterator{
                .filter = ternary_filter_rbegin(
                    std::forward<Self>(self).filter(),
                    self.ssize()
                ),
                .if_true = ternary_filter_rbegin(
                    std::forward<Self>(self).if_true(),
                    self.ssize()
                ),
                .if_false = ternary_filter_rbegin(
                    std::forward<Self>(self).if_false(),
                    self.ssize()
                ),
            }} noexcept;})
            requires (requires{{ternary_filter_iterator{
                .filter = ternary_filter_rbegin(
                    std::forward<Self>(self).filter(),
                    self.ssize()
                ),
                .if_true = ternary_filter_rbegin(
                    std::forward<Self>(self).if_true(),
                    self.ssize()
                ),
                .if_false = ternary_filter_rbegin(
                    std::forward<Self>(self).if_false(),
                    self.ssize()
                ),
            }};})
        {
            ssize_t size = self.ssize();
            if constexpr (mask || meta::range<true_type>) {
                return ternary_filter_iterator{
                    .filter = ternary_filter_rbegin(
                        std::forward<Self>(self).filter(),
                        size
                    ),
                    .if_true = ternary_filter_rbegin(
                        std::forward<Self>(self).if_true(),
                        size
                    ),
                    .if_false = ternary_filter_rbegin(
                        std::forward<Self>(self).if_false(),
                        size
                    ),
                };
            } else {
                return ternary_filter_iterator{
                    .filter = ternary_filter_rbegin(
                        std::forward<Self>(self).filter(),
                        size
                    ),
                    .if_true = std::make_reverse_iterator(
                        impl::trivial_iterator(std::forward<Self>(self).if_true()) + 1
                    ),
                    .if_false = ternary_filter_rbegin(
                        std::forward<Self>(self).if_false(),
                        size
                    ),
                };
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr auto rend(this Self&& self)
            noexcept (requires{{ternary_filter_iterator{
                .filter = ternary_filter_rend(std::forward<Self>(self).filter()),
                .if_true = ternary_filter_rend(std::forward<Self>(self).if_true()),
                .if_false = ternary_filter_rend(std::forward<Self>(self).if_false()),
            }} noexcept;})
            requires (requires{{ternary_filter_iterator{
                .filter = ternary_filter_rend(std::forward<Self>(self).filter()),
                .if_true = ternary_filter_rend(std::forward<Self>(self).if_true()),
                .if_false = ternary_filter_rend(std::forward<Self>(self).if_false()),
            }};})
        {
            if constexpr (mask || meta::range<true_type>) {
                return ternary_filter_iterator{
                    .filter = ternary_filter_rend(std::forward<Self>(self).filter()),
                    .if_true = ternary_filter_rend(std::forward<Self>(self).if_true()),
                    .if_false = ternary_filter_rend(std::forward<Self>(self).if_false()),
                };
            } else {
                return ternary_filter_iterator{
                    .filter = ternary_filter_rend(std::forward<Self>(self).filter()),
                    .if_true = std::make_reverse_iterator(
                        impl::trivial_iterator(std::forward<Self>(self).if_true())
                    ),
                    .if_false = ternary_filter_rend(std::forward<Self>(self).if_false()),
                };
            }
        }
    };
    template <typename Filter, typename True, typename False>
    ternary_filter(Filter&&, True&&, False&&) -> ternary_filter<
        meta::remove_rvalue<Filter>,
        meta::remove_rvalue<True>,
        meta::remove_rvalue<False>
    >;

}


namespace iter {

    /* A function object that conditionally filters or replaces elements from a range
    based on a boolean predicate or mask.

    `where{}` algorithms can be used in one of two ways, depending on the number and
    type of the arguments provided during construction and invocation:

        1.  If the initializer to `where{}` is a range, then it must yield values that
            are implicitly convertible to `bool`, where `true` indicates that the
            original element should be retained, and `false` indicates that it should
            omitted or replaced.
        2.  If the initializer to `where{}` is not a range, then it must be a function
            object that is callable with a const reference to a single argument and
            returns a value implicitly convertible to `bool`.  If the `where{}` object
            is later invoked with one or more arguments, this function will be applied
            to each element from the first argument to determine whether to retain or
            omit/replace that element.

    In both cases, the `where{}` object can be invoked with either one or two
    arguments:

        a.  If invoked with a single argument, then that argument will first be
            converted to a range and then filtered according to the predicate or mask,
            omitting any `false` indices.  Since it is not generically possible to know
            the number of true or false values in the mask or predicate ahead of time,
            the resulting range will not support size or indexing operations, and will
            never be tuple-like.
        b.  If invoked with two arguments, then replacements will be drawn from the
            first where the mask or predicate evaluates to `true` and from the second
            where it evaluates to `false`.  If the initializer is a function predicate,
            then it will be applied only to elements from the first argument, with
            non-ranges being treated as ranges of length 1.  If the initializer is a
            boolean mask instead, then non-range arguments will be broadcast across the
            length of the mask.  In either case, the length of the resulting range will
            always be equal to the smallest of the range operands, assuming they all
            have a defined size.  If none of the arguments are ranges, then the result
            will be a range of length 1.

    Note that in the ternary case, all range arguments will be co-iterated in parallel
    rather than in series, meaning that unchosen values will effectively be skipped.
    `iter::merge{}` makes the opposite choice, and only advances the range from which
    values are drawn, which may be more appropriate depending on circumstance.

    Many languages call this operation "select", "choose", or "filter", but Bertrand's
    implementation most resembles the behavior of NumPy's `where()` function, hence the
    name.  It can also be thought of as a kind of vectorized `if` (binary) or `if/else`
    (ternary) expression, with the aforementioned caveats around broadcasting. */
    template <meta::not_rvalue Filter>
    struct where {
        [[no_unique_address]] Filter filter;

        template <typename Self, typename T>
        [[nodiscard]] constexpr auto operator()(this Self&& self, T&& r)
            noexcept (requires{{range{impl::binary_filter{
                std::forward<Self>(self).filter,
                meta::to_range(std::forward<T>(r))
            }}} noexcept;})
            requires ((meta::range<meta::as_const_ref<Filter>, bool> || (
                !meta::range<Filter> &&
                meta::call_returns<
                    bool,
                    meta::as_const_ref<Filter>,
                    meta::as_const_ref<meta::yield_type<T>>
                >
            )) && requires{{range{impl::binary_filter{
                std::forward<Self>(self).filter,
                meta::to_range(std::forward<T>(r))
            }}};})
        {
            return range{impl::binary_filter{
                std::forward<Self>(self).filter,
                meta::to_range(std::forward<T>(r))
            }};
        }

        template <typename Self, typename True, typename False>
        [[nodiscard]] constexpr auto operator()(this Self&& self, True&& if_true, False&& if_false)
            noexcept (requires{{range{impl::ternary_filter{
                std::forward<Self>(self).filter,
                std::forward<True>(if_true),
                std::forward<False>(if_false)
            }}} noexcept;})
            requires ((meta::range<meta::as_const_ref<Filter>, bool> || (
                !meta::range<Filter> &&
                meta::call_returns<
                    bool,
                    meta::as_const_ref<Filter>,
                    meta::as_const_ref<meta::yield_type<meta::as_range_or_scalar<True>>>
                >
            )) && requires{{range{impl::ternary_filter{
                std::forward<Self>(self).filter,
                std::forward<True>(if_true),
                std::forward<False>(if_false)
            }}};})
        {
            return range{impl::ternary_filter{
                std::forward<Self>(self).filter,
                std::forward<True>(if_true),
                std::forward<False>(if_false)
            }};
        }
    };

    template <typename F>
    where(F&&) -> where<meta::remove_rvalue<F>>;

}


}


namespace std {

    namespace ranges {

        template <typename F, typename True>
        constexpr bool enable_borrowed_range<bertrand::impl::binary_filter<F, True>> =
            (bertrand::meta::lvalue<F> || enable_borrowed_range<bertrand::meta::unqualify<F>>) &&
            (bertrand::meta::lvalue<True> || enable_borrowed_range<bertrand::meta::unqualify<True>>);

        template <typename F, typename True, typename False>
        constexpr bool enable_borrowed_range<bertrand::impl::ternary_filter<F, True, False>> =
            (bertrand::meta::lvalue<F> || enable_borrowed_range<bertrand::meta::unqualify<F>>) &&
            (bertrand::meta::lvalue<True> || enable_borrowed_range<bertrand::meta::unqualify<True>>) &&
            (bertrand::meta::lvalue<False> || enable_borrowed_range<bertrand::meta::unqualify<False>>);

    }

    template <typename Filter, typename True, typename False>
        requires (bertrand::impl::ternary_filter<Filter, True, False>::tuple_like)
    struct tuple_size<bertrand::impl::ternary_filter<Filter, True, False>> : std::integral_constant<
        size_t,
        bertrand::impl::ternary_filter_tuple_size<
            bertrand::impl::ternary_filter<Filter, True, False>
        >
    > {};

    template <size_t I, typename Filter, typename True, typename False>
        requires (I < tuple_size<bertrand::impl::ternary_filter<Filter, True, False>>::value)
    struct tuple_element<I, bertrand::impl::ternary_filter<Filter, True, False>> {
        using type = bertrand::meta::remove_rvalue<decltype((
            std::declval<bertrand::impl::ternary_filter<Filter, True, False>>().template get<I>()
        ))>;
    };

}


#endif  // BERTRAND_ITER_WHERE_H