#ifndef BERTRAND_ITER_WHERE_H
#define BERTRAND_ITER_WHERE_H

#include "bertrand/iter/range.h"


namespace bertrand {


namespace impl {

    /// TODO: revise documentation to note that the binary form is only available
    /// if the `true` value is a range.

    /// TODO: `Self` must be allowed to be an rvalue, in which case we move the
    /// results out of it (but still store an lvalue internally).

    /* Binary filters require the iterator to skip over false elements, thereby
    preventing it from being a random access iterator, since the location of these
    skips cannot be known ahead of time.  The iterator is thus restricted only to
    forward or bidirectional modes. */
    template <range_direction Dir, meta::lvalue Self>
    struct binary_filter_iterator {
        using function_type = meta::remove_reference<decltype((std::declval<Self>().filter()))>;
        using begin_type = decltype(Dir::begin(std::declval<Self>().container()));
        using end_type = decltype(Dir::end(std::declval<Self>().container()));
        using category = std::conditional_t<
            meta::inherits<meta::iterator_category<begin_type>, std::bidirectional_iterator_tag>,
            std::bidirectional_iterator_tag,
            std::forward_iterator_tag
        >;
        using difference_type = meta::iterator_difference<begin_type>;
        using reference = meta::dereference_type<meta::as_const_ref<begin_type>>;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<value_type>;

        [[no_unique_address]] function_type* func = nullptr;
        [[no_unique_address]] difference_type index = 0;
        [[no_unique_address]] begin_type begin;
        [[no_unique_address]] end_type end;

        [[nodiscard]] constexpr binary_filter_iterator() = default;
        [[nodiscard]] constexpr binary_filter_iterator(Self self)
            noexcept (requires{
                {std::addressof(self.filter())} noexcept;
                {Dir::begin(self.container())} noexcept;
                {Dir::end(self.container())} noexcept;
                {begin != end} noexcept -> meta::nothrow::truthy;
                {(*func)(*begin)} noexcept -> meta::nothrow::truthy;
                {++begin} noexcept;
            })
            requires (requires{
                {std::addressof(self.filter())};
                {Dir::begin(self.container())};
                {Dir::end(self.container())};
                {begin != end} -> meta::truthy;
                {(*func)(*begin)} -> meta::truthy;
                {++begin};
            })
        :
            func(std::addressof(self.filter())),
            index(0),
            begin(Dir::begin(self.container())),
            end(Dir::end(self.container()))
        {
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
            NoneType
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
            NoneType,
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
            NoneType
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
            NoneType,
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
    template <range_direction Dir, meta::lvalue Self>
        requires (meta::range<decltype(std::declval<Self>().filter()), bool>)
    struct binary_filter_iterator<Dir, Self> {
        using mask_begin_type = decltype(Dir::begin(std::declval<Self>().filter()));
        using mask_end_type = decltype(Dir::end(std::declval<Self>().filter()));
        using begin_type = decltype(Dir::begin(std::declval<Self>().container()));
        using end_type = decltype(Dir::end(std::declval<Self>().container()));
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
        [[nodiscard]] constexpr binary_filter_iterator(Self& self)
            noexcept (requires{
                {Dir::begin(self.filter())} noexcept;
                {Dir::end(self.filter())} noexcept;
                {Dir::begin(self.container())} noexcept;
                {Dir::end(self.container())} noexcept;
                {begin != end && mask_begin != mask_end} noexcept -> meta::nothrow::truthy;
                {*mask_begin} noexcept -> meta::nothrow::truthy;
                {++begin} noexcept;
                {++mask_begin} noexcept;
            })
            requires (requires{
                {Dir::begin(self.filter())};
                {Dir::end(self.filter())};
                {Dir::begin(self.container())};
                {Dir::end(self.container())};
                {begin != end && mask_begin != mask_end} -> meta::truthy;
                {*mask_begin} -> meta::truthy;
                {++begin};
                {++mask_begin};
            })
        :
            mask_begin(Dir::begin(self.filter())),
            mask_end(Dir::end(self.filter())),
            index(0),
            begin(Dir::begin(self.container())),
            end(Dir::end(self.container()))
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
            NoneType
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
            NoneType,
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
            NoneType
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

    /* Binary `where{}` expressions correspond to a filter that omits values where the
    predicate or mask evaluates to `false`.  Because this cannot be known generically
    ahead of time, the resulting range is unsized, not a tuple, and not indexable. */
    template <meta::not_rvalue Filter, meta::not_rvalue C> requires (meta::range<C>)
    struct binary_filter {
        using filter_type = Filter;
        using true_type = C;

        [[no_unique_address]] impl::ref<filter_type> m_filter;
        [[no_unique_address]] impl::ref<true_type> m_true;

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) filter(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_filter);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) container(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_true);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto begin(this Self&& self)
            noexcept (requires{
                {binary_filter_iterator<range_forward, Self>{std::forward<Self>(self)}} noexcept;
            })
            requires (requires{
                {binary_filter_iterator<range_forward, Self>{std::forward<Self>(self)}};
            })
        {
            return binary_filter_iterator<range_forward, Self>{std::forward<Self>(self)};
        }

        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }

        template <typename Self>
        [[nodiscard]] constexpr auto rbegin(this Self&& self)
            noexcept (requires{
                {binary_filter_iterator<range_reverse, Self>{std::forward<Self>(self)}} noexcept;
            })
            requires (requires{
                {binary_filter_iterator<range_reverse, Self>{std::forward<Self>(self)}};
            })
        {
            return binary_filter_iterator<range_reverse, Self>{std::forward<Self>(self)};
        }

        [[nodiscard]] static constexpr NoneType rend() noexcept { return {}; }
    };
    template <typename Filter, typename C>
    binary_filter(Filter&&, C&&) -> binary_filter<
        meta::remove_rvalue<Filter>,
        meta::remove_rvalue<C>
    >;

    /* In order to simplify the implementation of ternary `where` for scalar operands,
    they will be wrapped in a trivial iterator type that infinitely repeats a const
    reference to that value when accessed, so that the filtered iterators do not
    need to specialize for scalar cases. */
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
            return 0;
        }

        [[nodiscard]] constexpr bool operator==(
            const trivial_repeat_iterator& other
        ) const noexcept {
            return false;
        }

        [[nodiscard]] constexpr auto operator<=>(
            const trivial_repeat_iterator& other
        ) const noexcept {
            return std::strong_ordering::less;
        }
    };
    template <typename T>
    trivial_repeat_iterator(T&) -> trivial_repeat_iterator<meta::as_const_ref<T>>;

    /* The mask and predicate cases must be kept separate in order to allow for the
    possibility of common ranges in both cases, despite the possible need for a third
    mask iterator. */
    template <typename Mask, typename True, typename False>
    struct ternary_mask_iterator {
        using category = std::conditional_t<
            meta::inherits<
                meta::common_type<
                    meta::iterator_category<Mask>,
                    meta::iterator_category<True>,
                    meta::iterator_category<False>
                >,
                std::random_access_iterator_tag
            >,
            std::random_access_iterator_tag,
            std::conditional_t<
                meta::inherits<
                    meta::common_type<
                        meta::iterator_category<Mask>,
                        meta::iterator_category<True>,
                        meta::iterator_category<False>
                    >,
                    std::forward_iterator_tag
                >,
                meta::common_type<
                    meta::iterator_category<Mask>,
                    meta::iterator_category<True>,
                    meta::iterator_category<False>
                >,
                std::forward_iterator_tag
            >
        >;
        using difference_type = meta::common_type<
            meta::iterator_difference<Mask>,
            meta::iterator_difference<True>,
            meta::iterator_difference<False>
        >;
        using reference = meta::make_union<
            meta::dereference_type<meta::as_const_ref<True>>,
            meta::dereference_type<meta::as_const_ref<False>>
        >;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<value_type>;

        [[no_unique_address]] Mask mask_iter;
        [[no_unique_address]] True true_iter;
        [[no_unique_address]] False false_iter;

        [[nodiscard]] constexpr reference operator*() const
            noexcept (requires{
                {*mask_iter} noexcept -> meta::nothrow::truthy;
                {*true_iter} noexcept -> meta::nothrow::convertible_to<reference>;
                {*false_iter} noexcept -> meta::nothrow::convertible_to<reference>;
            })
            requires (requires{
                {*mask_iter} -> meta::truthy;
                {*true_iter} -> meta::convertible_to<reference>;
                {*false_iter} -> meta::convertible_to<reference>;
            })
        {
            if (*mask_iter) {
                return *true_iter;
            } else {
                return *false_iter;
            }
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow{**this}} noexcept;})
            requires (requires{{impl::arrow{**this}};})
        {
            return impl::arrow{**this};
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const
            noexcept (requires{
                {mask_iter[n]} noexcept -> meta::nothrow::truthy;
                {true_iter[n]} noexcept -> meta::nothrow::convertible_to<reference>;
                {false_iter[n]} noexcept -> meta::nothrow::convertible_to<reference>;
            })
            requires (requires{
                {mask_iter[n]} -> meta::truthy;
                {true_iter[n]} -> meta::convertible_to<reference>;
                {false_iter[n]} -> meta::convertible_to<reference>;
            })
        {
            if (mask_iter[n]) {
                return true_iter[n];
            } else {
                return false_iter[n];
            }
        }

        constexpr ternary_mask_iterator& operator++()
            noexcept (requires{
                {++mask_iter} noexcept;
                {++true_iter} noexcept;
                {++false_iter} noexcept;
            })
            requires (requires{
                {++mask_iter};
                {++true_iter};
                {++false_iter};
            })
        {
            ++mask_iter;
            ++true_iter;
            ++false_iter;
            return *this;
        }

        [[nodiscard]] constexpr ternary_mask_iterator operator++(int)
            noexcept (requires{{ternary_mask_iterator{
                mask_iter++,
                true_iter++,
                false_iter++
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                mask_iter++,
                true_iter++,
                false_iter++
            }};})
        {
            return ternary_mask_iterator{
                mask_iter++,
                true_iter++,
                false_iter++
            };
        }

        constexpr ternary_mask_iterator& operator+=(difference_type n)
            noexcept (requires{
                {mask_iter += n} noexcept;
                {true_iter += n} noexcept;
                {false_iter += n} noexcept;
            })
            requires (requires{
                {mask_iter += n};
                {true_iter += n};
                {false_iter += n};
            })
        {
            mask_iter += n;
            true_iter += n;
            false_iter += n;
            return *this;
        }

        [[nodiscard]] friend constexpr ternary_mask_iterator operator+(
            const ternary_mask_iterator& self,
            difference_type n
        )
            noexcept (requires{{ternary_mask_iterator{
                self.mask_iter + n,
                self.true_iter + n,
                self.false_iter + n
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                self.mask_iter + n,
                self.true_iter + n,
                self.false_iter + n
            }};})
        {
            return ternary_mask_iterator{
                self.mask_iter + n,
                self.true_iter + n,
                self.false_iter + n
            };
        }

        [[nodiscard]] friend constexpr ternary_mask_iterator operator+(
            difference_type n,
            const ternary_mask_iterator& self
        )
            noexcept (requires{{ternary_mask_iterator{
                self.mask_iter + n,
                self.true_iter + n,
                self.false_iter + n
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                self.mask_iter + n,
                self.true_iter + n,
                self.false_iter + n
            }};})
        {
            return ternary_mask_iterator{
                self.mask_iter + n,
                self.true_iter + n,
                self.false_iter + n
            };
        }

        constexpr ternary_mask_iterator& operator--()
            noexcept (requires{
                {--mask_iter} noexcept;
                {--true_iter} noexcept;
                {--false_iter} noexcept;
            })
            requires (requires{
                {--mask_iter};
                {--true_iter};
                {--false_iter};
            })
        {
            --mask_iter;
            --true_iter;
            --false_iter;
            return *this;
        }

        [[nodiscard]] constexpr ternary_mask_iterator operator--(int)
            noexcept (requires{{ternary_mask_iterator{
                mask_iter--,
                true_iter--,
                false_iter--
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                mask_iter--,
                true_iter--,
                false_iter--
            }};})
        {
            return ternary_mask_iterator{
                mask_iter--,
                true_iter--,
                false_iter--
            };
        }

        constexpr ternary_mask_iterator& operator-=(difference_type n)
            noexcept (requires{
                {mask_iter -= n} noexcept;
                {true_iter -= n} noexcept;
                {false_iter -= n} noexcept;
            })
            requires (requires{
                {mask_iter -= n};
                {true_iter -= n};
                {false_iter -= n};
            })
        {
            mask_iter -= n;
            true_iter -= n;
            false_iter -= n;
            return *this;
        }

        [[nodiscard]] constexpr ternary_mask_iterator operator-(difference_type n) const
            noexcept (requires{{ternary_mask_iterator{
                mask_iter - n,
                true_iter - n,
                false_iter - n
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                mask_iter - n,
                true_iter - n,
                false_iter - n
            }};})
        {
            return ternary_mask_iterator{
                mask_iter - n,
                true_iter - n,
                false_iter - n
            };
        }

        [[nodiscard]] constexpr difference_type operator-(const ternary_mask_iterator& other) const
            noexcept (requires{{iter::min{}(
                difference_type(mask_iter - other.mask_iter),
                difference_type(true_iter - other.true_iter),
                difference_type(false_iter - other.false_iter)
            )} noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (requires{{iter::min{}(
                difference_type(mask_iter - other.mask_iter),
                difference_type(true_iter - other.true_iter),
                difference_type(false_iter - other.false_iter)
            )} -> meta::convertible_to<difference_type>;})
        {
            return iter::min{}(
                difference_type(mask_iter - other.mask_iter),
                difference_type(true_iter - other.true_iter),
                difference_type(false_iter - other.false_iter)
            );
        }

        [[nodiscard]] constexpr bool operator<(const ternary_mask_iterator& other) const
            noexcept (requires{{
                mask_iter < other.mask_iter &&
                true_iter < other.true_iter &&
                false_iter < other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                mask_iter < other.mask_iter &&
                true_iter < other.true_iter &&
                false_iter < other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return
                mask_iter < other.mask_iter &&
                true_iter < other.true_iter &&
                false_iter < other.false_iter;
        }

        [[nodiscard]] constexpr bool operator<=(const ternary_mask_iterator& other) const
            noexcept (requires{{
                mask_iter <= other.mask_iter &&
                true_iter <= other.true_iter &&
                false_iter <= other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                mask_iter <= other.mask_iter &&
                true_iter <= other.true_iter &&
                false_iter <= other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return
                mask_iter <= other.mask_iter &&
                true_iter <= other.true_iter &&
                false_iter <= other.false_iter;
        }

        [[nodiscard]] constexpr bool operator==(const ternary_mask_iterator& other) const
            noexcept (requires{{
                mask_iter == other.mask_iter ||
                true_iter == other.true_iter ||
                false_iter == other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                mask_iter == other.mask_iter ||
                true_iter == other.true_iter ||
                false_iter == other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return
                mask_iter == other.mask_iter ||
                true_iter == other.true_iter ||
                false_iter == other.false_iter;
        }

        [[nodiscard]] constexpr bool operator!=(const ternary_mask_iterator& other) const
            noexcept (requires{{
                mask_iter != other.mask_iter &&
                true_iter != other.true_iter &&
                false_iter != other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                mask_iter != other.mask_iter &&
                true_iter != other.true_iter &&
                false_iter != other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return
                mask_iter != other.mask_iter &&
                true_iter != other.true_iter &&
                false_iter != other.false_iter;
        }

        [[nodiscard]] constexpr bool operator>=(const ternary_mask_iterator& other) const
            noexcept (requires{{
                mask_iter >= other.mask_iter &&
                true_iter >= other.true_iter &&
                false_iter >= other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                mask_iter >= other.mask_iter &&
                true_iter >= other.true_iter &&
                false_iter >= other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return
                mask_iter >= other.mask_iter &&
                true_iter >= other.true_iter &&
                false_iter >= other.false_iter;
        }

        [[nodiscard]] constexpr bool operator>(const ternary_mask_iterator& other) const
            noexcept (requires{{
                mask_iter > other.mask_iter &&
                true_iter > other.true_iter &&
                false_iter > other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                mask_iter > other.mask_iter &&
                true_iter > other.true_iter &&
                false_iter > other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return
                mask_iter > other.mask_iter &&
                true_iter > other.true_iter &&
                false_iter > other.false_iter;
        }
    };
    template <typename Mask, typename True, typename False>
    ternary_mask_iterator(Mask, True, False) -> ternary_mask_iterator<Mask, True, False>;

    /* Ternary `where{}` expressions correspond to a merge between the left and right
    operands, choosing the left operand when the predicate or mask evaluates to `true`
    and the right operand when it evaluates to `false`.  This never changes the size of
    the underlying ranges, except to truncate to the shortest operand where
    appropriate.  The resulting range can therefore be sized, tuple-like, and indexed
    provided the operands support these operations. */
    template <meta::not_rvalue Filter, meta::not_rvalue True, meta::not_rvalue False>
    struct ternary_filter {
        using filter_type = meta::as_range<Filter>;
        using true_type = True;
        using false_type = False;

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
        template <typename T>
        struct _front_type { using type = T; };
        template <meta::range T>
        struct _front_type<T> { using type = meta::front_type<T>; };
        template <typename Self>
        using front_type = meta::make_union<
            typename _front_type<decltype((std::declval<Self>().if_true()))>::type,
            typename _front_type<decltype((std::declval<Self>().if_false()))>::type
        >;

        template <typename T>
        struct _back_type { using type = T; };
        template <meta::range T>
        struct _back_type<T> { using type = meta::back_type<T>; };
        template <typename Self>
        using back_type = meta::make_union<
            typename _back_type<decltype((std::declval<Self>().if_true()))>::type,
            typename _back_type<decltype((std::declval<Self>().if_false()))>::type
        >;

        template <typename T>
        struct _subscript_type { using type = T; };
        template <meta::range T>
        struct _subscript_type<T> { using type = meta::subscript_type<T, size_t>; };
        template <typename Self>
        using subscript_type = meta::make_union<
            typename _subscript_type<decltype((std::declval<Self>().if_true()))>::type,
            typename _subscript_type<decltype((std::declval<Self>().if_false()))>::type
        >;

        template <ssize_t I>
        static constexpr size_t normalize = impl::normalize_index<iter::min{}(
            meta::tuple_size<true_type>,
            meta::tuple_size<false_type>
        ), I>();

        template <ssize_t I, typename T>
        struct _get_type { using type = T; };
        template <ssize_t I, meta::tuple_like T>
        struct _get_type<I, T> { using type = meta::get_type<T, normalize<I>>; };
        template <ssize_t I, typename Self>
        using get_type = meta::make_union<
            typename _get_type<I, decltype((std::declval<Self>().if_true()))>::type,
            typename _get_type<I, decltype((std::declval<Self>().if_false()))>::type
        >;

        template <typename T>
        static constexpr size_t _size(T&& value)
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
        static constexpr ssize_t _ssize(T&& value)
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
        static constexpr bool _empty(T&& value)
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
        static constexpr decltype(auto) _front(T&& value)
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
        static constexpr decltype(auto) _back(T&& value)
            noexcept (!meta::range<T> || requires{{std::forward<T>(value).back()} noexcept;})
            requires (!meta::range<T> || requires{{std::forward<T>(value).back()};})
        {
            if constexpr (meta::range<T>) {
                return (std::forward<T>(value).back());
            } else {
                return (std::forward<T>(value));
            }
        }

        template <typename T>
        static constexpr decltype(auto) _subscript(T&& value, size_t index)
            noexcept (!meta::range<T> || requires{{std::forward<T>(value)[index]} noexcept;})
            requires (!meta::range<T> || requires{{std::forward<T>(value)[index]};})
        {
            if constexpr (meta::range<T>) {
                return (std::forward<T>(value)[index]);
            } else {
                return (std::forward<T>(value));
            }
        }

        template <size_t I, typename T>
        static constexpr decltype(auto) _get(T&& value)
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

        template <typename T>
        static constexpr auto _begin(T&& value)
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
        static constexpr auto _end(T&& value)
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
        static constexpr auto _rbegin(T&& value)
            noexcept (!meta::range<T> || requires{{std::forward<T>(value).rbegin()} noexcept;})
            requires (!meta::range<T> || requires{{std::forward<T>(value).rbegin()};})
        {
            if constexpr (meta::range<T>) {
                return std::forward<T>(value).rbegin();
            } else {
                return impl::trivial_repeat_iterator(value);
            }
        }

        template <typename T>
        static constexpr auto _rend(T&& value)
            noexcept (!meta::range<T> || requires{{std::forward<T>(value).rend()} noexcept;})
            requires (!meta::range<T> || requires{{std::forward<T>(value).rend()};})
        {
            if constexpr (meta::range<T>) {
                return std::forward<T>(value).rend();
            } else {
                return impl::trivial_repeat_iterator(value);
            }
        }

    public:
        [[nodiscard]] constexpr size_t size() const
            noexcept (requires{{iter::min{}(
                size_t(filter().size()),
                _size(if_true()),
                _size(if_false())
            )} noexcept;})
            requires (requires{{iter::min{}(
                size_t(filter().size()),
                _size(if_true()),
                _size(if_false())
            )};})
        {
            return iter::min{}(
                size_t(filter().size()),
                _size(if_true()),
                _size(if_false())
            );
        }

        [[nodiscard]] constexpr ssize_t ssize() const
            noexcept (requires{{iter::min{}(
                ssize_t(filter().ssize()),
                _ssize(if_true()),
                _ssize(if_false())
            )} noexcept;})
            requires (requires{{iter::min{}(
                ssize_t(filter().ssize()),
                _ssize(if_true()),
                _ssize(if_false())
            )};})
        {
            return iter::min{}(
                ssize_t(filter().ssize()),
                _ssize(if_true()),
                _ssize(if_false())
            );
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{
                filter().empty() || _empty(if_true()) || _empty(if_false())
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                filter().empty() || _empty(if_true()) || _empty(if_false())
            } -> meta::convertible_to<bool>;})
        {
            return filter().empty() || _empty(if_true()) || _empty(if_false());
        }

        template <typename Self>
        [[nodiscard]] constexpr front_type<Self> front(this Self&& self)
            noexcept (requires{
                {self.filter().front()} noexcept -> meta::nothrow::truthy;
                {
                    _front(std::forward<Self>(self).if_true())
                } noexcept -> meta::nothrow::convertible_to<front_type<Self>>;
                {
                    _front(std::forward<Self>(self).if_false())
                } noexcept -> meta::nothrow::convertible_to<front_type<Self>>;
            })
            requires (
                meta::front_returns<
                    bool,
                    decltype((std::forward<Self>(self).filter()))
                > && (
                    (meta::range<true_type> && meta::front_returns<
                        front_type<Self>,
                        decltype((std::forward<Self>(self).if_true()))
                    >) || (!meta::range<true_type> && meta::convertible_to<
                        decltype((std::forward<Self>(self).if_true())),
                        front_type<Self>
                    >)
                ) && (
                    (meta::range<false_type> && meta::front_returns<
                        front_type<Self>,
                        decltype((std::forward<Self>(self).if_false()))
                    >) || (!meta::range<false_type> && meta::convertible_to<
                        decltype((std::forward<Self>(self).if_false())),
                        front_type<Self>
                    >)
                )
            )
        {
            if (self.filter().front()) {
                return _front(std::forward<Self>(self).if_true());
            } else {
                return _front(std::forward<Self>(self).if_false());
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr back_type<Self> back(this Self&& self)
            noexcept (requires{
                {self.filter().back()} noexcept -> meta::nothrow::truthy;
                {
                    _back(std::forward<Self>(self).if_true())
                } noexcept -> meta::nothrow::convertible_to<back_type<Self>>;
                {
                    _back(std::forward<Self>(self).if_false())
                } noexcept -> meta::nothrow::convertible_to<back_type<Self>>;
            })
            requires (
                meta::back_returns<
                    bool,
                    decltype((std::forward<Self>(self).filter()))
                > && (
                    (meta::range<true_type> && meta::back_returns<
                        back_type<Self>,
                        decltype((std::forward<Self>(self).if_true()))
                    >) || (!meta::range<true_type> && meta::convertible_to<
                        decltype((std::forward<Self>(self).if_true())),
                        back_type<Self>
                    >)
                ) && (
                    (meta::range<false_type> && meta::back_returns<
                        back_type<Self>,
                        decltype((std::forward<Self>(self).if_false()))
                    >) || (!meta::range<false_type> && meta::convertible_to<
                        decltype((std::forward<Self>(self).if_false())),
                        back_type<Self>
                    >)
                )
            )
        {
            if (self.filter().back()) {
                return _back(std::forward<Self>(self).if_true());
            } else {
                return _back(std::forward<Self>(self).if_false());
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr subscript_type<Self> operator[](this Self&& self, ssize_t n)
            noexcept (requires(size_t m) {
                {size_t(impl::normalize_index(self.ssize(), n))} noexcept;
                {self.filter()[m]} noexcept -> meta::nothrow::truthy;
                {
                    _subscript(std::forward<Self>(self).if_true(), m)
                } noexcept -> meta::nothrow::convertible_to<subscript_type<Self>>;
                {
                    _subscript(std::forward<Self>(self).if_false(), m)
                } noexcept -> meta::nothrow::convertible_to<subscript_type<Self>>;
            })
            requires (
                meta::subscript_returns<
                    bool,
                    decltype((std::forward<Self>(self).filter())),
                    size_t
                > && (
                    (meta::range<true_type> && meta::subscript_returns<
                        subscript_type<Self>,
                        decltype((std::forward<Self>(self).if_true())),
                        size_t
                    >) || (!meta::range<true_type> && meta::convertible_to<
                        decltype((std::forward<Self>(self).if_true())),
                        subscript_type<Self>
                    >)
                ) && (
                    (meta::range<false_type> && meta::subscript_returns<
                        subscript_type<Self>,
                        decltype((std::forward<Self>(self).if_false())),
                        size_t
                    >) || (!meta::range<false_type> && meta::convertible_to<
                        decltype((std::forward<Self>(self).if_false())),
                        subscript_type<Self>
                    >)
                )
            )
        {
            size_t m = size_t(impl::normalize_index(self.ssize(), n));
            if (self.filter()[m]) {
                return _subscript(std::forward<Self>(self).if_true(), m);
            } else {
                return _subscript(std::forward<Self>(self).if_false(), m);
            }
        }

        /// TODO: this tuple size and type logic needs to be carefully looked at and
        /// unified with the std:: namespace helpers and get_type, etc.

        template <ssize_t I, typename Self>
            requires (
                meta::tuple_like<filter_type> &&
                meta::tuple_like<meta::as_range_or_scalar<true_type>> &&
                meta::tuple_like<meta::as_range_or_scalar<false_type>> &&
                impl::valid_index<
                    iter::min{}(
                        meta::tuple_size<filter_type>,
                        meta::range<true_type> ?
                            meta::tuple_size<meta::as_range_or_scalar<true_type>> :
                            std::numeric_limits<size_t>::max(),
                        meta::range<false_type> ?
                            meta::tuple_size<meta::as_range_or_scalar<false_type>> :
                            std::numeric_limits<size_t>::max()
                    ),
                    I
                >
            )
        [[nodiscard]] constexpr get_type<I, Self> get(this Self&& self)
            noexcept (requires{
                {self.filter().template get<normalize<I>>()} noexcept -> meta::nothrow::truthy;
                {
                    _get<normalize<I>>(std::forward<Self>(self).if_true())
                } noexcept -> meta::nothrow::convertible_to<get_type<I, Self>>;
                {
                    _get<normalize<I>>(std::forward<Self>(self).if_false())
                } noexcept -> meta::nothrow::convertible_to<get_type<I, Self>>;
            })
            requires (meta::get_returns<
                bool,
                decltype((std::forward<Self>(self).filter())),
                normalize<I>
            > && (
                (meta::range<true_type> && meta::get_returns<
                    get_type<I, Self>,
                    decltype((std::forward<Self>(self).if_true())),
                    normalize<I>
                >) || (!meta::range<true_type> && meta::convertible_to<
                    decltype((std::forward<Self>(self).if_true())),
                    get_type<I, Self>
                >)
            ) && (
                (meta::range<false_type> && meta::get_returns<
                    get_type<I, Self>,
                    decltype((std::forward<Self>(self).if_false())),
                    normalize<I>
                >) ||
                (!meta::range<False> && meta::convertible_to<
                    decltype((std::forward<Self>(self).if_false())),
                    get_type<I, Self>
                >)
            ))
        {
            if (self.filter().template get<normalize<I>>()) {
                return _get<normalize<I>>(std::forward<Self>(self).if_true());
            } else {
                return _get<normalize<I>>(std::forward<Self>(self).if_false());
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr auto begin(this Self&& self)
            noexcept (requires{{ternary_mask_iterator{
                .mask_iter = std::forward<Self>(self).filter().begin(),
                .true_iter = _begin(std::forward<Self>(self).if_true()),
                .false_iter = _begin(std::forward<Self>(self).if_false()),
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                .mask_iter = std::forward<Self>(self).filter().begin(),
                .true_iter = _begin(std::forward<Self>(self).if_true()),
                .false_iter = _begin(std::forward<Self>(self).if_false()),
            }};})
        {
            return ternary_mask_iterator{
                .mask_iter = std::forward<Self>(self).filter().begin(),
                .true_iter = _begin(std::forward<Self>(self).if_true()),
                .false_iter = _begin(std::forward<Self>(self).if_false()),
            };
        }

        template <typename Self>
        [[nodiscard]] constexpr auto end(this Self&& self)
            noexcept (requires{{ternary_mask_iterator{
                .mask_iter = std::forward<Self>(self).filter().end(),
                .true_iter = _end(std::forward<Self>(self).if_true()),
                .false_iter = _end(std::forward<Self>(self).if_false()),
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                .mask_iter = std::forward<Self>(self).filter().end(),
                .true_iter = _end(std::forward<Self>(self).if_true()),
                .false_iter = _end(std::forward<Self>(self).if_false()),
            }};})
        {
            return ternary_mask_iterator{
                .mask_iter = std::forward<Self>(self).filter().end(),
                .true_iter = _end(std::forward<Self>(self).if_true()),
                .false_iter = _end(std::forward<Self>(self).if_false()),
            };
        }

        template <typename Self>
        [[nodiscard]] constexpr auto rbegin(this Self&& self)
            noexcept (requires{{ternary_mask_iterator{
                .mask_iter = std::forward<Self>(self).filter().rbegin(),
                .true_iter = _rbegin(std::forward<Self>(self).if_true()),
                .false_iter = _rbegin(std::forward<Self>(self).if_false()),
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                .mask_iter = std::forward<Self>(self).filter().rbegin(),
                .true_iter = _rbegin(std::forward<Self>(self).if_true()),
                .false_iter = _rbegin(std::forward<Self>(self).if_false()),
            }};})
        {
            return ternary_mask_iterator{
                .mask_iter = std::forward<Self>(self).filter().rbegin(),
                .true_iter = _rbegin(std::forward<Self>(self).if_true()),
                .false_iter = _rbegin(std::forward<Self>(self).if_false()),
            };
        }

        template <typename Self>
        [[nodiscard]] constexpr auto rend(this Self&& self)
            noexcept (requires{{ternary_mask_iterator{
                .mask_iter = std::forward<Self>(self).filter().rend(),
                .true_iter = _rend(std::forward<Self>(self).if_true()),
                .false_iter = _rend(std::forward<Self>(self).if_false()),
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                .mask_iter = std::forward<Self>(self).filter().rend(),
                .true_iter = _rend(std::forward<Self>(self).if_true()),
                .false_iter = _rend(std::forward<Self>(self).if_false()),
            }};})
        {
            return ternary_mask_iterator{
                .mask_iter = std::forward<Self>(self).filter().rend(),
                .true_iter = _rend(std::forward<Self>(self).if_true()),
                .false_iter = _rend(std::forward<Self>(self).if_false()),
            };
        }
    };




    /// TODO: Only ternary forms will permit scalar `true` and `false` values, but
    /// the `true` value must be a range if the condition is given as a predicate.
    /// So `where(x > 5)(10, 20)` is valid, as is `where(x > 5)([...], 20)` and
    /// `where(x > 5)(10, [...])` or `where(x > 5)([...], [...])`, as well as
    /// `where([](int x) { return x > 5; })([...], 20)` and
    /// `where([](int x) { return x > 5; })([...], [...])`, but not
    /// `where([](int x) { return x > 5; })(10, [...])` or
    /// `where([](int x) { return x > 5; })(10, 20)`.

    /// -> The mask case is already good to go.

    /// TODO: perhaps the last form could just compile to a regular ternary statement,
    /// and the second to last could be a degenerate form that only considers the
    /// first value?  That would ensure full coverage, which is good for the mental
    /// model, but require some extra thought about how best to implement it.

    /// TODO: maybe the predicate case just immediately casts the first value to a
    /// scalar or range, and then only needs to handle the replacement value
    /// based on its range-ness?







    /* Ternary filters never change the overall size of the underlying range, and never
    require the iterator to skip over false elements.  The iterator can therefore be
    either forward, bidirectional, or random access (but not contiguous) without any
    issues. */
    template <typename Filter, typename True, typename False>
    struct ternary_predicate_iterator {
        using category = std::conditional_t<
            meta::inherits<
                meta::common_type<meta::iterator_category<True>, meta::iterator_category<False>>,
                std::random_access_iterator_tag
            >,
            std::random_access_iterator_tag,
            std::conditional_t<
                meta::inherits<
                    meta::common_type<meta::iterator_category<True>, meta::iterator_category<False>>,
                    std::forward_iterator_tag
                >,
                meta::common_type<meta::iterator_category<True>, meta::iterator_category<False>>,
                std::forward_iterator_tag
            >
        >;
        using difference_type = meta::common_type<
            meta::iterator_difference<True>,
            meta::iterator_difference<False>
        >;
        using reference = meta::make_union<
            meta::dereference_type<meta::as_const_ref<True>>,
            meta::dereference_type<meta::as_const_ref<False>>
        >;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<value_type>;

        [[no_unique_address]] Filter* func = nullptr;
        [[no_unique_address]] True true_iter;
        [[no_unique_address]] False false_iter;

    private:
        template <typename T>
        constexpr reference deref(T&& value) const
            noexcept (requires{
                {(*func)(value)} noexcept -> meta::nothrow::truthy;
                {std::forward<T>(value)} noexcept -> meta::nothrow::convertible_to<reference>;
                {*false_iter} noexcept -> meta::nothrow::convertible_to<reference>;
            })
            requires (requires{
                {(*func)(value)} -> meta::truthy;
                {std::forward<T>(value)} -> meta::convertible_to<reference>;
                {*false_iter} -> meta::convertible_to<reference>;
            })
        {
            if ((*func)(value)) {
                return std::forward<T>(value);
            } else {
                return *false_iter;
            }
        }

        template <typename T>
        constexpr reference deref(T&& value, difference_type n) const
            noexcept (requires{
                {(*func)(value)} noexcept -> meta::nothrow::truthy;
                {std::forward<T>(value)} noexcept -> meta::nothrow::convertible_to<reference>;
                {false_iter[n]} noexcept -> meta::nothrow::convertible_to<reference>;
            })
            requires (requires{
                {(*func)(value)} -> meta::truthy;
                {std::forward<T>(value)} -> meta::convertible_to<reference>;
                {false_iter[n]} -> meta::convertible_to<reference>;
            })
        {
            if ((*func)(value)) {
                return std::forward<T>(value);
            } else {
                return false_iter[n];
            }
        }

    public:
        [[nodiscard]] constexpr reference operator*() const
            noexcept (requires{{deref(*true_iter)} noexcept;})
            requires (requires{{deref(*true_iter)};})
        {
            return deref(*true_iter);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow{**this}} noexcept;})
            requires (requires{{impl::arrow{**this}};})
        {
            return impl::arrow{**this};
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const
            noexcept (requires{{deref(true_iter[n], n)} noexcept;})
            requires (requires{{deref(true_iter[n], n)};})
        {
            return deref(true_iter[n], n);
        }

        constexpr ternary_predicate_iterator& operator++()
            noexcept (requires{
                {++true_iter} noexcept;
                {++false_iter} noexcept;
            })
            requires (requires{
                {++true_iter};
                {++false_iter};
            })
        {
            ++true_iter;
            ++false_iter;
            return *this;
        }

        [[nodiscard]] constexpr ternary_predicate_iterator operator++(int)
            noexcept (requires{{ternary_predicate_iterator{
                func,
                true_iter++,
                false_iter++
            }} noexcept;})
            requires (requires{{ternary_predicate_iterator{
                func,
                true_iter++,
                false_iter++
            }};})
        {
            return ternary_predicate_iterator{
                func,
                true_iter++,
                false_iter++
            };
        }

        constexpr ternary_predicate_iterator& operator+=(difference_type n)
            noexcept (requires{
                {true_iter += n} noexcept;
                {false_iter += n} noexcept;
            })
            requires (requires{
                {true_iter += n};
                {false_iter += n};
            })
        {
            true_iter += n;
            false_iter += n;
            return *this;
        }

        [[nodiscard]] friend constexpr ternary_predicate_iterator operator+(
            const ternary_predicate_iterator& self,
            difference_type n
        )
            noexcept (requires{{ternary_predicate_iterator{
                self.func,
                self.true_iter + n,
                self.false_iter + n
            }} noexcept;})
            requires (requires{{ternary_predicate_iterator{
                self.func,
                self.true_iter + n,
                self.false_iter + n
            }};})
        {
            return ternary_predicate_iterator{
                self.func,
                self.true_iter + n,
                self.false_iter + n
            };
        }

        [[nodiscard]] friend constexpr ternary_predicate_iterator operator+(
            difference_type n,
            const ternary_predicate_iterator& self
        )
            noexcept (requires{{ternary_predicate_iterator{
                self.func,
                self.true_iter + n,
                self.false_iter + n
            }} noexcept;})
            requires (requires{{ternary_predicate_iterator{
                self.func,
                self.true_iter + n,
                self.false_iter + n
            }};})
        {
            return ternary_predicate_iterator{
                self.func,
                self.true_iter + n,
                self.false_iter + n
            };
        }

        constexpr ternary_predicate_iterator& operator--()
            noexcept (requires{
                {--true_iter} noexcept;
                {--false_iter} noexcept;
            })
            requires (requires{
                {--true_iter};
                {--false_iter};
            })
        {
            --true_iter;
            --false_iter;
            return *this;
        }

        [[nodiscard]] constexpr ternary_predicate_iterator operator--(int)
            noexcept (requires{{ternary_predicate_iterator{
                func,
                true_iter--,
                false_iter--
            }} noexcept;})
            requires (requires{{ternary_predicate_iterator{
                func,
                true_iter--,
                false_iter--
            }};})
        {
            return ternary_predicate_iterator{
                func,
                true_iter--,
                false_iter--
            };
        }

        constexpr ternary_predicate_iterator& operator-=(difference_type n)
            noexcept (requires{
                {true_iter -= n} noexcept;
                {false_iter -= n} noexcept;
            })
            requires (requires{
                {true_iter -= n};
                {false_iter -= n};
            })
        {
            true_iter -= n;
            false_iter -= n;
            return *this;
        }

        [[nodiscard]] constexpr ternary_predicate_iterator operator-(difference_type n) const
            noexcept (requires{{ternary_predicate_iterator{
                func,
                true_iter - n,
                false_iter - n
            }} noexcept;})
            requires (requires{{ternary_predicate_iterator{
                func,
                true_iter - n,
                false_iter - n
            }};})
        {
            return ternary_predicate_iterator{
                func,
                true_iter - n,
                false_iter - n
            };
        }

        [[nodiscard]] constexpr difference_type operator-(const ternary_predicate_iterator& other) const
            noexcept (requires{{iter::min{}(
                difference_type(true_iter - other.true_iter),
                difference_type(false_iter - other.false_iter)
            )} noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (requires{{iter::min{}(
                difference_type(true_iter - other.true_iter),
                difference_type(false_iter - other.false_iter)
            )} -> meta::convertible_to<difference_type>;})
        {
            return iter::min{}(
                difference_type(true_iter - other.true_iter),
                difference_type(false_iter - other.false_iter)
            );
        }

        [[nodiscard]] constexpr bool operator<(const ternary_predicate_iterator& other) const
            noexcept (requires{{
                true_iter < other.true_iter && false_iter < other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                true_iter < other.true_iter && false_iter < other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return true_iter < other.true_iter && false_iter < other.false_iter;
        }

        [[nodiscard]] constexpr bool operator<=(const ternary_predicate_iterator& other) const
            noexcept (requires{{
                true_iter <= other.true_iter && false_iter <= other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                true_iter <= other.true_iter && false_iter <= other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return true_iter <= other.true_iter && false_iter <= other.false_iter;
        }

        [[nodiscard]] constexpr bool operator==(const ternary_predicate_iterator& other) const
            noexcept (requires{{
                true_iter == other.true_iter || false_iter == other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                true_iter == other.true_iter || false_iter == other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return true_iter == other.true_iter || false_iter == other.false_iter;
        }

        [[nodiscard]] constexpr bool operator!=(const ternary_predicate_iterator& other) const
            noexcept (requires{{
                true_iter != other.true_iter && false_iter != other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                true_iter != other.true_iter && false_iter != other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return true_iter != other.true_iter && false_iter != other.false_iter;
        }

        [[nodiscard]] constexpr bool operator>=(const ternary_predicate_iterator& other) const
            noexcept (requires{{
                true_iter >= other.true_iter && false_iter >= other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                true_iter >= other.true_iter && false_iter >= other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return true_iter >= other.true_iter && false_iter >= other.false_iter;
        }

        [[nodiscard]] constexpr bool operator>(const ternary_predicate_iterator& other) const
            noexcept (requires{{
                true_iter > other.true_iter && false_iter > other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                true_iter > other.true_iter && false_iter > other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return true_iter > other.true_iter && false_iter > other.false_iter;
        }
    };
    template <typename Filter, typename True, typename False>
    ternary_predicate_iterator(Filter*, True, False) -> ternary_predicate_iterator<
        Filter,
        True,
        False
    >;

    /// TODO: inherit docs from masked case above when ready

    template <meta::not_rvalue Filter, meta::not_rvalue True, meta::not_rvalue False>
        requires (meta::call_returns<
            bool,
            meta::as_const_ref<Filter>,
            meta::yield_type<meta::as_range<True>>
        >)
    struct ternary_filter<Filter, True, False> {
        using filter_type = Filter;
        using true_type = meta::as_range<True>;
        using false_type = meta::as_range<False>;

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

        [[nodiscard]] constexpr size_t size() const
            noexcept (requires{{iter::min{}(
                size_t(if_true().size()),
                size_t(if_false().size())
            )} noexcept;})
            requires (requires{{iter::min{}(
                size_t(if_true().size()),
                size_t(if_false().size())
            )};})
        {
            return iter::min{}(
                size_t(if_true().size()),
                size_t(if_false().size())
            );
        }

        [[nodiscard]] constexpr ssize_t ssize() const
            noexcept (requires{{iter::min{}(
                ssize_t(if_true().ssize()),
                ssize_t(if_false().ssize())
            )} noexcept;})
            requires (requires{{iter::min{}(
                ssize_t(if_true().ssize()),
                ssize_t(if_false().ssize())
            )};})
        {
            return iter::min{}(
                ssize_t(if_true().ssize()),
                ssize_t(if_false().ssize())
            );
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{
                if_true().empty() || if_false().empty()
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                if_true().empty() || if_false().empty()
            } -> meta::convertible_to<bool>;})
        {
            return if_true().empty() || if_false().empty();
        }

    private:
        template <typename Self>
        using front_type = meta::make_union<
            meta::front_type<decltype((std::declval<Self>().if_true()))>,
            meta::front_type<decltype((std::declval<Self>().if_false()))>
        >;

        template <typename Self>
        using back_type = meta::make_union<
            meta::back_type<decltype((std::declval<Self>().if_true()))>,
            meta::back_type<decltype((std::declval<Self>().if_false()))>
        >;

        template <typename Self>
        using subscript_type = meta::make_union<
            meta::subscript_type<decltype((std::declval<Self>().if_true())), size_t>,
            meta::subscript_type<decltype((std::declval<Self>().if_false())), size_t>
        >;

        template <ssize_t I>
        static constexpr size_t normalize = impl::normalize_index<iter::min{}(
            meta::tuple_size<true_type>,
            meta::tuple_size<false_type>
        ), I>();

        template <ssize_t I, typename Self>
        using get_type = meta::make_union<
            meta::get_type<decltype((std::declval<Self>().if_true())), normalize<I>>,
            meta::get_type<decltype((std::declval<Self>().if_false())), normalize<I>>
        >;

        template <typename Self, typename T>
        constexpr front_type<Self> _front(this Self&& self, T&& value)
            noexcept (requires{
                {self.filter()(value)} noexcept -> meta::nothrow::truthy;
                {
                    std::forward<T>(value)
                } noexcept -> meta::nothrow::convertible_to<front_type<Self>>;
                {
                    std::forward<Self>(self).if_false().front()
                } noexcept -> meta::nothrow::convertible_to<front_type<Self>>;
            })
        {
            if (self.filter()(value)) {
                return std::forward<T>(value);
            } else {
                return std::forward<Self>(self).if_false().front();
            }
        }

        template <typename Self, typename T>
        constexpr back_type<Self> _back(this Self&& self, T&& value)
            noexcept (requires{
                {self.filter()(value)} noexcept -> meta::nothrow::truthy;
                {
                    std::forward<T>(value)
                } noexcept -> meta::nothrow::convertible_to<back_type<Self>>;
                {
                    std::forward<Self>(self).if_false().back()
                } noexcept -> meta::nothrow::convertible_to<back_type<Self>>;
            })
        {
            if (self.filter()(value)) {
                return std::forward<T>(value);
            } else {
                return std::forward<Self>(self).if_false().back();
            }
        }

        template <typename Self, typename T>
        constexpr subscript_type<Self> _subscript(this Self&& self, T&& value, size_t index)
            noexcept (requires{
                {self.filter()(value)} noexcept -> meta::nothrow::truthy;
                {
                    std::forward<T>(value)
                } noexcept -> meta::nothrow::convertible_to<subscript_type<Self>>;
                {
                    std::forward<Self>(self).if_false()[index]
                } noexcept -> meta::nothrow::convertible_to<subscript_type<Self>>;
            })
        {
            if (self.filter()(value)) {
                return std::forward<T>(value);
            } else {
                return std::forward<Self>(self).if_false()[index];
            }
        }

        template <size_t I, typename Self, typename T>
        constexpr get_type<I, Self> _get(this Self&& self, T&& value)
            noexcept (requires{
                {self.filter()(value)} noexcept -> meta::nothrow::truthy;
                {
                    std::forward<T>(value)
                } noexcept -> meta::nothrow::convertible_to<get_type<I, Self>>;
                {
                    std::forward<Self>(self).if_false().template get<I>()
                } noexcept -> meta::nothrow::convertible_to<get_type<I, Self>>;
            })
        {
            if (self.filter()(value)) {
                return std::forward<T>(value);
            } else {
                return std::forward<Self>(self).if_false().template get<I>();
            }
        }

    public:
        template <typename Self>
        [[nodiscard]] constexpr front_type<Self> front(this Self&& self)
            noexcept (requires{{std::forward<Self>(self)._front(
                std::forward<Self>(self).if_true().front()
            )} noexcept;})
            requires (
                meta::front_returns<
                    front_type<Self>,
                    decltype((std::forward<Self>(self).if_true()))
                > &&
                meta::front_returns<
                    front_type<Self>,
                    decltype((std::forward<Self>(self).if_false()))
                > &&
                meta::call_returns<
                    bool,
                    decltype((std::forward<Self>(self).filter())),
                    meta::front_type<decltype((std::forward<Self>(self).if_true()))>
                >
            )
        {
            return std::forward<Self>(self)._front(
                std::forward<Self>(self).if_true().front()
            );
        }

        template <typename Self>
        [[nodiscard]] constexpr back_type<Self> back(this Self&& self)
            noexcept (requires{{std::forward<Self>(self)._back(
                std::forward<Self>(self).if_true().back()
            )} noexcept;})
            requires (
                meta::back_returns<
                    back_type<Self>,
                    decltype((std::forward<Self>(self).if_true()))
                > &&
                meta::back_returns<
                    back_type<Self>,
                    decltype((std::forward<Self>(self).if_false()))
                > &&
                meta::call_returns<
                    bool,
                    decltype((std::forward<Self>(self).filter())),
                    meta::back_type<decltype((std::forward<Self>(self).if_true()))>
                >
            )
        {
            return std::forward<Self>(self)._back(
                std::forward<Self>(self).if_true().back()
            );
        }

        template <typename Self>
        [[nodiscard]] constexpr subscript_type<Self> operator[](this Self&& self, ssize_t n)
            noexcept (requires(size_t m) {
                {size_t(impl::normalize_index(self.ssize(), n))} noexcept;
                {std::forward<Self>(self)._subscript(
                    std::forward<Self>(self).if_true()[m],
                    m
                )} noexcept;
            })
            requires (
                meta::subscript_returns<
                    subscript_type<Self>,
                    decltype((std::forward<Self>(self).if_true())),
                    size_t
                > &&
                meta::subscript_returns<
                    subscript_type<Self>,
                    decltype((std::forward<Self>(self).if_false())),
                    size_t
                > &&
                meta::call_returns<
                    bool,
                    decltype((std::forward<Self>(self).filter())),
                    meta::subscript_type<decltype((std::forward<Self>(self).if_true())), size_t>
                >
            )
        {
            size_t m = size_t(impl::normalize_index(self.ssize(), n));
            return std::forward<Self>(self)._subscript(
                std::forward<Self>(self).if_true()[m],
                m
            );
        }

        template <ssize_t I, typename Self>
            requires (
                meta::tuple_like<true_type> &&
                meta::tuple_like<false_type> &&
                impl::valid_index<
                    iter::min{}(meta::tuple_size<true_type>, meta::tuple_size<false_type>),
                    I
                >
            )
        [[nodiscard]] constexpr get_type<I, Self> get(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).template _get<normalize<I>>(
                std::forward<Self>(self).if_true().template get<normalize<I>>()
            )} noexcept;})
            requires (
                meta::get_returns<
                    get_type<I, Self>,
                    decltype((std::forward<Self>(self).if_true())),
                    normalize<I>
                > &&
                meta::get_returns<
                    get_type<I, Self>,
                    decltype((std::forward<Self>(self).if_false())),
                    normalize<I>
                > &&
                meta::call_returns<
                    bool,
                    decltype((std::forward<Self>(self).filter())),
                    meta::get_type<decltype((std::forward<Self>(self).if_true())), normalize<I>>
                >
            )
        {
            return std::forward<Self>(self).template _get<normalize<I>>(
                std::forward<Self>(self).if_true().template get<normalize<I>>()
            );
        }

        template <typename Self>
        [[nodiscard]] constexpr auto begin(this Self&& self)
            noexcept (requires{{ternary_predicate_iterator{
                .func = std::addressof(self.filter()),
                .true_iter = std::forward<Self>(self).if_true().begin(),
                .false_iter = std::forward<Self>(self).if_false().begin(),
            }} noexcept;})
            requires (requires{{ternary_predicate_iterator{
                .func = std::addressof(self.filter()),
                .true_iter = std::forward<Self>(self).if_true().begin(),
                .false_iter = std::forward<Self>(self).if_false().begin(),
            }};})
        {
            return ternary_predicate_iterator{
                .func = std::addressof(self.filter()),
                .true_iter = std::forward<Self>(self).if_true().begin(),
                .false_iter = std::forward<Self>(self).if_false().begin(),
            };
        }

        template <typename Self>
        [[nodiscard]] constexpr auto end(this Self&& self)
            noexcept (requires{{ternary_predicate_iterator{
                .func = std::addressof(self.filter()),
                .true_iter = std::forward<Self>(self).if_true().end(),
                .false_iter = std::forward<Self>(self).if_false().end(),
            }} noexcept;})
            requires (requires{{ternary_predicate_iterator{
                .func = std::addressof(self.filter()),
                .true_iter = std::forward<Self>(self).if_true().end(),
                .false_iter = std::forward<Self>(self).if_false().end(),
            }};})
        {
            return ternary_predicate_iterator{
                .func = std::addressof(self.filter()),
                .true_iter = std::forward<Self>(self).if_true().end(),
                .false_iter = std::forward<Self>(self).if_false().end(),
            };
        }

        template <typename Self>
        [[nodiscard]] constexpr auto rbegin(this Self&& self)
            noexcept (requires{{ternary_predicate_iterator{
                .func = std::addressof(self.filter()),
                .true_iter = std::forward<Self>(self).if_true().rbegin(),
                .false_iter = std::forward<Self>(self).if_false().rbegin(),
            }} noexcept;})
            requires (requires{{ternary_predicate_iterator{
                .func = std::addressof(self.filter()),
                .true_iter = std::forward<Self>(self).if_true().rbegin(),
                .false_iter = std::forward<Self>(self).if_false().rbegin(),
            }};})
        {
            return ternary_predicate_iterator{
                .func = std::addressof(self.filter()),
                .true_iter = std::forward<Self>(self).if_true().rbegin(),
                .false_iter = std::forward<Self>(self).if_false().rbegin(),
            };
        }

        template <typename Self>
        [[nodiscard]] constexpr auto rend(this Self&& self)
            noexcept (requires{{ternary_predicate_iterator{
                .func = std::addressof(self.filter()),
                .true_iter = std::forward<Self>(self).if_true().rend(),
                .false_iter = std::forward<Self>(self).if_false().rend(),
            }} noexcept;})
            requires (requires{{ternary_predicate_iterator{
                .func = std::addressof(self.filter()),
                .true_iter = std::forward<Self>(self).if_true().rend(),
                .false_iter = std::forward<Self>(self).if_false().rend(),
            }};})
        {
            return ternary_predicate_iterator{
                .func = std::addressof(self.filter()),
                .true_iter = std::forward<Self>(self).if_true().rend(),
                .false_iter = std::forward<Self>(self).if_false().rend(),
            };
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

    /* A function object that filters elements from a range based on a user-defined
    predicate function or boolean mask.

    If the `where` object is constructed with a predicate function and later called
    with one or more arguments, then the function must be callable with the elements of
    the first argument and return values that are convertible to `bool`.  If the
    predicate returns `true`, then the value from the first argument will be included
    in the output range; otherwise, if a second argument is provided, the corresponding
    element from that argument will be included instead, acting as a ternary operator.
    If no second argument is provided, then elements for which the predicate returns
    `false` will be excluded from the output range.

    As an alternative to a predicate function, the `where` object can also be
    constructed with any `range` that yields a type convertible to `bool`.  In this
    case, the value of the range at each index will be used to filter the input(s),
    instead of invoking a separate function.  All other behavior remains the same as
    the predicate case. */
    template <meta::not_rvalue Filter>
    struct where {
        [[no_unique_address]] Filter filter;

        template <typename Self, meta::range T>
        [[nodiscard]] constexpr auto operator()(this Self&& self, T&& r)
            noexcept (requires{{range{impl::binary_filter{
                std::forward<Self>(self).filter,
                std::forward<T>(r)
            }}} noexcept;})
            requires ((
                meta::yields<meta::as_range<meta::as_const_ref<Filter>>, bool> ||
                meta::call_returns<
                    bool,
                    meta::as_const_ref<Filter>,
                    meta::yield_type<T>
                >
            ) && requires{{range{impl::binary_filter{
                std::forward<Self>(self).filter,
                std::forward<T>(r)
            }}};})
        {
            return range{impl::binary_filter{
                std::forward<Self>(self).filter,
                std::forward<T>(r)
            }};
        }

        /// TODO: the predicate case should allow non-ranges for all 3 arguments,
        /// and would devolve to a simple ternary expression in that case

        template <typename Self, typename True, typename False>
        [[nodiscard]] constexpr auto operator()(this Self&& self, True&& t, False&& f)
            noexcept (requires{{range{impl::ternary_filter{
                std::forward<Self>(self).filter,
                std::forward<True>(t),
                std::forward<False>(f)
            }}} noexcept;})
            requires ((meta::yields<meta::as_range<meta::as_const_ref<Filter>>, bool> || (
                meta::range<True> &&
                meta::call_returns<
                    bool,
                    meta::as_const_ref<Filter>,
                    meta::yield_type<True>
                >
            )) && requires{{range{impl::ternary_filter{
                std::forward<Self>(self).filter,
                std::forward<True>(t),
                std::forward<False>(f)
            }}};})
        {
            return range{impl::ternary_filter{
                std::forward<Self>(self).filter,
                std::forward<True>(t),
                std::forward<False>(f)
            }};
        }
    };

    template <typename F>
    where(F&&) -> where<meta::remove_rvalue<F>>;

}


}


_LIBCPP_BEGIN_NAMESPACE_STD

    namespace ranges {

        template <typename F, typename C>
        constexpr bool enable_borrowed_range<bertrand::impl::binary_filter<F, C>> =
            (bertrand::meta::lvalue<F> || enable_borrowed_range<bertrand::meta::unqualify<F>>) &&
            (bertrand::meta::lvalue<C> || enable_borrowed_range<bertrand::meta::unqualify<C>>);

        template <typename F, typename True, typename False>
        constexpr bool enable_borrowed_range<bertrand::impl::ternary_filter<F, True, False>> =
            (bertrand::meta::lvalue<F> || enable_borrowed_range<bertrand::meta::unqualify<F>>) &&
            (bertrand::meta::lvalue<True> || enable_borrowed_range<bertrand::meta::unqualify<True>>) &&
            (bertrand::meta::lvalue<False> || enable_borrowed_range<bertrand::meta::unqualify<False>>);

    }

    template <typename F, bertrand::meta::tuple_like True, bertrand::meta::tuple_like False>
    struct tuple_size<bertrand::impl::ternary_filter<F, True, False>> : std::integral_constant<
        size_t,
        bertrand::iter::min{}(bertrand::meta::tuple_size<True>, bertrand::meta::tuple_size<False>)
    > {};

    template <
        size_t I,
        typename F,
        bertrand::meta::tuple_like True,
        bertrand::meta::tuple_like False
    > requires (I < tuple_size<bertrand::impl::ternary_filter<F, True, False>>::value)
    struct tuple_element<I, bertrand::impl::ternary_filter<F, True, False>> {
        using type = bertrand::meta::remove_rvalue<decltype((
            std::declval<bertrand::impl::ternary_filter<F, True, False>>().template get<I>()
        ))>;
    };

_LIBCPP_END_NAMESPACE_STD


namespace bertrand::iter {

    static constexpr auto w = where{std::array{true, true, false}}(
        range(std::array{1, 2, 3}),
        4
    );
    static_assert(w.size() == 3);
    static_assert([] {
        auto it = w.begin();
        if (*it != 1) return false;
        ++it; if (*it != 2) return false;
        ++it; if (*it != 4) return false;
        ++it; if (it != w.end()) return false;

        return true;
    }());


}


#endif  // BERTRAND_ITER_WHERE_H