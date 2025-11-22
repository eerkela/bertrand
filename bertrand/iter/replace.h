#ifndef BERTRAND_ITER_REPLACE_H
#define BERTRAND_ITER_REPLACE_H

#include "bertrand/iter/range.h"


namespace bertrand {


namespace impl {

    /// TODO: I probably also need a replace_traits<Self> struct that deduces the
    /// proper types for the yield, subscript, front, and back types, similar to
    /// concat_traits



    /* Scalar or empty replacements are trivial, and only add overhead to the
    dereference or increment/decrement operators, respectively. */
    template <meta::not_reference Self, meta::not_rvalue Iter>
    struct replace_iterator {
        using iterator_category = std::conditional_t<
            meta::inherits<meta::iterator_category<Iter>, std::contiguous_iterator_tag>,
            std::random_access_iterator_tag,
            meta::iterator_category<Iter>
        >;
        using difference_type = meta::iterator_difference<Iter>;
        using reference = std::conditional_t<
            Self::empty_Value,
            meta::dereference_type<meta::as_const_ref<Iter>>,
            meta::make_union<
                meta::dereference_type<meta::as_const_ref<Iter>>,
                meta::yield_type<
                    typename Self::value_type
                >
            >
        >;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<value_type>;

        [[no_unique_address]] Self* self = nullptr;
        [[no_unique_address]] ssize_t index = 0;
        [[no_unique_address]] Iter iter;

    private:
        /// TODO: deduce a union return type for deref() as long as the value is not
        /// empty, and then replace all the convertible_to<T> checks below with that
        /// type.

        template <typename T> requires (!Self::empty_value && Self::predicate)
        constexpr decltype(auto) deref(T&& value) const
            noexcept (requires{
                {(*self->key)(value)} noexcept -> meta::nothrow::truthy;
                {(*self->value).template get<0>()} noexcept -> meta::nothrow::convertible_to<void>;
                {std::forward<T>(value)} noexcept -> meta::nothrow::convertible_to<void>;
            })
            requires (requires{
                {(*self->key)(value)} -> meta::truthy;
                {(*self->value).template get<0>()} -> meta::convertible_to<void>;
                {std::forward<T>(value)} -> meta::convertible_to<void>;
            })
        {
            if ((*self->key)(value)) {
                return (*self->value).template get<0>();
            }
            return std::forward<T>(value);
        }

        template <typename T> requires (!Self::empty_value && !Self::predicate)
        constexpr decltype(auto) deref(T&& value) const
            noexcept (requires{
                {*self->key == value} noexcept -> meta::nothrow::truthy;
                {(*self->value).template get<0>()} noexcept -> meta::nothrow::convertible_to<void>;
                {std::forward<T>(value)} noexcept -> meta::nothrow::convertible_to<void>;
            })
            requires (requires{
                {*self->key == value} -> meta::truthy;
                {(*self->value).template get<0>()} -> meta::convertible_to<void>;
                {std::forward<T>(value)} -> meta::convertible_to<void>;
            })
        {
            /// TODO: make sure the operand order always matches for these kinds of
            /// checks
            if (*self->key == value) {
                return (*self->value).template get<0>();
            }
            return std::forward<T>(value);
        }

    public:
        [[nodiscard]] constexpr decltype(auto) operator*() const
            noexcept (Self::empty_value ?
                requires{{*iter} noexcept -> meta::nothrow::convertible_to<void>;} :
                requires{{deref(*iter)} noexcept;}
            )
            requires (!Self::empty_value || requires{{deref(*iter)};})
        {
            if constexpr (Self::empty_value) {
                return *iter;
            } else {
                return deref(*iter);
            }
        }

        constexpr replace_iterator& operator++()
            noexcept (requires{{++iter} noexcept;})
            requires (requires{{++iter};})
        {
            ++iter;
            ++index;
            return *this;
        }

        [[nodiscard]] constexpr replace_iterator operator++(int)
            noexcept (requires{{++iter} noexcept;})
            requires (requires{{++iter};})
        {
            replace_iterator tmp = *this;
            ++*this;
            return tmp;
        }

        constexpr replace_iterator& operator+=()
    };

    /* Matching against a key range requires a linear search in the increment/decrement
    operators, and additional bookkeeping to account for failed matches. */
    template <meta::not_reference Self, meta::not_rvalue Iter>
        requires (Self::multi_key && !Self::multi_value)
    struct replace_iterator<Self, Iter> {
        [[no_unique_address]] Self* self = nullptr;
        [[no_unique_address]] ssize_t index = 0;
        [[no_unique_address]] Iter iter;

    };

    /* Replacing with a value range requires a nested iterator, which the outer
    increment/decrement operators may delegate to. */
    template <meta::not_reference Self, meta::not_rvalue Iter>
        requires (!Self::multi_key && Self::multi_value)
    struct replace_iterator<Self, Iter> {
        [[no_unique_address]] Self* self = nullptr;
        [[no_unique_address]] ssize_t index = 0;
        [[no_unique_address]] Iter iter;

    };

    /* If both a key range and value range are given, then both of the above effects
    must be combined. */
    template <meta::not_reference Self, meta::not_rvalue Iter>
        requires (Self::multi_key && Self::multi_value)
    struct replace_iterator<Self, Iter> {
        [[no_unique_address]] Self* self = nullptr;
        [[no_unique_address]] ssize_t index = 0;
        [[no_unique_address]] Iter iter;

    };


    /// TODO: maybe specialize on Self::multi_value, since that would probably need to
    /// store another iterator to keep track of resizing.  No need to specialize for
    /// multi_key, since that can be handled in the increment operator alone, whereby
    /// it would attempt to greedily match as many keys as possible at each position,
    /// and then swap over to the vaLue when a match is found.

    /// TODO: maybe the inner value range is stored as an optional?  The increment
    /// operator would check if it is engaged, and if so, increment that first before
    /// proceeding to the outer iterator.  This makes sense, but is expensive, and can
    /// be avoided if the key or value is known to be of length 0 or 1, which is a
    /// worthwhile optimization.  In every other case, I may need to copy the outer
    /// iterator every time I want to test for a match, since otherwise I may skip over
    /// elements if a match is not found, but one or more elements were already
    /// consumed.

    /// TODO: also, random access and bidirectional iterators over mulitiple values
    /// would be tricky, since they would need to have some memory of where the
    /// replacements were made.  What I could do is restrict it to bidirectional
    /// key ranges, in which case the decrement operator would attempt to match by
    /// reverse iterating over the key range, and basically inverting the logic.





    /// TODO: perhaps what I should do to simplify the implementation is break the
    /// key and value members off into separate structs, which account for matching and
    /// replacement separately, instead of combining them using separate
    /// specializations




    /// TODO: empty_key will never be true.  If it is, then the outer call operator
    /// will just forward the argument as a normal range.

    template <meta::not_rvalue C, meta::not_rvalue K, meta::not_rvalue V = void>
    struct replace {
        using container_type = meta::as_range<C>;
        using key_type = meta::as_range_or_scalar<K>;
        using value_type = meta::as_range_or_scalar<V>;
        static constexpr bool predicate = false;
        static constexpr bool multi_key = !meta::structured<key_type, 1>;
        static constexpr bool empty_value = meta::structured<value_type, 0>;
        static constexpr bool multi_value = !empty_value && !meta::structured<value_type, 1>;

        [[no_unique_address]] impl::ref<container_type> container;
        [[no_unique_address]] impl::ref<key_type> key;
        [[no_unique_address]] impl::ref<value_type> value;
    };

    template <meta::not_rvalue C, meta::not_rvalue F>
    struct replace<C, F, void> {
        using container_type = meta::as_range<C>;
        using key_type = meta::as_range_or_scalar<F>;

        /// TODO: value_type should be deduced from the return type of calling F on the
        /// yield type of C.
        using value_type = void;
        static constexpr bool predicate = true;
        static constexpr bool multi_key = !meta::structured<key_type, 1>;

        /// TODO: should the value apply the same empty/multi logic as above wrt the return type?
        static constexpr bool empty_value = false;
        static constexpr bool multi_value = meta::range<meta::call_type<
            meta::yield_type<meta::as_range_or_scalar<F>>,
            meta::yield_type<meta::as_range<C>>
        >>;

        [[no_unique_address]] impl::ref<container_type> container;
        [[no_unique_address]] impl::ref<key_type> key;
    };

}


namespace iter {

    template <meta::not_rvalue K, meta::not_rvalue V = void> requires (!meta::empty_range<K>)
    struct replace {
        [[no_unique_address]] K key;
        [[no_unique_address]] V value;

        template <typename Self, typename C>
        [[nodiscard]] constexpr auto operator()(this Self&& self, C&& r)
            noexcept (requires{{range<impl::replace<meta::remove_rvalue<C>, K, V>>{
                std::forward<C>(r),
                std::forward<Self>(self).key,
                std::forward<Self>(self).value
            }} noexcept;})
            requires (meta::has_eq<
                meta::yield_type<meta::as_range_or_scalar<K>>,
                meta::yield_type<meta::as_range<C>>
            > && requires{{range<impl::replace<meta::remove_rvalue<C>, K, V>>{
                std::forward<C>(r),
                std::forward<Self>(self).key,
                std::forward<Self>(self).value
            }};})
        {
            return range<impl::replace<meta::remove_rvalue<C>, K, V>>{
                std::forward<C>(r),
                std::forward<Self>(self).key,
                std::forward<Self>(self).value
            };
        }
    };
    template <meta::not_rvalue F> requires (!meta::empty_range<F>)
    struct replace<F, void> {
        [[no_unique_address]] F func;

        template <typename Self, typename C>
        [[nodiscard]] constexpr auto operator()(this Self&& self, C&& r)
            noexcept (requires{{range<impl::replace<meta::remove_rvalue<C>, F>>{
                std::forward<C>(r),
                std::forward<Self>(self).func
            }} noexcept;})
            requires (meta::callable<
                meta::yield_type<meta::as_range_or_scalar<F>>,
                meta::yield_type<meta::as_range<C>>
            > && requires{{range<impl::replace<meta::remove_rvalue<C>, F>>{
                std::forward<C>(r),
                std::forward<Self>(self).func
            }};})
        {
            return range<impl::replace<meta::remove_rvalue<C>, F>>{
                std::forward<C>(r),
                std::forward<Self>(self).func
            };
        }
    };

    template <typename F>
    replace(F&&) -> replace<meta::remove_rvalue<F>>;

    template <typename K, typename V>
    replace(K&&, V&&) -> replace<meta::remove_rvalue<K>, meta::remove_rvalue<V>>;

}


}


_LIBCPP_BEGIN_NAMESPACE_STD

    namespace ranges {



    }



_LIBCPP_END_NAMESPACE_STD


#endif  // BERTRAND_ITER_REPLACE_H