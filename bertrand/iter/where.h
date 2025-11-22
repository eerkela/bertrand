#ifndef BERTRAND_ITER_WHERE_H
#define BERTRAND_ITER_WHERE_H

#include "bertrand/iter/range.h"


namespace bertrand {


namespace impl {

    /* Ternary filters never change the overall size of the underlying range, and never
    require the iterator to skip over false elements.  The iterator can therefore be
    either forward, bidirectional, or random access without any issues. */
    template <meta::not_reference Self, typename True, typename False>
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

        [[no_unique_address]] Self* self = nullptr;
        [[no_unique_address]] True true_iter;
        [[no_unique_address]] False false_iter;

    private:
        template <typename T>
        constexpr reference deref(T&& value) const
            noexcept (requires{
                {(*self->func)(value)} noexcept -> meta::nothrow::truthy;
                {std::forward<T>(value)} noexcept -> meta::nothrow::convertible_to<reference>;
                {*false_iter} noexcept -> meta::nothrow::convertible_to<reference>;
            })
            requires (requires{
                {(*self->func)(value)} -> meta::truthy;
                {std::forward<T>(value)} -> meta::convertible_to<reference>;
                {*false_iter} -> meta::convertible_to<reference>;
            })
        {
            if ((*self->func)(value)) {
                return std::forward<T>(value);
            } else {
                return *false_iter;
            }
        }

        template <typename T>
        constexpr reference deref(T&& value, difference_type n) const
            noexcept (requires{
                {(*self->func)(value)} noexcept -> meta::nothrow::truthy;
                {std::forward<T>(value)} noexcept -> meta::nothrow::convertible_to<reference>;
                {false_iter[n]} noexcept -> meta::nothrow::convertible_to<reference>;
            })
            requires (requires{
                {(*self->func)(value)} -> meta::truthy;
                {std::forward<T>(value)} -> meta::convertible_to<reference>;
                {false_iter[n]} -> meta::convertible_to<reference>;
            })
        {
            if ((*self->func)(value)) {
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
                self,
                true_iter++,
                false_iter++
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                self,
                true_iter++,
                false_iter++
            }};})
        {
            return ternary_where_iterator{
                self,
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
                self.self,
                self.true_iter + n,
                self.false_iter + n
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                self.self,
                self.true_iter + n,
                self.false_iter + n
            }};})
        {
            return ternary_where_iterator{
                self.self,
                self.true_iter + n,
                self.false_iter + n
            };
        }

        [[nodiscard]] friend constexpr ternary_where_iterator operator+(
            difference_type n,
            const ternary_where_iterator& self
        )
            noexcept (requires{{ternary_where_iterator{
                self.self,
                self.true_iter + n,
                self.false_iter + n
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                self.self,
                self.true_iter + n,
                self.false_iter + n
            }};})
        {
            return ternary_where_iterator{
                self.self,
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
                self,
                true_iter--,
                false_iter--
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                self,
                true_iter--,
                false_iter--
            }};})
        {
            return ternary_where_iterator{
                self,
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
                self,
                true_iter - n,
                false_iter - n
            }} noexcept;})
            requires (requires{{ternary_where_iterator{
                self,
                true_iter - n,
                false_iter - n
            }};})
        {
            return ternary_where_iterator{
                self,
                true_iter - n,
                false_iter - n
            };
        }

        [[nodiscard]] constexpr difference_type operator-(const ternary_where_iterator& other) const
            noexcept (requires{{
                std::min(true_iter - other.true_iter, false_iter - other.false_iter)
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (requires{{
                std::min(true_iter - other.true_iter, false_iter - other.false_iter)
            } -> meta::convertible_to<difference_type>;})
        {
            return std::min(true_iter - other.true_iter, false_iter - other.false_iter);
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

        [[no_unique_address]] impl::ref<function_type> func;
        [[no_unique_address]] impl::ref<true_type> if_true;
        [[no_unique_address]] impl::ref<false_type> if_false;

        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{ternary_where_iterator<
                ternary_where,
                meta::begin_type<meta::as_lvalue<true_type>>,
                meta::begin_type<meta::as_lvalue<false_type>>
            >{
                .self = this,
                .true_iter = if_true->begin(),
                .false_iter = if_false->begin(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator<
                ternary_where,
                meta::begin_type<meta::as_lvalue<true_type>>,
                meta::begin_type<meta::as_lvalue<false_type>>
            >{
                .self = this,
                .true_iter = if_true->begin(),
                .false_iter = if_false->begin(),
            }};})
        {
            return ternary_where_iterator<
                ternary_where,
                meta::begin_type<meta::as_lvalue<true_type>>,
                meta::begin_type<meta::as_lvalue<false_type>>
            >{
                .self = this,
                .true_iter = if_true->begin(),
                .false_iter = if_false->begin(),
            };
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{ternary_where_iterator<
                const ternary_where,
                meta::begin_type<meta::as_const_ref<true_type>>,
                meta::begin_type<meta::as_const_ref<false_type>>
            >{
                .self = this,
                .true_iter = if_true->begin(),
                .false_iter = if_false->begin(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator<
                const ternary_where,
                meta::begin_type<meta::as_const_ref<true_type>>,
                meta::begin_type<meta::as_const_ref<false_type>>
            >{
                .self = this,
                .true_iter = if_true->begin(),
                .false_iter = if_false->begin(),
            }};})
        {
            return ternary_where_iterator<
                const ternary_where,
                meta::begin_type<meta::as_const_ref<true_type>>,
                meta::begin_type<meta::as_const_ref<false_type>>
            >{
                .self = this,
                .true_iter = if_true->begin(),
                .false_iter = if_false->begin(),
            };
        }

        [[nodiscard]] constexpr auto end()
            noexcept (requires{{ternary_where_iterator<
                ternary_where,
                meta::end_type<meta::as_lvalue<true_type>>,
                meta::end_type<meta::as_lvalue<false_type>>
            >{
                .self = this,
                .true_iter = if_true->end(),
                .false_iter = if_false->end(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator<
                ternary_where,
                meta::end_type<meta::as_lvalue<true_type>>,
                meta::end_type<meta::as_lvalue<false_type>>
            >{
                .self = this,
                .true_iter = if_true->end(),
                .false_iter = if_false->end(),
            }};})
        {
            return ternary_where_iterator<
                ternary_where,
                meta::end_type<meta::as_lvalue<true_type>>,
                meta::end_type<meta::as_lvalue<false_type>>
            >{
                .self = this,
                .true_iter = if_true->end(),
                .false_iter = if_false->end(),
            };
        }

        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{ternary_where_iterator<
                const ternary_where,
                meta::end_type<meta::as_const_ref<true_type>>,
                meta::end_type<meta::as_const_ref<false_type>>
            >{
                .self = this,
                .true_iter = if_true->end(),
                .false_iter = if_false->end(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator<
                const ternary_where,
                meta::end_type<meta::as_const_ref<true_type>>,
                meta::end_type<meta::as_const_ref<false_type>>
            >{
                .self = this,
                .true_iter = if_true->end(),
                .false_iter = if_false->end(),
            }};})
        {
            return ternary_where_iterator<
                const ternary_where,
                meta::end_type<meta::as_const_ref<true_type>>,
                meta::end_type<meta::as_const_ref<false_type>>
            >{
                .self = this,
                .true_iter = if_true->end(),
                .false_iter = if_false->end(),
            };
        }

        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{ternary_where_iterator<
                ternary_where,
                meta::rbegin_type<meta::as_lvalue<true_type>>,
                meta::rbegin_type<meta::as_lvalue<false_type>>
            >{
                .self = this,
                .true_iter = if_true->rbegin(),
                .false_iter = if_false->rbegin(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator<
                ternary_where,
                meta::rbegin_type<meta::as_lvalue<true_type>>,
                meta::rbegin_type<meta::as_lvalue<false_type>>
            >{
                .self = this,
                .true_iter = if_true->rbegin(),
                .false_iter = if_false->rbegin(),
            }};})
        {
            return ternary_where_iterator<
                ternary_where,
                meta::rbegin_type<meta::as_lvalue<true_type>>,
                meta::rbegin_type<meta::as_lvalue<false_type>>
            >{
                .self = this,
                .true_iter = if_true->rbegin(),
                .false_iter = if_false->rbegin(),
            };
        }

        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{ternary_where_iterator<
                const ternary_where,
                meta::rbegin_type<meta::as_const_ref<true_type>>,
                meta::rbegin_type<meta::as_const_ref<false_type>>
            >{
                .self = this,
                .true_iter = if_true->rbegin(),
                .false_iter = if_false->rbegin(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator<
                const ternary_where,
                meta::rbegin_type<meta::as_const_ref<true_type>>,
                meta::rbegin_type<meta::as_const_ref<false_type>>
            >{
                .self = this,
                .true_iter = if_true->rbegin(),
                .false_iter = if_false->rbegin(),
            }};})
        {
            return ternary_where_iterator<
                const ternary_where,
                meta::rbegin_type<meta::as_const_ref<true_type>>,
                meta::rbegin_type<meta::as_const_ref<false_type>>
            >{
                .self = this,
                .true_iter = if_true->rbegin(),
                .false_iter = if_false->rbegin(),
            };
        }

        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{ternary_where_iterator<
                ternary_where,
                meta::rend_type<meta::as_const_ref<true_type>>,
                meta::rend_type<meta::as_const_ref<false_type>>
            >{
                .self = this,
                .true_iter = if_true->rend(),
                .false_iter = if_false->rend(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator<
                ternary_where,
                meta::rend_type<meta::as_const_ref<true_type>>,
                meta::rend_type<meta::as_const_ref<false_type>>
            >{
                .self = this,
                .true_iter = if_true->rend(),
                .false_iter = if_false->rend(),
            }};})
        {
            return ternary_where_iterator<
                ternary_where,
                meta::rend_type<meta::as_const_ref<true_type>>,
                meta::rend_type<meta::as_const_ref<false_type>>
            >{
                .self = this,
                .true_iter = if_true->rend(),
                .false_iter = if_false->rend(),
            };
        }

        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{ternary_where_iterator<
                const ternary_where,
                meta::rend_type<meta::as_const_ref<true_type>>,
                meta::rend_type<meta::as_const_ref<false_type>>
            >{
                .self = this,
                .true_iter = if_true->rend(),
                .false_iter = if_false->rend(),
            }} noexcept;})
            requires (requires{{ternary_where_iterator<
                const ternary_where,
                meta::rend_type<meta::as_const_ref<true_type>>,
                meta::rend_type<meta::as_const_ref<false_type>>
            >{
                .self = this,
                .true_iter = if_true->rend(),
                .false_iter = if_false->rend(),
            }};})
        {
            return ternary_where_iterator<
                const ternary_where,
                meta::rend_type<meta::as_const_ref<true_type>>,
                meta::rend_type<meta::as_const_ref<false_type>>
            >{
                .self = this,
                .true_iter = if_true->rend(),
                .false_iter = if_false->rend(),
            };
        }
    };

    template <typename F, typename True, typename False>
    ternary_where(F&&, True&&, False&&) -> ternary_where<
        meta::remove_rvalue<F>,
        meta::remove_rvalue<True>,
        meta::remove_rvalue<False>
    >;

    /* Binary filters require the iterator to skip over false elements, thereby
    preventing it from being a random access iterator, since the location of these
    skips cannot be known ahead of time.  The iterator is thus restricted only to
    forward or bidirectional modes. */
    template <meta::not_reference Self, typename Begin, typename End>
    struct binary_where_iterator {
        using category = std::conditional_t<
            meta::inherits<meta::iterator_category<Begin>, std::bidirectional_iterator_tag>,
            std::bidirectional_iterator_tag,
            std::forward_iterator_tag
        >;
        using difference_type = meta::iterator_difference<Begin>;
        using reference = meta::dereference_type<meta::as_const_ref<Begin>>;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<value_type>;

        [[no_unique_address]] Self* self = nullptr;
        [[no_unique_address]] difference_type index = 0;
        [[no_unique_address]] Begin begin;
        [[no_unique_address]] End end;

        constexpr void init()
            noexcept (requires{
                {begin != end} noexcept -> meta::nothrow::truthy;
                {(*self->func)(*begin)} noexcept -> meta::nothrow::truthy;
                {++begin} noexcept;
            })
            requires (requires{
                {begin != end} -> meta::truthy;
                {(*self->func)(*begin)} -> meta::truthy;
                {++begin};
            })
        {
            while (begin != end) {
                if ((*self->func)(*begin)) {
                    break;
                }
                ++begin;
                ++index;
            }
        }

        [[nodiscard]] constexpr decltype(auto) operator*() const
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
                {(*self->func)(*begin)} noexcept -> meta::nothrow::truthy;
            })
            requires (requires{
                {++begin};
                {begin != end} -> meta::truthy;
                {(*self->func)(*begin)} -> meta::truthy;
            })
        {
            ++begin;
            ++index;
            while (begin != end) {
                if ((*self->func)(*begin)) {
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
                {(*self->func)(*begin)} noexcept -> meta::nothrow::truthy;
            })
            requires (requires{
                {--begin};
                {(*self->func)(*begin)} -> meta::truthy;
            })
        {
            --begin;
            --index;
            while (index >= 0) {
                if ((*self->func)(*begin)) {
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

    /* Binary `where{}` expressions correspond to a filter that omits values where the
    predicate evaluates to `false`.  Because this cannot be known generically ahead of
    time, the resulting range is unsized, not a tuple, and not reliably indexable. */
    template <meta::not_rvalue F, meta::not_rvalue C>
    struct binary_where {
        using function_type = F;
        using true_type = meta::as_range<C>;

        [[no_unique_address]] impl::ref<function_type> func;
        [[no_unique_address]] impl::ref<true_type> if_true;

        [[nodiscard]] constexpr auto begin()
            noexcept (requires(binary_where_iterator<
                binary_where,
                meta::begin_type<meta::as_lvalue<true_type>>,
                meta::end_type<meta::as_lvalue<true_type>>
            > result) {
                {binary_where_iterator<
                    binary_where,
                    meta::begin_type<meta::as_lvalue<true_type>>,
                    meta::end_type<meta::as_lvalue<true_type>>
                >{
                    .self = this,
                    .index = 0,
                    .begin = if_true->begin(),
                    .end = if_true->end()
                }} noexcept;
                {result.init()};
            })
            requires (requires(binary_where_iterator<
                binary_where,
                meta::begin_type<meta::as_lvalue<true_type>>,
                meta::end_type<meta::as_lvalue<true_type>>
            > result) {
                {binary_where_iterator<
                    binary_where,
                    meta::begin_type<meta::as_lvalue<true_type>>,
                    meta::end_type<meta::as_lvalue<true_type>>
                >{
                    .self = this,
                    .index = 0,
                    .begin = if_true->begin(),
                    .end = if_true->end()
                }};
                {result.init()};
            })
        {
            binary_where_iterator<
                binary_where,
                meta::begin_type<meta::as_lvalue<true_type>>,
                meta::end_type<meta::as_lvalue<true_type>>
            > result {
                .self = this,
                .index = 0,
                .begin = if_true->begin(),
                .end = if_true->end()
            };
            result.init();
            return result;
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires(binary_where_iterator<
                const binary_where,
                meta::begin_type<meta::as_const_ref<true_type>>,
                meta::end_type<meta::as_const_ref<true_type>>
            > result) {
                {binary_where_iterator<
                    const binary_where,
                    meta::begin_type<meta::as_const_ref<true_type>>,
                    meta::end_type<meta::as_const_ref<true_type>>
                >{
                    .self = this,
                    .index = 0,
                    .begin = if_true->begin(),
                    .end = if_true->end()
                }} noexcept;
                {result.init()};
            })
            requires (requires(binary_where_iterator<
                const binary_where,
                meta::begin_type<meta::as_const_ref<true_type>>,
                meta::end_type<meta::as_const_ref<true_type>>
            > result) {
                {binary_where_iterator<
                    const binary_where,
                    meta::begin_type<meta::as_const_ref<true_type>>,
                    meta::end_type<meta::as_const_ref<true_type>>
                >{
                    .self = this,
                    .index = 0,
                    .begin = if_true->begin(),
                    .end = if_true->end()
                }};
                {result.init()};
            })
        {
            binary_where_iterator<
                const binary_where,
                meta::begin_type<meta::as_const_ref<true_type>>,
                meta::end_type<meta::as_const_ref<true_type>>
            > result {
                .self = this,
                .index = 0,
                .begin = if_true->begin(),
                .end = if_true->end()
            };
            result.init();
            return result;
        }

        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }
        
        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires(binary_where_iterator<
                binary_where,
                meta::rbegin_type<meta::as_lvalue<true_type>>,
                meta::rend_type<meta::as_lvalue<true_type>>
            > result) {
                {binary_where_iterator<
                    binary_where,
                    meta::rbegin_type<meta::as_lvalue<true_type>>,
                    meta::rend_type<meta::as_lvalue<true_type>>
                >{
                    .self = this,
                    .index = 0,
                    .begin = if_true->rbegin(),
                    .end = if_true->rend()
                }} noexcept;
                {result.init()};
            })
            requires (requires(binary_where_iterator<
                binary_where,
                meta::rbegin_type<meta::as_lvalue<true_type>>,
                meta::rend_type<meta::as_lvalue<true_type>>
            > result) {
                {binary_where_iterator<
                    binary_where,
                    meta::rbegin_type<meta::as_lvalue<true_type>>,
                    meta::rend_type<meta::as_lvalue<true_type>>
                >{
                    .self = this,
                    .index = 0,
                    .begin = if_true->rbegin(),
                    .end = if_true->rend()
                }};
                {result.init()};
            })
        {
            binary_where_iterator<
                binary_where,
                meta::rbegin_type<meta::as_lvalue<true_type>>,
                meta::rend_type<meta::as_lvalue<true_type>>
            > result {
                .self = this,
                .index = 0,
                .begin = if_true->rbegin(),
                .end = if_true->rend()
            };
            result.init();
            return result;
        }

        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires(binary_where_iterator<
                const binary_where,
                meta::rbegin_type<meta::as_const_ref<true_type>>,
                meta::rend_type<meta::as_const_ref<true_type>>
            > result) {
                {binary_where_iterator<
                    const binary_where,
                    meta::rbegin_type<meta::as_const_ref<true_type>>,
                    meta::rend_type<meta::as_const_ref<true_type>>
                >{
                    .self = this,
                    .index = 0,
                    .begin = if_true->rbegin(),
                    .end = if_true->rend()
                }} noexcept;
                {result.init()};
            })
            requires (requires(binary_where_iterator<
                const binary_where,
                meta::rbegin_type<meta::as_const_ref<true_type>>,
                meta::rend_type<meta::as_const_ref<true_type>>
            > result) {
                {binary_where_iterator<
                    const binary_where,
                    meta::rbegin_type<meta::as_const_ref<true_type>>,
                    meta::rend_type<meta::as_const_ref<true_type>>
                >{
                    .self = this,
                    .index = 0,
                    .begin = if_true->rbegin(),
                    .end = if_true->rend()
                }};
                {result.init()};
            })
        {
            binary_where_iterator<
                const binary_where,
                meta::rbegin_type<meta::as_const_ref<true_type>>,
                meta::rend_type<meta::as_const_ref<true_type>>
            > result {
                .self = this,
                .index = 0,
                .begin = if_true->rbegin(),
                .end = if_true->rend()
            };
            result.init();
            return result;
        }

        [[nodiscard]] static constexpr NoneType rend() noexcept { return {}; }
    };

    template <typename F, typename C>
    binary_where(F&&, C&&) -> binary_where<meta::remove_rvalue<F>, meta::remove_rvalue<C>>;

}


namespace iter {

    /// TODO: document this, including both the binary and ternary forms

    /*  */
    template <meta::not_rvalue F>
    struct where {
        [[no_unique_address]] F func;

        template <typename Self, typename T>
        [[nodiscard]] constexpr auto operator()(this Self&& self, T&& r)
            noexcept (requires{{range{impl::binary_where{
                std::forward<Self>(self).func,
                std::forward<T>(r)
            }}} noexcept;})
            requires (meta::call_returns<
                bool,
                meta::as_const_ref<F>,
                meta::yield_type<meta::as_range<T>>
            > &&
            requires{{range{impl::binary_where{
                std::forward<Self>(self).func,
                std::forward<T>(r)
            }}};})
        {
            return range{impl::binary_where{
                std::forward<Self>(self).func,
                std::forward<T>(r)
            }};
        }

        template <typename Self, typename True, typename False>
        [[nodiscard]] constexpr auto operator()(this Self&& self, True&& t, False&& f)
            noexcept (requires{{range{impl::ternary_where{
                std::forward<Self>(self).func,
                std::forward<True>(t),
                std::forward<False>(f)
            }}} noexcept;})
            requires (meta::call_returns<
                bool,
                meta::as_const_ref<F>,
                meta::yield_type<meta::as_range<True>>
            > && requires{{range{impl::ternary_where{
                std::forward<Self>(self).func,
                std::forward<True>(t),
                std::forward<False>(f)
            }}};})
        {
            return range{impl::ternary_where{
                std::forward<Self>(self).func,
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



    }



_LIBCPP_END_NAMESPACE_STD


namespace bertrand::iter {


    static constexpr auto w = where{[](int x) { return x > 1; }}(
        std::array{1, 2, 3}
    );
    static_assert([] {
        auto it1 = w->begin();
        if (*it1++ != 2) return false;
        if (*it1++ != 3) return false;
        if (it1 != w->end()) return false;

        auto it2 = w->rbegin();
        if (*it2++ != 3) return false;
        if (*it2++ != 2) return false;
        if (it2 != w->rend()) return false;

        for (auto&& x : w) {
            if (x != 1 && x != 2 && x != 3) {
                return false;
            }
        }

        return true;
    }());

    static constexpr auto w2 = where{[](int x) { return x > 1; }}(
        std::array{1, 2, 3},
        std::array{4, 5, 6}
    );
    static_assert(w2->end() - w2->begin() == 3);
    static_assert([] {
        auto it1 = w2->begin();
        if (*it1++ != 4) return false;
        if (*it1++ != 2) return false;
        if (*it1++ != 3) return false;
        if (it1 != w2->end()) return false;

        auto it2 = w2->rbegin();
        if (*it2++ != 3) return false;
        if (*it2++ != 2) return false;
        if (*it2++ != 4) return false;
        if (it2 != w2->rend()) return false;

        for (auto&& x : w2) {
            if (x != 4 && x != 2 && x != 3) {
                return false;
            }
        }

        return true;
    }());


}


#endif  // BERTRAND_ITER_WHERE_H