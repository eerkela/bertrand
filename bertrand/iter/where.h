#ifndef BERTRAND_ITER_WHERE_H
#define BERTRAND_ITER_WHERE_H

#include "bertrand/iter/range.h"


namespace bertrand {


namespace impl {

    /// TODO: revise documentation to note that the binary form is only available
    /// if the `true` value is a range.

    /// TODO: reverse iteration over a `where{}` expression would need to account
    /// for the arguments not all having the same size, or else it will give
    /// erroneous results.  This also has to be done for the binary case as well.

    /// TODO: `Self` must be allowed to be an rvalue, in which case we move the
    /// results out of it (but still store an lvalue internally).

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

    /* Binary `where{}` expressions correspond to a filter that omits values from a
    range where the predicate or mask evaluates to `false`.  Because this cannot be
    known generically ahead of time, the resulting range is unsized, not a tuple, and
    not indexable.  Its iterators may only satisfy forward or bidirectional modes, and
    will never be common. */
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

    /* In order to simplify the implementation of ternary `where` with scalar operands,
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
            return std::numeric_limits<difference_type>::max();
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
        }

        [[nodiscard]] constexpr bool operator<(const ternary_filter_iterator& other) const
            noexcept (requires{{
                filter < other.filter &&
                if_true < other.if_true &&
                if_false < other.if_false
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                filter < other.filter &&
                if_true < other.if_true &&
                if_false < other.if_false
            } -> meta::convertible_to<bool>;})
        {
            if constexpr (
                meta::specialization_of<True, trivial_repeat_iterator> &&
                meta::specialization_of<False, trivial_repeat_iterator>
            ) {
                return filter < other.filter;
            } else if constexpr (meta::specialization_of<True, trivial_repeat_iterator>) {
                return
                    filter < other.filter &&
                    if_false < other.if_false;
            } else if constexpr (meta::specialization_of<False, trivial_repeat_iterator>) {
                return
                    filter < other.filter &&
                    if_true < other.if_true;
            } else {
                return
                    filter < other.filter &&
                    if_true < other.if_true &&
                    if_false < other.if_false;
            }
        }

        [[nodiscard]] constexpr bool operator<=(const ternary_filter_iterator& other) const
            noexcept (requires{{
                filter <= other.filter &&
                if_true <= other.if_true &&
                if_false <= other.if_false
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                filter <= other.filter &&
                if_true <= other.if_true &&
                if_false <= other.if_false
            } -> meta::convertible_to<bool>;})
        {
            if constexpr (
                meta::specialization_of<True, trivial_repeat_iterator> &&
                meta::specialization_of<False, trivial_repeat_iterator>
            ) {
                return filter <= other.filter;
            } else if constexpr (meta::specialization_of<True, trivial_repeat_iterator>) {
                return
                    filter <= other.filter &&
                    if_false <= other.if_false;
            } else if constexpr (meta::specialization_of<False, trivial_repeat_iterator>) {
                return
                    filter <= other.filter &&
                    if_true <= other.if_true;
            } else {
                return
                    filter <= other.filter &&
                    if_true <= other.if_true &&
                    if_false <= other.if_false;
            }
        }

        [[nodiscard]] constexpr bool operator==(const ternary_filter_iterator& other) const
            noexcept (requires{{
                filter == other.filter ||
                if_true == other.if_true ||
                if_false == other.if_false
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                filter == other.filter ||
                if_true == other.if_true ||
                if_false == other.if_false
            } -> meta::convertible_to<bool>;})
        {
            if constexpr (
                meta::specialization_of<True, trivial_repeat_iterator> &&
                meta::specialization_of<False, trivial_repeat_iterator>
            ) {
                return filter == other.filter;
            } else if constexpr (meta::specialization_of<True, trivial_repeat_iterator>) {
                return
                    filter == other.filter ||
                    if_false == other.if_false;
            } else if constexpr (meta::specialization_of<False, trivial_repeat_iterator>) {
                return
                    filter == other.filter ||
                    if_true == other.if_true;
            } else {
                return
                    filter == other.filter ||
                    if_true == other.if_true ||
                    if_false == other.if_false;
            }
        }

        [[nodiscard]] constexpr bool operator!=(const ternary_filter_iterator& other) const
            noexcept (requires{{
                filter != other.filter &&
                if_true != other.if_true &&
                if_false != other.if_false
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                filter != other.filter &&
                if_true != other.if_true &&
                if_false != other.if_false
            } -> meta::convertible_to<bool>;})
        {
            if constexpr (
                meta::specialization_of<True, trivial_repeat_iterator> &&
                meta::specialization_of<False, trivial_repeat_iterator>
            ) {
                return filter != other.filter;
            } else if constexpr (meta::specialization_of<True, trivial_repeat_iterator>) {
                return
                    filter != other.filter &&
                    if_false != other.if_false;
            } else if constexpr (meta::specialization_of<False, trivial_repeat_iterator>) {
                return
                    filter != other.filter &&
                    if_true != other.if_true;
            } else {
                return
                    filter != other.filter &&
                    if_true != other.if_true &&
                    if_false != other.if_false;
            }
        }

        [[nodiscard]] constexpr bool operator>=(const ternary_filter_iterator& other) const
            noexcept (requires{{
                filter >= other.filter &&
                if_true >= other.if_true &&
                if_false >= other.if_false
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                filter >= other.filter &&
                if_true >= other.if_true &&
                if_false >= other.if_false
            } -> meta::convertible_to<bool>;})
        {
            if constexpr (
                meta::specialization_of<True, trivial_repeat_iterator> &&
                meta::specialization_of<False, trivial_repeat_iterator>
            ) {
                return filter >= other.filter;
            } else if constexpr (meta::specialization_of<True, trivial_repeat_iterator>) {
                return
                    filter >= other.filter &&
                    if_false >= other.if_false;
            } else if constexpr (meta::specialization_of<False, trivial_repeat_iterator>) {
                return
                    filter >= other.filter &&
                    if_true >= other.if_true;
            } else {
                return
                    filter >= other.filter &&
                    if_true >= other.if_true &&
                    if_false >= other.if_false;
            }
        }

        [[nodiscard]] constexpr bool operator>(const ternary_filter_iterator& other) const
            noexcept (requires{{
                filter > other.filter &&
                if_true > other.if_true &&
                if_false > other.if_false
            } noexcept -> meta::nothrow::convertible_to<bool>;})
            requires (requires{{
                filter > other.filter &&
                if_true > other.if_true &&
                if_false > other.if_false
            } -> meta::convertible_to<bool>;})
        {
            if constexpr (
                meta::specialization_of<True, trivial_repeat_iterator> &&
                meta::specialization_of<False, trivial_repeat_iterator>
            ) {
                return filter > other.filter;
            } else if constexpr (meta::specialization_of<True, trivial_repeat_iterator>) {
                return
                    filter > other.filter &&
                    if_false > other.if_false;
            } else if constexpr (meta::specialization_of<False, trivial_repeat_iterator>) {
                return
                    filter > other.filter &&
                    if_true > other.if_true;
            } else {
                return
                    filter > other.filter &&
                    if_true > other.if_true &&
                    if_false > other.if_false;
            }
        }
    };
    template <typename Mask, typename True, typename False>
    ternary_filter_iterator(Mask, True, False) -> ternary_filter_iterator<Mask, True, False>;

    template <meta::not_rvalue Filter, meta::not_rvalue True, meta::not_rvalue False>
    struct ternary_filter;

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
        requires (meta::unqualified<Self> && Self::tuple_like)
    constexpr size_t ternary_filter_tuple_size<Self> = (
        Self::mask ||
        meta::range<typename Self::true_type> ||
        meta::range<typename Self::false_type>
    ) ?
        iter::min{}(
            Self::mask ?
                meta::tuple_size<meta::as_range_or_scalar<typename Self::filter_type>> :
                std::numeric_limits<size_t>::max(),
            meta::range<typename Self::true_type> ?
                meta::tuple_size<meta::as_range_or_scalar<typename Self::true_type>> :
                std::numeric_limits<size_t>::max(),
            meta::range<typename Self::false_type> ?
                meta::tuple_size<meta::as_range_or_scalar<typename Self::false_type>> :
                std::numeric_limits<size_t>::max()
        ) : 1;

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

    template <typename T>
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
    the length of the other operands.

    Since the size may be known, the resulting range may be tuple-like and can be
    indexed if all of the operands support these operations. */
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
            } else if constexpr (mask || meta::range<true_type> || meta::range<false_type>) {
                return iter::min{}(
                    ternary_filter_size(filter()),
                    ternary_filter_size(if_true()),
                    ternary_filter_size(if_false())
                );
            } else {
                return 1;
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
            } else if constexpr (mask || meta::range<true_type> || meta::range<false_type>) {
                return iter::min{}(
                    ternary_filter_ssize(filter()),
                    ternary_filter_ssize(if_true()),
                    ternary_filter_ssize(if_false())
                );
            } else {
                return 1;
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
            if constexpr (mask || meta::range<true_type> || meta::range<false_type>) {
                return ternary_filter_iterator{
                    .filter = ternary_filter_begin(std::forward<Self>(self).filter()),
                    .if_true = ternary_filter_begin(std::forward<Self>(self).if_true()),
                    .if_false = ternary_filter_begin(std::forward<Self>(self).if_false()),
                };
            } else {
                return ternary_filter_iterator{
                    .filter = ternary_filter_begin(std::forward<Self>(self).filter()),
                    .if_true = std::addressof(self.if_true()),
                    .if_false = std::addressof(self.if_false()),
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
            if constexpr (mask || meta::range<true_type> || meta::range<false_type>) {
                return ternary_filter_iterator{
                    .filter = ternary_filter_end(std::forward<Self>(self).filter()),
                    .if_true = ternary_filter_end(std::forward<Self>(self).if_true()),
                    .if_false = ternary_filter_end(std::forward<Self>(self).if_false()),
                };
            } else {
                return ternary_filter_iterator{
                    .filter = ternary_filter_end(std::forward<Self>(self).filter()),
                    .if_true = std::addressof(self.if_true()) + 1,
                    .if_false = std::addressof(self.if_false()) + 1,
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
            if constexpr (mask || meta::range<true_type> || meta::range<false_type>) {
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
                    .if_true = std::make_reverse_iterator(std::addressof(self.if_true()) + 1),
                    .if_false = std::make_reverse_iterator(std::addressof(self.if_false()) + 1),
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
            if constexpr (mask || meta::range<true_type> || meta::range<false_type>) {
                return ternary_filter_iterator{
                    .filter = ternary_filter_rend(std::forward<Self>(self).filter()),
                    .if_true = ternary_filter_rend(std::forward<Self>(self).if_true()),
                    .if_false = ternary_filter_rend(std::forward<Self>(self).if_false()),
                };
            } else {
                return ternary_filter_iterator{
                    .filter = ternary_filter_rend(std::forward<Self>(self).filter()),
                    .if_true = std::make_reverse_iterator(std::addressof(self.if_true())),
                    .if_false = std::make_reverse_iterator(std::addressof(self.if_false())),
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
            requires ((meta::range<meta::as_const_ref<Filter>, bool> || (
                !meta::range<Filter> &&
                meta::call_returns<
                    bool,
                    meta::as_const_ref<Filter>,
                    meta::yield_type<T>
                >
            )) && requires{{range{impl::binary_filter{
                std::forward<Self>(self).filter,
                std::forward<T>(r)
            }}};})
        {
            return range{impl::binary_filter{
                std::forward<Self>(self).filter,
                std::forward<T>(r)
            }};
        }

        template <typename Self, typename True, typename False>
        [[nodiscard]] constexpr auto operator()(this Self&& self, True&& t, False&& f)
            noexcept (requires{{range{impl::ternary_filter{
                std::forward<Self>(self).filter,
                std::forward<True>(t),
                std::forward<False>(f)
            }}} noexcept;})
            requires ((meta::range<meta::as_const_ref<Filter>, bool> || (
                !meta::range<Filter> &&
                meta::call_returns<
                    bool,
                    meta::as_const_ref<Filter>,
                    meta::yield_type<meta::as_range_or_scalar<True>>
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

_LIBCPP_END_NAMESPACE_STD


namespace bertrand::iter {

    static constexpr auto w = where{range(std::array{true, true, false})}(
        range(std::array{1, 2, 3, 4}),
        10
    );
    static_assert(w.size() == 3);
    static_assert(w.ssize() == 3);
    static_assert(!w.empty());
    static_assert(w.front() == 1);
    static_assert(w.back() == 10);
    static_assert(w[0] == 1);
    static_assert(w[1] == 2);
    static_assert(w[2] == 10);
    static_assert(w[-1] == 10);
    static_assert(w[-2] == 2);
    static_assert(w[-3] == 1);
    static_assert(w.get<0>() == 1);
    static_assert(w.get<1>() == 2);
    static_assert(w.get<2>() == 10);
    static_assert(w.get<-1>() == 10);
    static_assert(w.get<-2>() == 2);
    static_assert(w.get<-3>() == 1);
    static_assert([] {
        auto it = w.begin();
        if (*it != 1) return false;
        ++it; if (*it != 2) return false;
        ++it; if (*it != 10) return false;
        ++it; if (it != w.end()) return false;

        auto rit = w.rbegin();
        if (*rit != 10) return false;
        ++rit; if (*rit != 2) return false;
        ++rit; if (*rit != 1) return false;
        ++rit; if (rit != w.rend()) return false;

        auto&& [a, b, c] = w;
        return true;
    }());


    static constexpr auto w2 = where{[](int x){ return x < 3; }}(
        range(std::array{1, 2, 3, 4}),
        10
    );
    static_assert(w2.size() == 4);
    static_assert(w2.ssize() == 4);
    static_assert(!w2.empty());
    static_assert(w2.front() == 1);
    static_assert(w2.back() == 10);
    static_assert(w2[0] == 1);
    static_assert(w2[1] == 2);
    static_assert(w2[2] == 10);
    static_assert(w2[3] == 10);
    static_assert(w2[-1] == 10);
    static_assert(w2[-2] == 10);
    static_assert(w2[-3] == 2);
    static_assert(w2[-4] == 1);
    static_assert(w2.get<0>() == 1);
    static_assert(w2.get<1>() == 2);
    static_assert(w2.get<2>() == 10);
    static_assert(w2.get<3>() == 10);
    static_assert(w2.get<-1>() == 10);
    static_assert(w2.get<-2>() == 10);
    static_assert(w2.get<-3>() == 2);
    static_assert(w2.get<-4>() == 1);
    static_assert([] {
        auto it = w2.begin();
        if (*it != 1) return false;
        ++it; if (*it != 2) return false;
        ++it; if (*it != 10) return false;
        ++it; if (*it != 10) return false;
        ++it; if (it != w2.end()) return false;

        auto rit = w2.rbegin();
        if (*rit != 10) return false;
        ++rit; if (*rit != 10) return false;
        ++rit; if (*rit != 2) return false;
        ++rit; if (*rit != 1) return false;
        ++rit; if (rit != w2.rend()) return false;

        auto&& [a, b, c, d] = w2;
        return true;
    }());

    /// TODO: this special case must have a size of 1 unless the replacement range is
    /// empty.  Luckily, pretty much everything else is good to go.

    static constexpr auto w3 = where{[](int x) { return x < 3; }}(
        3,
        range(std::array{2, 3, 4})
    );
    static_assert(w3.size() == 1);
    static_assert(w3.ssize() == 1);
    static_assert(!w3.empty());
    static_assert(w3.front() == 2);
    static_assert(w3.back() == 2);
    static_assert(w3[0] == 2);
    static_assert(w3[-1] == 2);
    static_assert(w3.get<0>() == 2);
    static_assert(w3.get<-1>() == 2);
    static_assert([] {
        auto it = w3.begin();
        if (*it != 2) return false;
        ++it; if (it != w3.end()) return false;

        auto rit = w3.rbegin();
        if (*rit != 2) return false;
        ++rit; if (rit != w3.rend()) return false;

        auto&& [a] = w3;
        return true;
    }());


}


#endif  // BERTRAND_ITER_WHERE_H