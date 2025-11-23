#ifndef BERTRAND_ITER_WHERE_H
#define BERTRAND_ITER_WHERE_H

#include "bertrand/iter/range.h"


namespace bertrand {


namespace impl {

    /* Binary filters require the iterator to skip over false elements, thereby
    preventing it from being a random access iterator, since the location of these
    skips cannot be known ahead of time.  The iterator is thus restricted only to
    forward or bidirectional modes. */
    template <range_direction Dir, meta::lvalue Self>
    struct binary_where_iterator {
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

        [[nodiscard]] constexpr binary_where_iterator() = default;
        [[nodiscard]] constexpr binary_where_iterator(Self self)
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

        constexpr binary_where_iterator& operator++()
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

        [[nodiscard]] constexpr binary_where_iterator operator++(int)
            noexcept (requires{
                {binary_where_iterator{*this}} noexcept;
                {++*this} noexcept;
            })
            requires (requires{
                {binary_where_iterator{*this}};
                {++*this};
            })
        {
            binary_where_iterator temp = *this;
            ++*this;
            return temp;
        }

        constexpr binary_where_iterator& operator--()
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

        [[nodiscard]] constexpr binary_where_iterator operator--(int)
            noexcept (requires{
                {binary_where_iterator{*this}} noexcept;
                {--*this} noexcept;
            })
            requires (requires{
                {binary_where_iterator{*this}};
                {--*this};
            })
        {
            binary_where_iterator temp = *this;
            --*this;
            return temp;
        }

        [[nodiscard]] constexpr bool operator==(const binary_where_iterator& other) const noexcept {
            return index == other.index;
        }

        [[nodiscard]] constexpr auto operator<=>(const binary_where_iterator& other) const noexcept {
            return index <=> other.index;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const binary_where_iterator& self,
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
            const binary_where_iterator& self
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
            const binary_where_iterator& self,
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
            const binary_where_iterator& self
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
    struct binary_where_iterator<Dir, Self> {
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

        [[nodiscard]] constexpr binary_where_iterator() = default;
        [[nodiscard]] constexpr binary_where_iterator(Self& self)
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

        constexpr binary_where_iterator& operator++()
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

        [[nodiscard]] constexpr binary_where_iterator operator++(int)
            noexcept (requires{
                {binary_where_iterator{*this}} noexcept;
                {++*this} noexcept;
            })
            requires (requires{
                {binary_where_iterator{*this}};
                {++*this};
            })
        {
            binary_where_iterator temp = *this;
            ++*this;
            return temp;
        }

        constexpr binary_where_iterator& operator--()
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

        [[nodiscard]] constexpr binary_where_iterator operator--(int)
            noexcept (requires{
                {binary_where_iterator{*this}} noexcept;
                {--*this} noexcept;
            })
            requires (requires{
                {binary_where_iterator{*this}};
                {--*this};
            })
        {
            binary_where_iterator temp = *this;
            --*this;
            return temp;
        }

        [[nodiscard]] constexpr bool operator==(const binary_where_iterator& other) const noexcept {
            return index == other.index;
        }

        [[nodiscard]] constexpr auto operator<=>(const binary_where_iterator& other) const noexcept {
            return index <=> other.index;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const binary_where_iterator& self,
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
            const binary_where_iterator& self
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
            const binary_where_iterator& self,
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
    predicate evaluates to `false`.  Because this cannot be known generically ahead of
    time, the resulting range is unsized, not a tuple, and not reliably indexable. */
    template <meta::not_rvalue F, meta::not_rvalue C>
    struct binary_where {
        using filter_type = F;
        using true_type = meta::as_range<C>;

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

        [[nodiscard]] constexpr auto begin()
            noexcept (requires{
                {binary_where_iterator<range_forward, binary_where&>{*this}} noexcept;
            })
            requires (requires{
                {binary_where_iterator<range_forward, binary_where&>{*this}};
            })
        {
            return binary_where_iterator<range_forward, binary_where&>{*this};
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{
                {binary_where_iterator<range_forward, const binary_where&>{*this}} noexcept;
            })
            requires (requires{
                {binary_where_iterator<range_forward, const binary_where&>{*this}};
            })
        {
            return binary_where_iterator<range_forward, const binary_where&>{*this,};
        }

        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }
        
        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{
                {binary_where_iterator<range_reverse, binary_where&>{*this}} noexcept;
            })
            requires (requires{
                {binary_where_iterator<range_reverse, binary_where&>{*this}};
            })
        {
            return binary_where_iterator<range_reverse, binary_where&>{*this};
        }

        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{
                {binary_where_iterator<range_reverse, const binary_where&>{*this}} noexcept;
            })
            requires (requires{
                {binary_where_iterator<range_reverse, const binary_where&>{*this}};
            })
        {
            return binary_where_iterator<range_reverse, const binary_where&>{*this};
        }

        [[nodiscard]] static constexpr NoneType rend() noexcept { return {}; }
    };
    template <typename F, typename C>
    binary_where(F&&, C&&) -> binary_where<meta::remove_rvalue<F>, meta::remove_rvalue<C>>;


    /// TODO: ternary where needs the same changes as binary where in order to support
    /// boolean masks.



    /* Ternary filters never change the overall size of the underlying range, and never
    require the iterator to skip over false elements.  The iterator can therefore be
    either forward, bidirectional, or random access without any issues. */
    template <meta::not_reference F, typename True, typename False>
    struct ternary_where_iterator {
        using category = std::conditional_t<
            meta::inherits<meta::iterator_category<True>, std::random_access_iterator_tag>,
            std::random_access_iterator_tag,
            std::conditional_t<
                meta::inherits<meta::iterator_category<True>, std::forward_iterator_tag>,
                meta::iterator_category<True>,
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

        [[no_unique_address]] F* func = nullptr;
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

        constexpr ternary_where_iterator& operator++()
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

        [[nodiscard]] constexpr ternary_where_iterator operator++(int)
            noexcept (requires{{ternary_where_iterator{
                func,
                true_iter++,
                false_iter++
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                func,
                true_iter++,
                false_iter++
            }};})
        {
            return ternary_where_iterator{
                func,
                true_iter++,
                false_iter++
            };
        }

        constexpr ternary_where_iterator& operator+=(difference_type n)
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

        [[nodiscard]] friend constexpr ternary_where_iterator operator+(
            const ternary_where_iterator& self,
            difference_type n
        )
            noexcept (requires{{ternary_where_iterator{
                self.func,
                self.true_iter + n,
                self.false_iter + n
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                self.func,
                self.true_iter + n,
                self.false_iter + n
            }};})
        {
            return ternary_where_iterator{
                self.func,
                self.true_iter + n,
                self.false_iter + n
            };
        }

        [[nodiscard]] friend constexpr ternary_where_iterator operator+(
            difference_type n,
            const ternary_where_iterator& self
        )
            noexcept (requires{{ternary_where_iterator{
                self.func,
                self.true_iter + n,
                self.false_iter + n
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                self.func,
                self.true_iter + n,
                self.false_iter + n
            }};})
        {
            return ternary_where_iterator{
                self.func,
                self.true_iter + n,
                self.false_iter + n
            };
        }

        constexpr ternary_where_iterator& operator--()
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

        [[nodiscard]] constexpr ternary_where_iterator operator--(int)
            noexcept (requires{{ternary_where_iterator{
                func,
                true_iter--,
                false_iter--
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                func,
                true_iter--,
                false_iter--
            }};})
        {
            return ternary_where_iterator{
                func,
                true_iter--,
                false_iter--
            };
        }

        constexpr ternary_where_iterator& operator-=(difference_type n)
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

        [[nodiscard]] constexpr ternary_where_iterator operator-(difference_type n) const
            noexcept (requires{{ternary_where_iterator{
                func,
                true_iter - n,
                false_iter - n
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                func,
                true_iter - n,
                false_iter - n
            }};})
        {
            return ternary_where_iterator{
                func,
                true_iter - n,
                false_iter - n
            };
        }

        [[nodiscard]] constexpr difference_type operator-(const ternary_where_iterator& other) const
            noexcept (requires{{std::min(
                difference_type(true_iter - other.true_iter),
                difference_type(false_iter - other.false_iter)
            )} noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (requires{{std::min(
                difference_type(true_iter - other.true_iter),
                difference_type(false_iter - other.false_iter)
            )} -> meta::convertible_to<difference_type>;})
        {
            return std::min(
                difference_type(true_iter - other.true_iter),
                difference_type(false_iter - other.false_iter)
            );
        }

        [[nodiscard]] constexpr bool operator<(const ternary_where_iterator& other) const
            noexcept (requires{{
                true_iter < other.true_iter && false_iter < other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                true_iter < other.true_iter && false_iter < other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return true_iter < other.true_iter && false_iter < other.false_iter;
        }

        [[nodiscard]] constexpr bool operator<=(const ternary_where_iterator& other) const
            noexcept (requires{{
                true_iter <= other.true_iter && false_iter <= other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                true_iter <= other.true_iter && false_iter <= other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return true_iter <= other.true_iter && false_iter <= other.false_iter;
        }

        [[nodiscard]] constexpr bool operator==(const ternary_where_iterator& other) const
            noexcept (requires{{
                true_iter == other.true_iter || false_iter == other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                true_iter == other.true_iter || false_iter == other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return true_iter == other.true_iter || false_iter == other.false_iter;
        }

        [[nodiscard]] constexpr bool operator!=(const ternary_where_iterator& other) const
            noexcept (requires{{
                true_iter != other.true_iter && false_iter != other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                true_iter != other.true_iter && false_iter != other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return true_iter != other.true_iter && false_iter != other.false_iter;
        }

        [[nodiscard]] constexpr bool operator>=(const ternary_where_iterator& other) const
            noexcept (requires{{
                true_iter >= other.true_iter && false_iter >= other.false_iter
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                true_iter >= other.true_iter && false_iter >= other.false_iter
            } -> meta::convertible_to<bool>;})
        {
            return true_iter >= other.true_iter && false_iter >= other.false_iter;
        }

        [[nodiscard]] constexpr bool operator>(const ternary_where_iterator& other) const
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
    template <typename F, typename True, typename False>
    ternary_where_iterator(F*, True, False) -> ternary_where_iterator<F, True, False>;

    template <typename Self>
    struct where_subscript { using type = void; };
    template <typename Self>
        requires (
            meta::has_subscript<decltype((std::declval<Self>().if_true())), size_t> &&
            meta::has_subscript<decltype((std::declval<Self>().if_false())), size_t>
        )
    struct where_subscript<Self> {
        using type = meta::make_union<
            meta::subscript_type<decltype((std::declval<Self>().if_true())), size_t>,
            meta::subscript_type<decltype((std::declval<Self>().if_false())), size_t>
        >;
    };

    template <typename Self>
    struct where_front { using type = void; };
    template <typename Self>
        requires (
            meta::has_front<decltype((std::declval<Self>().if_true()))> &&
            meta::has_front<decltype((std::declval<Self>().if_false()))>
        )
    struct where_front<Self> {
        using type = meta::make_union<
            meta::front_type<decltype((std::declval<Self>().if_true()))>,
            meta::front_type<decltype((std::declval<Self>().if_false()))>
        >;
    };

    template <typename Self>
    struct where_back { using type = void; };
    template <typename Self>
        requires (
            meta::has_back<decltype((std::declval<Self>().if_true()))> &&
            meta::has_back<decltype((std::declval<Self>().if_false()))>
        )
    struct where_back<Self> {
        using type = meta::make_union<
            meta::back_type<decltype((std::declval<Self>().if_true()))>,
            meta::back_type<decltype((std::declval<Self>().if_false()))>
        >;
    };

    template <typename Self>
    struct where_traits {
        using subscript = where_subscript<Self>::type;
        static constexpr bool has_subscript = meta::not_void<subscript>;
        using front = where_front<Self>::type;
        static constexpr bool has_front = meta::not_void<front>;
        using back = where_back<Self>::type;
        static constexpr bool has_back = meta::not_void<back>;
        template <size_t I>
        using get = meta::make_union<
            meta::get_type<decltype((std::declval<Self>().if_true())), I>,
            meta::get_type<decltype((std::declval<Self>().if_false())), I>
        >;
    };

    /* Ternary `where{}` expressions correspond to a merge between the left and right
    operands, choosing the left operand when the predicate evaluates to `true` and the
    right operand when it evaluates to `false`.  This never changes the size of the
    underlying ranges, except to truncate to the shortest operand where appropriate.
    The resulting range can therefore be sized, tuple-like, and indexed provided the
    operands support these operations. */
    template <meta::not_rvalue F, meta::not_rvalue True, meta::not_rvalue False>
    struct ternary_where {
        using function_type = F;
        using true_type = meta::as_range<True>;
        using false_type = meta::as_range<False>;

        [[no_unique_address]] impl::ref<function_type> m_func;
        [[no_unique_address]] impl::ref<true_type> m_true;
        [[no_unique_address]] impl::ref<false_type> m_false;

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) func(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_func);
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
            noexcept (requires{{std::min(
                size_t(if_true().size()),
                size_t(if_false().size())
            )} noexcept;})
            requires (requires{{std::min(
                size_t(if_true().size()),
                size_t(if_false().size())
            )};})
        {
            return std::min(
                size_t(if_true().size()),
                size_t(if_false().size())
            );
        }

        [[nodiscard]] constexpr ssize_t ssize() const
            noexcept (requires{{std::min(
                ssize_t(if_true().ssize()),
                ssize_t(if_false().ssize())
            )} noexcept;})
            requires (requires{{std::min(
                ssize_t(if_true().ssize()),
                ssize_t(if_false().ssize())
            )};})
        {
            return std::min(
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
        template <typename Self, typename T>
        constexpr where_traits<Self>::front _front(this Self&& self, T&& value)
            noexcept (requires{
                {self.func()(value)} noexcept -> meta::nothrow::truthy;
                {
                    std::forward<T>(value)
                } noexcept -> meta::nothrow::convertible_to<typename where_traits<Self>::front>;
                {
                    std::forward<Self>(self).if_false().front()
                } noexcept -> meta::nothrow::convertible_to<typename where_traits<Self>::front>;
            })
            requires (requires{
                {self.func()(value)} -> meta::truthy;
                {
                    std::forward<T>(value)
                } -> meta::convertible_to<typename where_traits<Self>::front>;
                {
                    std::forward<Self>(self).if_false().front()
                } -> meta::convertible_to<typename where_traits<Self>::front>;
            })
        {
            if (self.func()(value)) {
                return std::forward<T>(value);
            } else {
                return std::forward<Self>(self).if_false().front();
            }
        }

        template <typename Self, typename T>
        constexpr where_traits<Self>::back _back(this Self&& self, T&& value)
            noexcept (requires{
                {self.func()(value)} noexcept -> meta::nothrow::truthy;
                {
                    std::forward<T>(value)
                } noexcept -> meta::nothrow::convertible_to<typename where_traits<Self>::back>;
                {
                    std::forward<Self>(self).if_false().back()
                } noexcept -> meta::nothrow::convertible_to<typename where_traits<Self>::back>;
            })
            requires (requires{
                {self.func()(value)} -> meta::truthy;
                {
                    std::forward<T>(value)
                } -> meta::convertible_to<typename where_traits<Self>::back>;
                {
                    std::forward<Self>(self).if_false().back()
                } -> meta::convertible_to<typename where_traits<Self>::back>;
            })
        {
            if (self.func()(value)) {
                return std::forward<T>(value);
            } else {
                return std::forward<Self>(self).if_false().back();
            }
        }

        template <typename Self, typename T>
        constexpr where_traits<Self>::subscript _subscript(
            this Self&& self,
            T&& value,
            size_t index
        )
            noexcept (requires{
                {self.func()(value)} noexcept -> meta::nothrow::truthy;
                {
                    std::forward<T>(value)
                } noexcept -> meta::nothrow::convertible_to<typename where_traits<Self>::subscript>;
                {
                    std::forward<Self>(self).if_false()[index]
                } noexcept -> meta::nothrow::convertible_to<typename where_traits<Self>::subscript>;
            })
            requires (requires{
                {self.func()(value)} -> meta::truthy;
                {
                    std::forward<T>(value)
                } -> meta::convertible_to<typename where_traits<Self>::subscript>;
                {
                    std::forward<Self>(self).if_false()[index]
                } -> meta::convertible_to<typename where_traits<Self>::subscript>;
            })
        {
            if (self.func()(value)) {
                return std::forward<T>(value);
            } else {
                return std::forward<Self>(self).if_false()[index];
            }
        }

        template <ssize_t I>
        static constexpr size_t normalize = impl::normalize_index<std::min(
            meta::tuple_size<true_type>,
            meta::tuple_size<false_type>
        ), I>();

        template <size_t I, typename Self, typename T>
        constexpr where_traits<Self>::template get<I> _get(this Self&& self, T&& value)
            noexcept (requires{
                {self.func()(value)} noexcept -> meta::nothrow::truthy;
                {
                    std::forward<T>(value)
                } noexcept -> meta::nothrow::convertible_to<
                    typename where_traits<Self>::template get<I>
                >;
                {
                    std::forward<Self>(self).if_false().template get<I>()
                } noexcept -> meta::nothrow::convertible_to<
                    typename where_traits<Self>::template get<I>
                >;
            })
            requires (requires{
                {self.func()(value)} -> meta::truthy;
                {
                    std::forward<T>(value)
                } -> meta::convertible_to<typename where_traits<Self>::template get<I>>;
                {
                    std::forward<Self>(self).if_false().template get<I>()
                } -> meta::convertible_to<typename where_traits<Self>::template get<I>>;
            })
        {
            if (self.func()(value)) {
                return std::forward<T>(value);
            } else {
                return std::forward<Self>(self).if_false().template get<I>();
            }
        }

    public:
        template <typename Self>
        [[nodiscard]] constexpr where_traits<Self>::front front(this Self&& self)
            noexcept (requires{{std::forward<Self>(self)._front(
                std::forward<Self>(self).if_true().front()
            )} noexcept;})
            requires (requires{{std::forward<Self>(self)._front(
                std::forward<Self>(self).if_true().front()
            )};})
        {
            return std::forward<Self>(self)._front(
                std::forward<Self>(self).if_true().front()
            );
        }

        template <typename Self>
        [[nodiscard]] constexpr where_traits<Self>::back back(this Self&& self)
            noexcept (requires{{std::forward<Self>(self)._back(
                std::forward<Self>(self).if_true().back()
            )} noexcept;})
            requires (requires{{std::forward<Self>(self)._back(
                std::forward<Self>(self).if_true().back()
            )};})
        {
            return std::forward<Self>(self)._back(
                std::forward<Self>(self).if_true().back()
            );
        }

        template <typename Self>
        [[nodiscard]] constexpr where_traits<Self>::subscript operator[](
            this Self&& self,
            ssize_t n
        )
            noexcept (requires(size_t m) {
                {size_t(impl::normalize_index(self.ssize(), n))} noexcept;
                {std::forward<Self>(self)._subscript(
                    std::forward<Self>(self).if_true()[m],
                    m
                )} noexcept;
            })
            requires (requires(size_t m) {
                {size_t(impl::normalize_index(self.ssize(), n))};
                {std::forward<Self>(self)._subscript(
                    std::forward<Self>(self).if_true()[m],
                    m
                )};
            })
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
                    std::min(meta::tuple_size<true_type>, meta::tuple_size<false_type>),
                    I
                >
            )
        [[nodiscard]] constexpr auto get(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).template _get<normalize<I>>(
                std::forward<Self>(self).if_true().template get<normalize<I>>()
            )} noexcept;})
            -> where_traits<Self>::template get<impl::normalize_index<
                std::min(meta::tuple_size<true_type>, meta::tuple_size<false_type>),
                I
            >()>
            requires (requires{{std::forward<Self>(self).template _get<normalize<I>>(
                std::forward<Self>(self).if_true().template get<normalize<I>>()
            )};})
        {
            return std::forward<Self>(self).template _get<normalize<I>>(
                std::forward<Self>(self).if_true().template get<normalize<I>>()
            );
        }

        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            }};})
        {
            return ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            };
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            }};})
        {
            return ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            };
        }

        [[nodiscard]] constexpr auto end()
            noexcept (requires{{ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            }};})
        {
            return ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            };
        }

        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            }};})
        {
            return ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            };
        }

        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            }};})
        {
            return ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            };
        }

        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            }};})
        {
            return ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            };
        }

        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            }};})
        {
            return ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            };
        }

        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            }};})
        {
            return ternary_where_iterator{
                .func = std::addressof(*m_func),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            };
        }
    };
    template <typename F, typename True, typename False>
    ternary_where(F&&, True&&, False&&) -> ternary_where<
        meta::remove_rvalue<F>,
        meta::remove_rvalue<True>,
        meta::remove_rvalue<False>
    >;

}


namespace iter {

    /// TODO: document the boolean mask support as an alternative to callable
    /// predicates.


    /* A function object that filters elements from a range based on a user-defined
    predicate.

    The predicate function (provided as a constructor argument) can be any callable
    that takes a single element from the input range and returns a value convertible to
    `bool`, where `true` indicates that the element should be included in the output
    range.  If a second range is provided as a call argument, then replacements will be
    drawn from that range wherever the predicate returns `false`, otherwise they will
    be excluded from the output.  In the ternary form, the result will have the same
    size as the shorter of the two input ranges, and in the binary form, the result
    will be unsized.
    
    Other libraries and languages may refer to this as a `filter` operation. */
    template <meta::not_rvalue F>
    struct where {
        [[no_unique_address]] F filter;

        template <typename Self, typename T>
        [[nodiscard]] constexpr auto operator()(this Self&& self, T&& r)
            noexcept (requires{{range{impl::binary_where{
                std::forward<Self>(self).filter,
                std::forward<T>(r)
            }}} noexcept;})
            requires ((meta::range<meta::as_const_ref<F>, bool> || meta::call_returns<
                bool,
                meta::as_const_ref<F>,
                meta::yield_type<meta::as_range<T>>
            >) &&
            requires{{range{impl::binary_where{
                std::forward<Self>(self).filter,
                std::forward<T>(r)
            }}};})
        {
            return range{impl::binary_where{
                std::forward<Self>(self).filter,
                std::forward<T>(r)
            }};
        }

        template <typename Self, typename True, typename False>
        [[nodiscard]] constexpr auto operator()(this Self&& self, True&& t, False&& f)
            noexcept (requires{{range{impl::ternary_where{
                std::forward<Self>(self).filter,
                std::forward<True>(t),
                std::forward<False>(f)
            }}} noexcept;})
            requires ((meta::range<meta::as_const_ref<F>, bool> || meta::call_returns<
                bool,
                meta::as_const_ref<F>,
                meta::yield_type<meta::as_range<True>>
            >) && requires{{range{impl::ternary_where{
                std::forward<Self>(self).filter,
                std::forward<True>(t),
                std::forward<False>(f)
            }}};})
        {
            return range{impl::ternary_where{
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

        /// TODO: enable proper borrowed range support for where{} ranges with
        /// boolean masks, which do not strictly require lvalue predicates.

        template <typename F, typename C>
        constexpr bool enable_borrowed_range<bertrand::impl::binary_where<F&, C>> =
            bertrand::meta::lvalue<C> || enable_borrowed_range<bertrand::meta::unqualify<C>>;

        template <typename F, typename True, typename False>
        constexpr bool enable_borrowed_range<bertrand::impl::ternary_where<F&, True, False>> = (
            bertrand::meta::lvalue<True> ||
            enable_borrowed_range<bertrand::meta::unqualify<True>>
        ) && (
            bertrand::meta::lvalue<False> ||
            enable_borrowed_range<bertrand::meta::unqualify<False>>
        );

    }

    template <typename F, bertrand::meta::tuple_like True, bertrand::meta::tuple_like False>
    struct tuple_size<bertrand::impl::ternary_where<F, True, False>> : std::integral_constant<
        size_t,
        std::min(bertrand::meta::tuple_size<True>, bertrand::meta::tuple_size<False>)
    > {};

    template <
        size_t I,
        typename F,
        bertrand::meta::tuple_like True,
        bertrand::meta::tuple_like False
    > requires (I < tuple_size<bertrand::impl::ternary_where<F, True, False>>::value)
    struct tuple_element<I, bertrand::impl::ternary_where<F, True, False>> {
        using type = bertrand::meta::remove_rvalue<typename bertrand::impl::where_traits<
            bertrand::impl::ternary_where<F, True, False>
        >::template get<I>>;
    };

_LIBCPP_END_NAMESPACE_STD


namespace bertrand::iter {

    static constexpr auto w = where{[](int x) { return x > 1; }}(
        std::array{1, 2, 3}
    );
    static_assert([] {
        auto it = w->begin();
        if (*it++ != 2) return false;
        if (*it++ != 3) return false;
        if (it != w.end()) return false;

        auto it2 = w.rbegin();
        if (*it2++ != 3) return false;
        if (*it2++ != 2) return false;
        if (it2 != w.rend()) return false;

        return true;
    }());

    static constexpr auto w2 = where{range{std::array{false, true}}}(
        std::array{1, 2, 3}
    );
    static_assert([] {
        auto it = w2->begin();
        if (*it++ != 2) return false;
        // if (*it++ != 3) return false;
        if (it != w2.end()) return false;

        auto it2 = w2->rbegin();
        if (*it2++ != 3) return false;
        // if (*it2++ != 2) return false;
        if (it2 != w2.rend()) return false;

        return true;
    }());


}


#endif  // BERTRAND_ITER_WHERE_H