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
    predicate or mask evaluates to `false`.  Because this cannot be known generically
    ahead of time, the resulting range is unsized, not a tuple, and not indexable. */
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

    /* Ternary filters never change the overall size of the underlying range, and never
    require the iterator to skip over false elements.  The iterator can therefore be
    either forward, bidirectional, or random access (but not contiguous) without any
    issues. */
    template <typename Filter, typename True, typename False>
    struct ternary_where_iterator {
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
    template <typename Filter, typename True, typename False>
    ternary_where_iterator(Filter*, True, False) -> ternary_where_iterator<Filter, True, False>;

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
            noexcept (requires{{std::min({
                difference_type(mask_iter - other.mask_iter),
                difference_type(true_iter - other.true_iter),
                difference_type(false_iter - other.false_iter)
            })} noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (requires{{std::min({
                difference_type(mask_iter - other.mask_iter),
                difference_type(true_iter - other.true_iter),
                difference_type(false_iter - other.false_iter)
            })} -> meta::convertible_to<difference_type>;})
        {
            return std::min({
                difference_type(mask_iter - other.mask_iter),
                difference_type(true_iter - other.true_iter),
                difference_type(false_iter - other.false_iter)
            });
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
    struct ternary_where {
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
        static constexpr size_t normalize = impl::normalize_index<std::min(
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
                    std::min(meta::tuple_size<true_type>, meta::tuple_size<false_type>),
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

        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            }};})
        {
            return ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            };
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            }};})
        {
            return ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            };
        }

        [[nodiscard]] constexpr auto end()
            noexcept (requires{{ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            }};})
        {
            return ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            };
        }

        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            }};})
        {
            return ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            };
        }

        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            }};})
        {
            return ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            };
        }

        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            }};})
        {
            return ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            };
        }

        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            }};})
        {
            return ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            };
        }

        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            }};})
        {
            return ternary_where_iterator{
                .func = std::addressof(filter()),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            };
        }
    };
    template <meta::not_rvalue Filter, meta::not_rvalue True, meta::not_rvalue False>
        requires (meta::range<meta::as_const_ref<Filter>, bool>)
    struct ternary_where<Filter, True, False> {
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
            noexcept (requires{{std::min({
                size_t(filter().size()),
                size_t(if_true().size()),
                size_t(if_false().size())
            })} noexcept;})
            requires (requires{{std::min({
                size_t(filter().size()),
                size_t(if_true().size()),
                size_t(if_false().size())
            })};})
        {
            return std::min({
                size_t(filter().size()),
                size_t(if_true().size()),
                size_t(if_false().size())
            });
        }

        [[nodiscard]] constexpr ssize_t ssize() const
            noexcept (requires{{std::min({
                ssize_t(filter().ssize()),
                ssize_t(if_true().ssize()),
                ssize_t(if_false().ssize())
            })} noexcept;})
            requires (requires{{std::min({
                ssize_t(filter().ssize()),
                ssize_t(if_true().ssize()),
                ssize_t(if_false().ssize())
            })};})
        {
            return std::min({
                ssize_t(filter().ssize()),
                ssize_t(if_true().ssize()),
                ssize_t(if_false().ssize())
            });
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{
                filter().empty() || if_true().empty() || if_false().empty()
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                filter().empty() || if_true().empty() || if_false().empty()
            } -> meta::convertible_to<bool>;})
        {
            return filter().empty() || if_true().empty() || if_false().empty();
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
        static constexpr size_t normalize = impl::normalize_index<std::min(
            meta::tuple_size<true_type>,
            meta::tuple_size<false_type>
        ), I>();

        template <ssize_t I, typename Self>
        using get_type = meta::make_union<
            meta::get_type<decltype((std::declval<Self>().if_true())), normalize<I>>,
            meta::get_type<decltype((std::declval<Self>().if_false())), normalize<I>>
        >;

    public:
        template <typename Self>
        [[nodiscard]] constexpr front_type<Self> front(this Self&& self)
            noexcept (requires{
                {self.filter().front()} noexcept -> meta::nothrow::truthy;
                {
                    std::forward<Self>(self).if_true().front()
                } noexcept -> meta::nothrow::convertible_to<front_type<Self>>;
                {
                    std::forward<Self>(self).if_false().front()
                } noexcept -> meta::nothrow::convertible_to<front_type<Self>>;
            })
            requires (
                meta::front_returns<
                    front_type<Self>,
                    decltype((std::forward<Self>(self).if_true()))
                > &&
                meta::front_returns<
                    front_type<Self>,
                    decltype((std::forward<Self>(self).if_false()))
                > &&
                meta::front_returns<
                    bool,
                    decltype((std::forward<Self>(self).filter()))
                >
            )
        {
            if (self.filter().front()) {
                return std::forward<Self>(self).if_true().front();
            } else {
                return std::forward<Self>(self).if_false().front();
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr back_type<Self> back(this Self&& self)
            noexcept (requires{
                {self.filter().back()} noexcept -> meta::nothrow::truthy;
                {
                    std::forward<Self>(self).if_true().back()
                } noexcept -> meta::nothrow::convertible_to<back_type<Self>>;
                {
                    std::forward<Self>(self).if_false().back()
                } noexcept -> meta::nothrow::convertible_to<back_type<Self>>;
            })
            requires (
                meta::back_returns<
                    back_type<Self>,
                    decltype((std::forward<Self>(self).if_true()))
                > &&
                meta::back_returns<
                    back_type<Self>,
                    decltype((std::forward<Self>(self).if_false()))
                > &&
                meta::back_returns<
                    bool,
                    decltype((std::forward<Self>(self).filter()))
                >
            )
        {
            if (self.filter().back()) {
                return std::forward<Self>(self).if_true().back();
            } else {
                return std::forward<Self>(self).if_false().back();
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr subscript_type<Self> operator[](this Self&& self, ssize_t n)
            noexcept (requires(size_t m) {
                {size_t(impl::normalize_index(self.ssize(), n))} noexcept;
                {self.filter()[m]} noexcept -> meta::nothrow::truthy;
                {
                    std::forward<Self>(self).if_true()[m]
                } noexcept -> meta::nothrow::convertible_to<subscript_type<Self>>;
                {
                    std::forward<Self>(self).if_false()[m]
                } noexcept -> meta::nothrow::convertible_to<subscript_type<Self>>;
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
                meta::subscript_returns<
                    bool,
                    decltype((std::forward<Self>(self).filter())),
                    size_t
                >
            )
        {
            size_t m = size_t(impl::normalize_index(self.ssize(), n));
            if (self.filter()[m]) {
                return std::forward<Self>(self).if_true()[m];
            } else {
                return std::forward<Self>(self).if_false()[m];
            }
        }

        template <ssize_t I, typename Self>
            requires (
                meta::tuple_like<filter_type> &&
                meta::tuple_like<true_type> &&
                meta::tuple_like<false_type> &&
                impl::valid_index<
                    std::min({
                        meta::tuple_size<filter_type>,
                        meta::tuple_size<true_type>,
                        meta::tuple_size<false_type>
                    }),
                    I
                >
            )
        [[nodiscard]] constexpr get_type<I, Self> get(this Self&& self)
            noexcept (requires{
                {self.filter().template get<normalize<I>>()} noexcept -> meta::nothrow::truthy;
                {
                    std::forward<Self>(self).if_true().template get<normalize<I>>()
                } noexcept -> meta::nothrow::convertible_to<get_type<I, Self>>;
                {
                    std::forward<Self>(self).if_false().template get<normalize<I>>()
                } noexcept -> meta::nothrow::convertible_to<get_type<I, Self>>;
            })
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
                meta::get_returns<
                    bool,
                    decltype((std::forward<Self>(self).filter())),
                    normalize<I>
                >
            )
        {
            if (self.filter().template get<normalize<I>>()) {
                return std::forward<Self>(self).if_true().template get<normalize<I>>();
            } else {
                return std::forward<Self>(self).if_false().template get<normalize<I>>();
            }
        }





        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{ternary_mask_iterator{
                .mask_iter = filter().begin(),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                .mask_iter = filter().begin(),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            }};})
        {
            return ternary_mask_iterator{
                .mask_iter = filter().begin(),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            };
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{ternary_mask_iterator{
                .mask_iter = filter().begin(),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                .mask_iter = filter().begin(),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            }};})
        {
            return ternary_mask_iterator{
                .mask_iter = filter().begin(),
                .true_iter = if_true().begin(),
                .false_iter = if_false().begin(),
            };
        }

        [[nodiscard]] constexpr auto end()
            noexcept (requires{{ternary_mask_iterator{
                .mask_iter = filter().end(),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                .mask_iter = filter().end(),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            }};})
        {
            return ternary_mask_iterator{
                .mask_iter = filter().end(),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            };
        }

        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{ternary_mask_iterator{
                .mask_iter = filter().end(),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                .mask_iter = filter().end(),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            }};})
        {
            return ternary_mask_iterator{
                .mask_iter = filter().end(),
                .true_iter = if_true().end(),
                .false_iter = if_false().end(),
            };
        }

        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{ternary_mask_iterator{
                .mask_iter = filter().rbegin(),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                .mask_iter = filter().rbegin(),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            }};})
        {
            return ternary_mask_iterator{
                .mask_iter = filter().rbegin(),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            };
        }

        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{ternary_mask_iterator{
                .mask_iter = filter().rbegin(),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                .mask_iter = filter().rbegin(),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            }};})
        {
            return ternary_mask_iterator{
                .mask_iter = filter().rbegin(),
                .true_iter = if_true().rbegin(),
                .false_iter = if_false().rbegin(),
            };
        }

        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{ternary_mask_iterator{
                .mask_iter = filter().rend(),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                .mask_iter = filter().rend(),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            }};})
        {
            return ternary_mask_iterator{
                .mask_iter = filter().rend(),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            };
        }

        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{ternary_mask_iterator{
                .mask_iter = filter().rend(),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            }} noexcept;})
            requires (requires{{ternary_mask_iterator{
                .mask_iter = filter().rend(),
                .true_iter = if_true().rend(),
                .false_iter = if_false().rend(),
            }};})
        {
            return ternary_mask_iterator{
                .mask_iter = filter().rend(),
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

        template <typename F, typename C>
        constexpr bool enable_borrowed_range<bertrand::impl::binary_where<F, C>> =
            (bertrand::meta::lvalue<F> || enable_borrowed_range<bertrand::meta::unqualify<F>>) &&
            (bertrand::meta::lvalue<C> || enable_borrowed_range<bertrand::meta::unqualify<C>>);

        template <typename F, typename True, typename False>
        constexpr bool enable_borrowed_range<bertrand::impl::ternary_where<F, True, False>> =
            (bertrand::meta::lvalue<F> || enable_borrowed_range<bertrand::meta::unqualify<F>>) &&
            (bertrand::meta::lvalue<True> || enable_borrowed_range<bertrand::meta::unqualify<True>>) &&
            (bertrand::meta::lvalue<False> || enable_borrowed_range<bertrand::meta::unqualify<False>>);

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
        using type = bertrand::meta::remove_rvalue<decltype((
            std::declval<bertrand::impl::ternary_where<F, True, False>>().template get<I>()
        ))>;
    };

_LIBCPP_END_NAMESPACE_STD


#endif  // BERTRAND_ITER_WHERE_H