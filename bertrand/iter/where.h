#ifndef BERTRAND_ITER_WHERE_H
#define BERTRAND_ITER_WHERE_H

#include "bertrand/iter/range.h"


namespace bertrand {


namespace impl {

    /// TODO: where{} iterators are restricted to bidirectional or forward iterators
    /// unless there is a replacement range provided, in which case they can be random
    /// access, but not contiguous.

    /* Ternary filters never change the overall size of the underlying range, and never
    require the iterator to skip over false elements.  The iterator can therefore be
    either forward, bidirectional, or random access without any issues. */
    template <
        meta::not_reference Self,
        typename TrueBegin,
        typename TrueEnd,
        typename FalseBegin = void,
        typename FalseEnd = void
    >
    struct where_iterator {
        // Self* self = nullptr;
        // TrueIter true_iter;
        // FalseIter false_iter;

    };

    /* Ternary `where{}` expressions correspond to a merge between the left and right
    operands, choosing the left operand when the predicate evaluates to `true` and the
    right operand when it evaluates to `false`.  This never changes the size of the
    underlying ranges, except to truncate to the shortest operand where appropriate.
    The resulting range can therefore be sized, tuple-like, and indexed provided the
    operands support these operations. */
    template <typename F, typename L, typename R = void>
    struct where {
        using function_type = F;
        using true_type = meta::as_range_or_scalar<L>;
        using false_type = meta::as_range_or_scalar<R>;

        [[no_unique_address]] impl::ref<function_type> func;
        [[no_unique_address]] impl::ref<true_type> if_true;
        [[no_unique_address]] impl::ref<false_type> if_false;

    };

    /* Binary filters require the iterator to skip over false elements, thereby
    preventing it from being a random access iterator, since the location of these
    skips cannot be known ahead of time.  The iterator is thus restricted only to
    forward or bidirectional modes. */
    template <meta::not_reference Self, typename TrueBegin, typename TrueEnd>
    struct where_iterator<Self, TrueBegin, TrueEnd, void, void> {
        using category = std::conditional_t<
            meta::inherits<meta::iterator_category<TrueBegin>, std::bidirectional_iterator_tag>,
            std::bidirectional_iterator_tag,
            std::forward_iterator_tag
        >;
        using difference_type = meta::iterator_difference<TrueBegin>;
        using reference = meta::iterator_reference<TrueBegin>;
        using value_type = meta::iterator_value<TrueBegin>;
        using pointer = meta::iterator_pointer<TrueBegin>;

        [[no_unique_address]] Self* self = nullptr;
        [[no_unique_address]] difference_type index = 0;
        [[no_unique_address]] TrueBegin true_begin;
        [[no_unique_address]] TrueEnd true_end;

        constexpr void init()
            noexcept (requires{
                {true_begin != true_end} noexcept -> meta::nothrow::truthy;
                {(*self->func)(*true_begin)} noexcept -> meta::nothrow::truthy;
                {++true_begin} noexcept;
            })
            requires (requires{
                {true_begin != true_end} -> meta::truthy;
                {(*self->func)(*true_begin)} -> meta::truthy;
                {++true_begin};
            })
        {
            while (true_begin != true_end) {
                if ((*self->func)(*true_begin)) {
                    break;
                }
                ++true_begin;
                ++index;
            }
        }

        [[nodiscard]] constexpr decltype(auto) operator*() const
            noexcept (requires{{*true_begin} noexcept;})
            requires (requires{{*true_begin};})
        {
            return (*true_begin);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow{*true_begin}} noexcept;})
            requires (requires{{impl::arrow{*true_begin}};})
        {
            return impl::arrow{*true_begin};
        }

        constexpr where_iterator& operator++()
            noexcept (requires{
                {++true_begin} noexcept;
                {true_begin != true_end} noexcept -> meta::nothrow::truthy;
                {(*self->func)(*true_begin)} noexcept -> meta::nothrow::truthy;
            })
            requires (requires{
                {++true_begin};
                {true_begin != true_end} -> meta::truthy;
                {(*self->func)(*true_begin)} -> meta::truthy;
            })
        {
            ++true_begin;
            ++index;
            while (true_begin != true_end) {
                if ((*self->func)(*true_begin)) {
                    break;
                }
                ++true_begin;
                ++index;
            }
            return *this;
        }

        [[nodiscard]] constexpr where_iterator operator++(int)
            noexcept (requires{
                {where_iterator{*this}} noexcept;
                {++*this} noexcept;
            })
            requires (requires{
                {where_iterator{*this}};
                {++*this};
            })
        {
            where_iterator temp = *this;
            ++*this;
            return temp;
        }

        constexpr where_iterator& operator--()
            noexcept (requires{
                {--true_begin} noexcept;
                {(*self->func)(*true_begin)} noexcept -> meta::nothrow::truthy;
            })
            requires (requires{
                {--true_begin};
                {(*self->func)(*true_begin)} -> meta::truthy;
            })
        {
            --true_begin;
            --index;
            while (index >= 0) {
                if ((*self->func)(*true_begin)) {
                    break;
                }
                --true_begin;
                --index;
            }
            return *this;
        }

        [[nodiscard]] constexpr where_iterator operator--(int)
            noexcept (requires{
                {where_iterator{*this}} noexcept;
                {--*this} noexcept;
            })
            requires (requires{
                {where_iterator{*this}};
                {--*this};
            })
        {
            where_iterator temp = *this;
            --*this;
            return temp;
        }

        [[nodiscard]] constexpr bool operator==(const where_iterator& other) const noexcept {
            return index == other.index;
        }

        [[nodiscard]] constexpr auto operator<=>(const where_iterator& other) const noexcept {
            return index <=> other.index;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const where_iterator& self,
            NoneType
        )
            noexcept (requires{
                {self.true_begin == self.true_end} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {self.true_begin == self.true_end} -> meta::convertible_to<bool>;
            })
        {
            return self.true_begin == self.true_end;
        }

        [[nodiscard]] friend constexpr bool operator==(
            NoneType,
            const where_iterator& self
        )
            noexcept (requires{
                {self.true_begin == self.true_end} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {self.true_begin == self.true_end} -> meta::convertible_to<bool>;
            })
        {
            return self.true_begin == self.true_end;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const where_iterator& self,
            NoneType
        )
            noexcept (requires{
                {self.true_begin != self.true_end} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {self.true_begin != self.true_end} -> meta::convertible_to<bool>;
            })
        {
            return self.true_begin != self.true_end;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            NoneType,
            const where_iterator& self
        )
            noexcept (requires{
                {self.true_begin != self.true_end} noexcept -> meta::nothrow::convertible_to<bool>;
            })
            requires (requires{
                {self.true_begin != self.true_end} -> meta::convertible_to<bool>;
            })
        {
            return self.true_begin != self.true_end;
        }
    };

    /* Binary `where{}` expressions correspond to a filter that omits values where the
    predicate evaluates to `false`.  Because this cannot be known generically ahead of
    time, the resulting range is unsized, not a tuple, and not reliably indexable. */
    template <typename F, typename C>
    struct where<F, C, void> {
        using function_type = F;
        using true_type = meta::as_range<C>;
        using false_type = void;

        [[no_unique_address]] impl::ref<F> func;
        [[no_unique_address]] impl::ref<true_type> if_true;

        [[nodiscard]] constexpr where(meta::forward<F> f, meta::forward<C> t)
            noexcept (requires{
                {impl::ref<F>{std::forward<F>(f)}} noexcept;
                {impl::ref<true_type>{std::forward<C>(t)}} noexcept;
            })
            requires (requires{
                {impl::ref<F>{std::forward<F>(f)}};
                {impl::ref<true_type>{std::forward<C>(t)}};
            })
        :
            func{std::forward<F>(f)},
            if_true{std::forward<C>(t)}
        {}

        [[nodiscard]] constexpr auto begin()
            noexcept (requires(where_iterator<
                where,
                meta::begin_type<meta::as_lvalue<true_type>>,
                meta::end_type<meta::as_lvalue<true_type>>
            > result) {
                {where_iterator<
                    where,
                    meta::begin_type<meta::as_lvalue<true_type>>,
                    meta::end_type<meta::as_lvalue<true_type>>
                >{
                    .self = this,
                    .index = 0,
                    .true_begin = if_true->begin(),
                    .true_end = if_true->end()
                }} noexcept;
                {result.init()};
            })
            requires (requires(where_iterator<
                where,
                meta::begin_type<meta::as_lvalue<true_type>>,
                meta::end_type<meta::as_lvalue<true_type>>
            > result) {
                {where_iterator<
                    where,
                    meta::begin_type<meta::as_lvalue<true_type>>,
                    meta::end_type<meta::as_lvalue<true_type>>
                >{
                    .self = this,
                    .index = 0,
                    .true_begin = if_true->begin(),
                    .true_end = if_true->end()
                }};
                {result.init()};
            })
        {
            where_iterator<
                where,
                meta::begin_type<meta::as_lvalue<true_type>>,
                meta::end_type<meta::as_lvalue<true_type>>
            > result {
                .self = this,
                .index = 0,
                .true_begin = if_true->begin(),
                .true_end = if_true->end()
            };
            result.init();
            return result;
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires(where_iterator<
                const where,
                meta::begin_type<meta::as_const_ref<true_type>>,
                meta::end_type<meta::as_const_ref<true_type>>
            > result) {
                {where_iterator<
                    const where,
                    meta::begin_type<meta::as_const_ref<true_type>>,
                    meta::end_type<meta::as_const_ref<true_type>>
                >{
                    .self = this,
                    .index = 0,
                    .true_begin = if_true->begin(),
                    .true_end = if_true->end()
                }} noexcept;
                {result.init()};
            })
            requires (requires(where_iterator<
                const where,
                meta::begin_type<meta::as_const_ref<true_type>>,
                meta::end_type<meta::as_const_ref<true_type>>
            > result) {
                {where_iterator<
                    const where,
                    meta::begin_type<meta::as_const_ref<true_type>>,
                    meta::end_type<meta::as_const_ref<true_type>>
                >{
                    .self = this,
                    .index = 0,
                    .true_begin = if_true->begin(),
                    .true_end = if_true->end()
                }};
                {result.init()};
            })
        {
            where_iterator<
                const where,
                meta::begin_type<meta::as_const_ref<true_type>>,
                meta::end_type<meta::as_const_ref<true_type>>
            > result {
                .self = this,
                .index = 0,
                .true_begin = if_true->begin(),
                .true_end = if_true->end()
            };
            result.init();
            return result;
        }

        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }
        
        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires(where_iterator<
                where,
                meta::rbegin_type<meta::as_lvalue<true_type>>,
                meta::rend_type<meta::as_lvalue<true_type>>
            > result) {
                {where_iterator<
                    where,
                    meta::rbegin_type<meta::as_lvalue<true_type>>,
                    meta::rend_type<meta::as_lvalue<true_type>>
                >{
                    .self = this,
                    .index = 0,
                    .true_begin = if_true->rbegin(),
                    .true_end = if_true->rend()
                }} noexcept;
                {result.init()};
            })
            requires (requires(where_iterator<
                where,
                meta::rbegin_type<meta::as_lvalue<true_type>>,
                meta::rend_type<meta::as_lvalue<true_type>>
            > result) {
                {where_iterator<
                    where,
                    meta::rbegin_type<meta::as_lvalue<true_type>>,
                    meta::rend_type<meta::as_lvalue<true_type>>
                >{
                    .self = this,
                    .index = 0,
                    .true_begin = if_true->rbegin(),
                    .true_end = if_true->rend()
                }};
                {result.init()};
            })
        {
            where_iterator<
                where,
                meta::rbegin_type<meta::as_lvalue<true_type>>,
                meta::rend_type<meta::as_lvalue<true_type>>
            > result {
                .self = this,
                .index = 0,
                .true_begin = if_true->rbegin(),
                .true_end = if_true->rend()
            };
            result.init();
            return result;
        }

        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires(where_iterator<
                const where,
                meta::rbegin_type<meta::as_const_ref<true_type>>,
                meta::rend_type<meta::as_const_ref<true_type>>
            > result) {
                {where_iterator<
                    const where,
                    meta::rbegin_type<meta::as_const_ref<true_type>>,
                    meta::rend_type<meta::as_const_ref<true_type>>
                >{
                    .self = this,
                    .index = 0,
                    .true_begin = if_true->rbegin(),
                    .true_end = if_true->rend()
                }} noexcept;
                {result.init()};
            })
            requires (requires(where_iterator<
                const where,
                meta::rbegin_type<meta::as_const_ref<true_type>>,
                meta::rend_type<meta::as_const_ref<true_type>>
            > result) {
                {where_iterator<
                    const where,
                    meta::rbegin_type<meta::as_const_ref<true_type>>,
                    meta::rend_type<meta::as_const_ref<true_type>>
                >{
                    .self = this,
                    .index = 0,
                    .true_begin = if_true->rbegin(),
                    .true_end = if_true->rend()
                }};
                {result.init()};
            })
        {
            where_iterator<
                const where,
                meta::rbegin_type<meta::as_const_ref<true_type>>,
                meta::rend_type<meta::as_const_ref<true_type>>
            > result {
                .self = this,
                .index = 0,
                .true_begin = if_true->rbegin(),
                .true_end = if_true->rend()
            };
            result.init();
            return result;
        }

        [[nodiscard]] static constexpr NoneType rend() noexcept { return {}; }
    };

}


namespace iter {

    template <meta::not_rvalue F>
    struct where {
        [[no_unique_address]] F func;

        /// TODO: these call operators should be constrained to make sure that the
        /// function is callable with the appropriate arguments, for better diagnostics.

        template <typename Self, typename T>
        [[nodiscard]] constexpr auto operator()(this Self&& self, T&& r)
            noexcept (requires{{range<impl::where<F, meta::remove_rvalue<T>>>{
                std::forward<Self>(self).func,
                std::forward<T>(r)
            }} noexcept;})
            requires (requires{{range<impl::where<F, meta::remove_rvalue<T>>>{
                std::forward<Self>(self).func,
                std::forward<T>(r)
            }};})
        {
            return range<impl::where<F, meta::remove_rvalue<T>>>{
                std::forward<Self>(self).func,
                std::forward<T>(r)
            };
        }

        template <typename Self, typename L, typename R>
        [[nodiscard]] constexpr auto operator()(this Self&& self, L&& t, R&& f)
            noexcept (requires{{range<impl::where<
                F,
                meta::remove_rvalue<L>,
                meta::remove_rvalue<R>
            >>{
                std::forward<Self>(self).func,
                std::forward<L>(t),
                std::forward<R>(f)
            }} noexcept;})
            requires (requires{{range<impl::where<
                F,
                meta::remove_rvalue<L>,
                meta::remove_rvalue<R>
            >>{
                std::forward<Self>(self).func,
                std::forward<L>(t),
                std::forward<R>(f)
            }};})
        {
            return range<impl::where<F, meta::remove_rvalue<L>, meta::remove_rvalue<R>>>{
                std::forward<Self>(self).func,
                std::forward<L>(t),
                std::forward<R>(f)
            };
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


    static constexpr auto w =
        where{[](int x) { return x > 1; }}(std::array{1, 2, 3});

    static_assert([] {
        auto it = w->rbegin();

        for (auto&& x : w) {
            if (x != 1 && x != 2 && x != 3) {
                return false;
            }
        }

        return true;
    }());


}


#endif  // BERTRAND_ITER_WHERE_H