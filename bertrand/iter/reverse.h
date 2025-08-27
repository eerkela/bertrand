#ifndef BERTRAND_ITER_REVERSE_H
#define BERTRAND_ITER_REVERSE_H

#include "bertrand/common.h"
#include "bertrand/iter/range.h"


namespace bertrand {


/// TODO: swap() operators for all ranges and all public range adaptors.


/// TODO: if the adapted container for reverse() is a sequence, then I may need to
/// emit has_size() and a size() method that can possibly throw.


namespace impl {

    /* An adaptor for a container that causes `range<impl::reversed<C>>` to reverse
    iterate over the container `C` instead of forward iterating.  This equates to
    swapping all of the `begin()` and `end()` methods with their reversed counterparts,
    and modifying the indexing logic to map index `i` to index `-i - 1`, which
    triggers Python-style wraparound. */
    template <meta::not_rvalue C> requires (meta::reverse_iterable<C> || meta::tuple_like<C>)
    struct reverse {
        using type = C;
        using size_type = size_t;
        using index_type = ssize_t;

    private:
        [[no_unique_address]] impl::ref<type> m_range;

    public:
        [[nodiscard]] constexpr reverse(meta::forward<type> range)
            noexcept (requires{{impl::ref<type>(std::forward<type>(range))} noexcept;})
            requires (requires{{impl::ref<type>(std::forward<type>(range))};})
        :
            m_range(std::forward<type>(range))
        {}

        /* Perfectly forward the underlying container according to the reversed range's
        current cvref qualifications. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_range);
        }

        /* Swap the underlying containers between two reversed ranges. */
        constexpr void swap(reverse& other)
            noexcept (requires{{m_range.swap(other.m_range)} noexcept;})
            requires (requires{{m_range.swap(other.m_range)};})
        {
            m_range.swap(other.m_range);
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

        /* The total number of elements in the reversed range, as an unsigned
        integer. */
        [[nodiscard]] constexpr size_type size() const
            noexcept (meta::nothrow::size_returns<size_type, meta::as_const_ref<type>> || (
                !meta::size_returns<size_type, meta::as_const_ref<type>> &&
                meta::tuple_like<type>
            ))
            requires (
                meta::size_returns<size_type, meta::as_const_ref<type>> ||
                meta::tuple_like<type>
            )
        {
            if constexpr (meta::size_returns<size_type, meta::as_const_ref<type>>) {
                return std::ranges::size(value());
            } else {
                return meta::tuple_size<type>;
            }
        }

        /* The total number of elements in the reversed range, as a signed integer. */
        [[nodiscard]] constexpr index_type ssize() const
            noexcept (meta::nothrow::ssize_returns<index_type, meta::as_const_ref<type>> || (
                !meta::ssize_returns<index_type, meta::as_const_ref<type>> &&
                meta::tuple_like<type>
            ))
            requires (
                meta::ssize_returns<index_type, meta::as_const_ref<type>> ||
                meta::tuple_like<type>
            )
        {
            if constexpr (meta::ssize_returns<index_type, meta::as_const_ref<type>>) {
                return std::ranges::ssize(value());
            } else {
                return meta::to_signed(meta::tuple_size<type>);
            }
        }

        /* True if the reversed range contains zero elements.  False otherwise. */
        [[nodiscard]] constexpr bool empty() const
            noexcept (meta::nothrow::has_empty<meta::as_const_ref<type>> || (
                !meta::has_empty<meta::as_const_ref<type>> && meta::tuple_like<type>
            ))
            requires (meta::has_empty<meta::as_const_ref<type>> || meta::tuple_like<type>)
        {
            if constexpr (meta::has_empty<meta::as_const_ref<type>>) {
                return std::ranges::empty(value());
            } else {
                return meta::tuple_size<type> == 0;
            }
        }

        /* Allow tuple-like access and destructuring of the reversed range, with
        Python-style wraparound for negative indices.  This is identical to accessing
        the underlying container, but maps index `I` to `-I - 1` before applying
        wraparound. */
        template <index_type I, typename Self>
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{
                {meta::unpack_tuple<-I - 1>(std::forward<Self>(self).value())} noexcept;
            })
            requires (requires{
                {meta::unpack_tuple<-I - 1>(std::forward<Self>(self).value())};
            })
        {
            return (meta::unpack_tuple<-I - 1>(std::forward<Self>(self).value()));
        }

        /* Index operator for accessing elements in the reversed range, with
        Python-style wraparound for negative indices.  This is identical to indexing
        the underlying container, but maps index `i` to `-i - 1` before applying
        wraparound.  The actual index will always be forwarded as an unsigned
        `size_type` integer to maintain compatibility with as many container types as
        possible. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, index_type i)
            noexcept (requires{{std::forward<Self>(self).value()[
                size_type(impl::normalize_index(self.ssize(), -i - 1))]
            } noexcept;})
            requires (requires{{std::forward<Self>(self).value()[
                size_type(impl::normalize_index(self.ssize(), -i - 1))]
            };})
        {
            return (std::forward<Self>(self).value()[
                size_type(impl::normalize_index(self.ssize(), -i - 1))
            ]);
        }

        /* The reversed range maps `begin()` to the underlying container's `rbegin()`
        method. */
        [[nodiscard]] constexpr decltype(auto) begin()
            noexcept (requires{{impl::make_range_reversed{value()}.begin()} noexcept;})
            requires (requires{{impl::make_range_reversed{value()}.begin()};})
        {
            return (impl::make_range_reversed{value()}.begin());
        }

        /* The reversed range maps `begin()` to the underlying container's `rbegin()`
        method. */
        [[nodiscard]] constexpr decltype(auto) begin() const
            noexcept (requires{{impl::make_range_reversed{value()}.begin()} noexcept;})
            requires (requires{{impl::make_range_reversed{value()}.begin()};})
        {
            return (impl::make_range_reversed{value()}.begin());
        }

        /* The reversed range maps `end()` to the underlying container's `rend()`
        method. */
        [[nodiscard]] constexpr decltype(auto) end()
            noexcept (requires{{impl::make_range_reversed{value()}.end()} noexcept;})
            requires (requires{{impl::make_range_reversed{value()}.end()};})
        {
            return (impl::make_range_reversed{value()}.end());
        }

        /* The reversed range maps `end()` to the underlying container's `rend()`
        method. */
        [[nodiscard]] constexpr decltype(auto) end() const
            noexcept (requires{{impl::make_range_reversed{value()}.end()} noexcept;})
            requires (requires{{impl::make_range_reversed{value()}.end()};})
        {
            return (impl::make_range_reversed{value()}.end());
        }

        /* The reversed range maps `rbegin()` to the underlying container's `begin()`
        method. */
        [[nodiscard]] constexpr decltype(auto) rbegin()
            noexcept (requires{{impl::make_range_iterator{value()}.begin()} noexcept;})
            requires (requires{{impl::make_range_iterator{value()}.begin()};})
        {
            return (impl::make_range_iterator{value()}.begin());
        }

        /* The reversed range maps `rbegin()` to the underlying container's `begin()`
        method. */
        [[nodiscard]] constexpr decltype(auto) rbegin() const
            noexcept (requires{{impl::make_range_iterator{value()}.begin()} noexcept;})
            requires (requires{{impl::make_range_iterator{value()}.begin()};})
        {
            return (impl::make_range_iterator{value()}.begin());
        }

        /* The reversed range maps `rend()` to the underlying container's `end()`
        method. */
        [[nodiscard]] constexpr decltype(auto) rend()
            noexcept (requires{{impl::make_range_iterator{value()}.end()} noexcept;})
            requires  (requires{{impl::make_range_iterator{value()}.end()};})
        {
            return (impl::make_range_iterator{value()}.end());
        }

        /* The reversed range maps `rend()` to the underlying container's `end()`
        method. */
        [[nodiscard]] constexpr decltype(auto) rend() const
            noexcept (requires{{impl::make_range_iterator{value()}.end()} noexcept;})
            requires (requires{{impl::make_range_iterator{value()}.end()};})
        {
            return (impl::make_range_iterator{value()}.end());
        }
    };

}


namespace iter {

    /* A function object that reverses the order of iteration for a supported container.

    The ranges that are produced by this object act just like normal ranges, but with the
    forward and reverse iterators swapped, and the indexing logic modified to map index
    `i` to index `-i - 1` before applying Python-style wraparound.

    Note that the `reverse` class must be default-constructed, which standardizes it with
    respect to other range adaptors and allows it to be easily chained together with other
    operations to form more complex range-based algorithms. */
    struct reverse {
    private:
        template <typename C>
        using container = impl::reverse<meta::remove_rvalue<C>>;

        template <typename C>
        using range = iter::range<container<C>>;

    public:
        template <typename C> requires (meta::reverse_iterable<C> || meta::tuple_like<C>)
        [[nodiscard]] static constexpr range<C> operator()(C&& c)
            noexcept (requires{{range<C>{container<C>{std::forward<C>(c)}}} noexcept;})
            requires (requires{{range<C>{container<C>{std::forward<C>(c)}}};})
        {
            return range<C>{container<C>{std::forward<C>(c)}};
        }
    };

}


}


#endif  // BERTRAND_ITER_REVERSE_H