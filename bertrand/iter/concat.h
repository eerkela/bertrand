#ifndef BERTRAND_ITER_CONCAT_H
#define BERTRAND_ITER_CONCAT_H

#include "bertrand/iter/range.h"


namespace bertrand {


namespace impl {

    template <size_t I, typename T> requires (I < T::alternatives)
    constexpr size_t _concat_get = (!T::trivial && I % 2 == 1) ?
        0 :
        impl::visitable<const typename T::unique&>::alternatives::template index<
            const typename T::template type<I>&
        >();

    /* Cast the internal union of a `concat_iterator` to the alternative encoded at
    index `I`.  Separators are encoded as odd indices as long as they are not trivial,
    and the underlying union may be collapsed to just the unique alternatives as an
    optimization, which complicates decoding. */
    template <size_t I, typename T> requires (I < T::alternatives)
    constexpr decltype(auto) concat_get(T& self)
        noexcept (I < T::unique_alternatives ?
            requires{{impl::visitable<decltype((self.child))>::template get<_concat_get<I, T>>(
                self.child
            )} noexcept;} :
            requires{{impl::visitable<decltype((self.child))>::template get<_concat_get<I, T>>(
                self.child
            )};}
        )
    {
        return (impl::visitable<decltype((self.child))>::template get<_concat_get<I, T>>(
            self.child
        ));
    }

    /* Dereferencing a concat iterator does not depend on the exact index, just the
    unique alternative. */
    template <typename T>
    struct concat_deref {
        template <size_t I>
        struct fn {
            template <typename U>
            static constexpr T operator()(const U& u)
                noexcept (requires{{
                    *impl::visitable<const U&>::template get<I>(u).begin
                } noexcept -> meta::nothrow::convertible_to<T>;})
            {
                return *impl::visitable<const U&>::template get<I>(u).begin;
            }
        };
    };

    /* Comparing two concat iterators first compares their indices, and only invokes
    the vtable function if they are both in-bounds and happen to match, meaning that it
    can get away with using unique alternatives, and forcing the argument types to
    match exactly.  Doing so reduces dispatch overhead and binary size. */
    template <size_t I>
    struct concat_compare {
        template <typename U>
        static constexpr std::strong_ordering operator()(const U& lhs, const U& rhs)
            noexcept (requires{{
                impl::visitable<const U&>::template get<I>(lhs).index <=>
                impl::visitable<const U&>::template get<I>(rhs).index
            } noexcept;})
        {
            return
                impl::visitable<const U&>::template get<I>(lhs).index <=>
                impl::visitable<const U&>::template get<I>(rhs).index;
        }
    };

    /* Incrementing a concat iterator will attempt to increment the current subrange
    first, and then advance to either the next argument or separator if it is
    exhausted, depending on the current index encoding.  Empty subranges will be
    skipped, possibly requiring recursive calls for future indices (bypassing
    separators).

    Random-access increments are optimized to skip over intermediate subranges where
    possible, but still require linear time in the number of arguments, albeit with a
    low constant factor due to precomputed separator sizes and random-access distances
    between begin iterators and their sentinels. */
    template <size_t I>
    struct concat_increment {
        template <bool trivial>
        static constexpr size_t next = trivial ? I + 1 : (I | 1) + 1;

        template <typename T>
        static constexpr void skip(T& self)
            noexcept (
                next<T::trivial> >= T::alternatives ||
                requires(decltype(self.template get<next<T::trivial>>()) sub) {
                    {sub.begin != sub.end} noexcept -> meta::nothrow::truthy;
                    {self.child = self.template get<next<T::trivial>>()} noexcept;
                    {concat_increment<next<T::trivial>>::skip(self)} noexcept;
                }
            )
        {
            if constexpr (next<T::trivial> < T::alternatives) {
                self.index = next<T::trivial>;
                if (auto sub = self.template get<next<T::trivial>>(); sub.begin != sub.end) {
                    self.child = std::move(sub);
                    return;
                }
                concat_increment<next<T::trivial>>::skip(self);
            } else {
                /// NOTE: if we reach this point, then the last alternative is
                /// guaranteed to be empty
                self.index = T::alternatives;
                self.child = self.template get<T::alternatives - 1>();
            }
        }

        template <typename T>
        static constexpr void operator()(T& self)
            noexcept (requires(decltype((concat_get<I>(self))) curr) {
                {++curr.begin} noexcept;
                {curr.begin != curr.end} noexcept -> meta::nothrow::truthy;
            } && (I + 1 >= T::alternatives || (requires{{skip(self)} noexcept;} && (
                T::trivial || I % 2 != 0 || requires{
                    {self.child = self.template get<I + 1>()} noexcept;
                }
            ))))
        {
            // inner increment
            auto& curr = concat_get<I>(self);
            ++curr.begin;
            ++curr.index;
            if (curr.begin != curr.end) {
                return;
            }

            // outer increment
            ++self.index;
            if constexpr (I + 1 < T::alternatives) {  // next alternative exists
                if constexpr (!T::trivial && I % 2 == 0) {  // next alternative is a separator
                    if constexpr (!T::dynamic) {  // size is known to be non-zero
                        self.child = self.template get<I + 1>();
                        return;
                    } else {  // size must be checked at run time
                        if (self.outer->sep_size() != 0) {
                            self.child = self.template get<I + 1>();
                            return;
                        }
                        ++self.index;
                    }
                }
                skip(self);
            }
        }

        /// NOTE: random access assumes positive `n`.  The caller is responsible for
        /// handling negative values by flipping their sign and delegating to
        /// `concat_decrement` instead.

        template <typename T>
        static constexpr void skip(T& self, ssize_t& n)
            noexcept (requires(decltype((
                self.template get<I + 1 < T::alternatives ? I + 1 : I>()
            )) curr) {
                {
                    self.child = self.template get<I + 1 < T::alternatives ? I + 1 : I>()
                } noexcept;
                {curr.end - curr.begin} noexcept -> meta::nothrow::convertible_to<ssize_t>;
                {curr.begin += n} noexcept;
            })
            requires (requires(decltype((
                self.template get<I + 1 < T::alternatives ? I + 1 : I>()
            )) curr) {
                {self.child = self.template get<I + 1 < T::alternatives ? I + 1 : I>()};
                {curr.end - curr.begin} -> meta::convertible_to<ssize_t>;
                {curr.begin += n};
            })
        {
            if constexpr (I + 1 < T::alternatives) {  // next alternative exists
                ++self.index;
                if constexpr (!T::trivial && I % 2 == 0) {  // next alternative is a separator
                    ssize_t size = self.sep_size();
                    if (n < size) {
                        auto curr = self.template get<I + 1>();
                        curr.begin += n;
                        curr.index += n;
                        self.child = std::move(curr);
                        return;
                    }
                    n -= size;
                } else {  // next alternative is an argument
                    auto curr = self.template get<I + 1>();
                    ssize_t size = curr.end - curr.begin;
                    if (n < size) {
                        curr.begin += n;
                        curr.index += n;
                        self.child = std::move(curr);
                        return;
                    }
                    n -= size;
                }
                concat_increment<I + 1>::skip(self, n);
            } else {  // no next alternative - initialize to an end iterator
                self.index = T::alternatives;
                auto curr = self.template get<I>();
                ssize_t size = curr.end - curr.begin;
                curr.begin += size;
                curr.index += size;
                self.child = std::move(curr);
            }
        }

        template <typename T>
        static constexpr void operator()(T& self, ssize_t& n)
            noexcept (requires(decltype((concat_get<I>(self))) curr) {
                {curr.end - curr.begin} noexcept -> meta::nothrow::convertible_to<ssize_t>;
                {curr.begin += n} noexcept;
                {skip(self, n)} noexcept;
            })
            requires (requires(decltype((concat_get<I>(self))) curr) {
                {curr.end - curr.begin} -> meta::convertible_to<ssize_t>;
                {curr.begin += n};
                {skip(self, n)};
            })
        {
            // inner increment
            auto& curr = concat_get<I>(self);
            ssize_t remaining = curr.end - curr.begin;
            if (n < remaining) {
                curr.begin += n;
                curr.index += n;
                return;
            }

            // outer increment
            n -= remaining;
            skip(self, n);
        }
    };

    /* Decrementing a concat iterator will attempt to decrement the current subrange
    first, and then retreat to either the last element of a previous argument or
    separator if it causes the index to decrement past zero, possibly requiring
    recursive calls for prior indices (bypassing separators).

    Random-access decrements are optimized to skip over intermediate subranges where
    possible, but still require linear time in the number of arguments, albeit with a
    low constant factor due to precomputed separator sizes and iterator indices.

    An extra vtable entry will be generated to handle decrementing an end iterator,
    whose subrange begin will always compare equal to its end.  Other than some extra
    indexing, everything else is handled identically to normal decrements. */
    template <size_t I>
    struct concat_decrement {
        template <bool trivial>
        static constexpr size_t prev = trivial ? I - 1 : (I - 1) & ~1;

        template <typename T, typename S>
        static constexpr void last(T& self, S& sub)
            noexcept (requires{{
                meta::ssize(self.template get<prev<T::trivial>>()) - 1
            } noexcept -> meta::nothrow::convertible_to<ssize_t>;} ?
                (requires(ssize_t size) {{sub.begin += size};} ?
                    requires(ssize_t size) {{sub.begin += size} noexcept;} :
                    requires{{++sub.begin} noexcept;}
                ) :
                requires{
                    {sub.begin} noexcept -> meta::nothrow::copyable;
                    {sub.begin != sub.end} noexcept -> meta::nothrow::truthy;
                    {++sub.begin} noexcept;
                }
            )
        {
            if constexpr (requires{{
                meta::ssize(self.template get<prev<T::trivial>>()) - 1
            } -> meta::convertible_to<ssize_t>;}) {
                ssize_t size = meta::ssize(self.template get<prev<T::trivial>>()) - 1;
                if constexpr (requires{{sub.begin += size};}) {
                    sub.begin += size;
                    sub.index += size;
                } else {
                    sub.index += size;
                    for (ssize_t i = 0; i < size; ++i) {
                        ++sub.begin;
                    }
                }
            } else {
                auto it = sub.begin;
                ++it;
                while (it != sub.end) {
                    ++it;
                    ++sub.begin;
                    ++sub.index;
                }
            }
        }

        template <typename T>
        static constexpr void skip(T& self)
            noexcept (
                I > 0 ||
                requires(decltype(self.template get<prev<T::trivial>>()) sub) {
                    {sub.begin != sub.end} noexcept -> meta::nothrow::truthy;
                    {last(self, sub)} noexcept;
                    {self.child = self.template get<prev<T::trivial>>()} noexcept;
                    {concat_decrement<prev<T::trivial>>::skip(self)} noexcept;
                }
            )
        {
            if constexpr (I > 0) {
                self.index = prev<T::trivial>;
                if (auto sub = self.template get<prev<T::trivial>>(); sub.begin != sub.end) {
                    last(self, sub);
                    self.child = std::move(sub);
                    return;
                }
                concat_decrement<prev<T::trivial>>::skip(self);
            } else {
                /// NOTE: if we reach this point, then the first alternative is
                /// guaranteed to be empty
                self.index = 0;
                self.child = self.template get<0>();
            }
        }

        template <typename T> requires (I < T::alternatives)
        static constexpr void operator()(T& self)
            noexcept (requires(decltype((concat_get<I>(self))) curr) {
                {--curr.begin} noexcept;
            } && (I <= 0 || (requires{{skip(self)} noexcept;} && (
                T::trivial || I % 2 != 0 || requires(decltype((self.template get<I - 1>())) sub) {
                    {self.child = self.template get<I - 1>()} noexcept;
                    {last(self, sub)} noexcept;
                }
            ))))
        {
            // inner decrement
            auto& curr = concat_get<I>(self);
            if (curr.index > 0) {
                --curr.begin;
                --curr.index;
                return;
            }

            // outer decrement
            if constexpr (I > 0) {  // previous alternative exists
                --self.index;
                if constexpr (!T::trivial && I % 2 == 0) {  // previous alternative is a separator
                    if constexpr (!T::dynamic) {  // size is known to be non-zero
                        auto sub = self.template get<I - 1>();
                        if (sub.begin == sub.end) throw TypeError();
                        last(self, sub);
                        self.child = std::move(sub);
                        return;
                    } else {  // size must be checked at run time
                        if (self.outer->sep_size() != 0) {
                            auto sub = self.template get<I - 1>();
                            last(self, sub);
                            self.child = std::move(sub);
                            return;
                        }
                        --self.index;
                    }
                }
                skip(self);
            }
        }

        template <typename T> requires (I == T::alternatives)
        static constexpr void operator()(T& self)
            noexcept (requires{{concat_decrement<I - 1>::operator()(self)} noexcept;})
        {
            --self.index;
            concat_decrement<I - 1>::operator()(self);
        }

        template <typename T>
        static constexpr void skip(T& self, ssize_t& n)
            noexcept (requires(decltype((self.template get<(I > 0 ? I - 1 : I)>())) curr) {
                {self.child = self.template get<(I > 0 ? I - 1 : I)>()} noexcept;
                {curr.end - curr.begin} noexcept -> meta::nothrow::convertible_to<ssize_t>;
                {curr.begin += n} noexcept;
            })
            requires (requires(decltype((self.template get<(I > 0 ? I - 1 : I)>())) curr) {
                {self.child = self.template get<(I > 0 ? I - 1 : I)>()};
                {curr.end - curr.begin} -> meta::convertible_to<ssize_t>;
                {curr.begin += n};
            })
        {
            if constexpr (I > 0) {  // previous alternative exists
                --self.index;
                if constexpr (!T::trivial && I % 2 == 0) {  // previous alternative is a separator
                    ssize_t size = self.sep_size();
                    if (n <= size) {
                        auto curr = self.template get<I - 1>();
                        size -= n;
                        curr.begin += size;
                        curr.index += size;
                        self.child = std::move(curr);
                        return;
                    }
                    n -= size;
                } else {
                    auto curr = self.template get<I - 1>();
                    ssize_t size = curr.end - curr.begin;
                    if (n <= size) {
                        size -= n;
                        curr.begin += size;
                        curr.index += size;
                        self.child = std::move(curr);
                        return;
                    }
                    n -= size;
                }
                concat_decrement<I - 1>::skip(self, n);
            } else {  // no previous alternative - initialize to a begin iterator
                self.index = 0;
                self.child = self.template get<I>();
            }
        }

        template <typename T> requires (I < T::alternatives)
        static constexpr void operator()(T& self, ssize_t& n)
            noexcept (requires(decltype((concat_get<I>(self))) curr) {
                {curr.begin -= n} noexcept;
                {skip(self, n)} noexcept;
            })
            requires (requires(decltype((concat_get<I>(self))) curr) {
                {curr.begin -= n};
                {skip(self, n)};
            })
        {
            // inner decrement
            auto& curr = concat_get<I>(self);
            if (n <= curr.index) {
                curr.begin -= n;
                curr.index -= n;
                return;
            }

            // outer decrement
            n -= curr.index;
            skip(self, n);
        }

        template <typename T> requires (I == T::alternatives)
        static constexpr void operator()(T& self, ssize_t& n)
            noexcept (requires{{concat_decrement<I - 1>::operator()(self, n)} noexcept;})
            requires (requires{{concat_decrement<I - 1>::operator()(self, n)};})
        {
            --self.index;
            concat_decrement<I - 1>::operator()(self, n);
        }
    };

    /* Computing the distance between two concat iterators requires a cartesian product
    encoding both indices.  If the indices are the same, then we can simply visit that
    alternative and take the difference between their internal indices.  Otherwise, we
    need to get the distance from the leftmost iterator to the end of its current
    subrange, sum over all intermediate subranges, and then add the distance from the
    beginning of the rightmost iterator's subrange to its current position. */
    template <size_t I>
    struct concat_distance {
        template <size_t alternatives>
        static constexpr size_t quotient = I / (alternatives + 1);
        template <size_t alternatives>
        static constexpr size_t remainder = I % (alternatives + 1);
        template <size_t alternatives>
        static constexpr size_t sentinel = (alternatives + 1) * (alternatives + 1) - 1;

        template <size_t J, typename T> requires (J < T::alternatives)
        static constexpr ssize_t left(const T& self)
            noexcept (requires{{
                concat_get<J>(self).end - concat_get<J>(self).begin
            } noexcept -> meta::nothrow::convertible_to<ssize_t>;})
            requires (requires{{
                concat_get<J>(self).end - concat_get<J>(self).begin
            } -> meta::convertible_to<ssize_t>;})
        {
            return concat_get<J>(self).end - concat_get<J>(self).begin;
        }

        template <typename T> requires (quotient<T::alternatives> < remainder<T::alternatives>)
        static constexpr ssize_t middle(const T& self)
            noexcept (requires{{
                self.sep_size() + concat_distance<I + T::alternatives + 1>::middle(self)
            } noexcept -> meta::nothrow::convertible_to<ssize_t>;})
            requires ((!T::trivial && quotient<T::alternatives> % 2 == 1) && requires{{
                self.sep_size() + concat_distance<I + T::alternatives + 1>::middle(self)
            } -> meta::convertible_to<ssize_t>;})
        {
            return self.sep_size() + concat_distance<I + T::alternatives + 1>::middle(self);
        }

        template <typename T> requires (quotient<T::alternatives> < remainder<T::alternatives>)
        static constexpr ssize_t middle(const T& self)
            noexcept (requires{{
                meta::distance(self.template arg<quotient<T::alternatives>>()) +
                concat_distance<I + T::alternatives + 1>::middle(self)
            } noexcept -> meta::nothrow::convertible_to<ssize_t>;})
            requires ((T::trivial || quotient<T::alternatives> % 2 == 0) && requires{{
                meta::distance(self.template arg<quotient<T::alternatives>>()) +
                concat_distance<I + T::alternatives + 1>::middle(self)
            } -> meta::convertible_to<ssize_t>;})
        {
            return
                meta::distance(self.template arg<quotient<T::alternatives>>()) +
                concat_distance<I + T::alternatives + 1>::middle(self);
        }

        template <typename T> requires (quotient<T::alternatives> >= remainder<T::alternatives>)
        static constexpr ssize_t middle(const T& self) noexcept {
            return 0;
        }

        template <typename T> requires (I < sentinel<T::alternatives>)
        static constexpr ssize_t operator()(const T& lhs, const T& rhs)
            noexcept (requires{{
                left<quotient<T::alternatives>>(lhs) +
                concat_distance<I + T::alternatives + 1>::middle(lhs)
            } noexcept -> meta::nothrow::convertible_to<ssize_t>;})
            requires (quotient<T::alternatives> < remainder<T::alternatives> && requires{{
                left<quotient<T::alternatives>>(lhs) +
                concat_distance<I + T::alternatives + 1>::middle(lhs)
            } -> meta::convertible_to<ssize_t>;})
        {
            ssize_t size =
                left<quotient<T::alternatives>>(lhs) +
                concat_distance<I + T::alternatives + 1>::middle(lhs);
            if constexpr (remainder<T::alternatives> < T::alternatives) {
                size += concat_get<remainder<T::alternatives>>(rhs).index;
            }
            return -size;
        }

        template <typename T> requires (I < sentinel<T::alternatives>)
        static constexpr ssize_t operator()(const T& lhs, const T& rhs)
            noexcept (requires{{
                left<remainder<T::alternatives>>(rhs) +
                concat_distance<
                    (remainder<T::alternatives> + 1) * (T::alternatives + 1) +
                    quotient<T::alternatives>
                >::middle(rhs)
            } noexcept -> meta::nothrow::convertible_to<ssize_t>;})
            requires (remainder<T::alternatives> < quotient<T::alternatives> && requires{{
                left<remainder<T::alternatives>>(rhs) +
                concat_distance<
                    (remainder<T::alternatives> + 1) * (T::alternatives + 1) +
                    quotient<T::alternatives>
                >::middle(rhs)
            } -> meta::convertible_to<ssize_t>;})
        {
            ssize_t size =
                left<remainder<T::alternatives>>(rhs) +
                concat_distance<
                    (remainder<T::alternatives> + 1) * (T::alternatives + 1) +
                    quotient<T::alternatives>
                >::middle(rhs);
            if constexpr (quotient<T::alternatives> < T::alternatives) {
                size += concat_get<quotient<T::alternatives>>(lhs).index;
            }
            return size;
        }

        template <typename T> requires (I < sentinel<T::alternatives>)
        static constexpr ssize_t operator()(const T& lhs, const T& rhs) noexcept
            requires (quotient<T::alternatives> == remainder<T::alternatives>)
        {
            return
                concat_get<quotient<T::alternatives>>(lhs).index -
                concat_get<quotient<T::alternatives>>(rhs).index;
        }

        template <typename T> requires (I >= sentinel<T::alternatives>)
        static constexpr ssize_t operator()(const T& lhs, const T& rhs) noexcept {
            return 0;
        }

        template <typename T> requires (I < T::alternatives)
        static constexpr ssize_t operator()(NoneType, const T& rhs)
            noexcept (requires{{
                (concat_get<I>(rhs).end - concat_get<I>(rhs).begin) +
                concat_distance<(I + 1) * (T::alternatives + 1) + T::alternatives>::middle(rhs)
            } noexcept -> meta::nothrow::convertible_to<ssize_t>;})
            requires (requires{{
                (concat_get<I>(rhs).end - concat_get<I>(rhs).begin) +
                concat_distance<(I + 1) * (T::alternatives + 1) + T::alternatives>::middle(rhs)
            } -> meta::convertible_to<ssize_t>;})
        {
            return
                (concat_get<I>(rhs).end - concat_get<I>(rhs).begin) +
                concat_distance<(I + 1) * (T::alternatives + 1) + T::alternatives>::middle(rhs);
        }

        template <typename T> requires (I == T::alternatives)
        static constexpr ssize_t operator()(NoneType, const T& rhs) noexcept {
            return 0;
        }
    };

    template <typename>
    struct _concat_traits;
    template <typename... U>
    struct _concat_traits<meta::pack<U...>> {
        using category = meta::common_type<typename meta::remove_reference<U>::category...>;
        using reference = meta::make_union<typename meta::remove_reference<U>::reference...>;
    };
    template <typename U>
    using concat_traits = _concat_traits<typename impl::visitable<const U&>::alternatives>;

    /* A subrange is stored over each concatenated argument, consisting of a begin/end
    iterator pair and an encoded index, which obeys the same rules as
    `concat_iterator`.  See the algorithms above for behavioral details. */
    template <range_direction Dir, meta::unqualified Begin, meta::unqualified End>
    struct concat_subrange {
        using category = meta::iterator_category<Begin>;
        using reference = meta::dereference_type<const Begin&>;
        Begin begin;
        End end;
        ssize_t index = 0;
    };

    /* The overall concatenated iterator stores a reference to the outer `impl::concat`
    container and a union of subranges for each argument and separator.  As an
    optimization, the subrange union is reduced to the unique iterator types for each
    argument and separator, which avoids extra dispatch overhead when dereferencing or
    comparing iterators.  An encoded index is used to track which argument or separator
    is currently active, and to mark the end iterator state, with separators being
    encoded as odd indices if they are not trivial.  The total number of alternatives
    is always equal to `n` or `2n - 1` (if a nontrivial separator is given), where `n`
    is equal to the number of concatenated arguments. */
    template <meta::not_reference Outer, range_direction Dir>
    struct concat_iterator {
        static constexpr bool trivial = Outer::trivial;
        static constexpr bool dynamic = Outer::dynamic;
        static constexpr size_t alternatives =
            Outer::arg_type::size() + (Outer::arg_type::size() - 1) * !trivial;

    private:
        template <size_t I>
        struct _type {
            static constexpr size_t idx = I / (1 + !trivial);
            using type = concat_subrange<
                Dir,
                decltype(Dir::begin(std::declval<Outer&>().template arg<idx>())),
                decltype(Dir::end(std::declval<Outer&>().template arg<idx>()))
            >;
        };
        template <size_t I>
            requires ((trivial || I % 2 == 0) && std::same_as<Dir, range_reverse>)
        struct _type<I> {
            static constexpr size_t idx = (alternatives - 1 - I) / (1 + !trivial);
            using type = concat_subrange<
                Dir,
                decltype(Dir::begin(std::declval<Outer&>().template arg<idx>())),
                decltype(Dir::end(std::declval<Outer&>().template arg<idx>()))
            >;
        };
        template <size_t I> requires (!trivial && I % 2 == 1)
        struct _type<I> {
            using type = concat_subrange<
                Dir,
                decltype(Dir::begin(std::declval<Outer&>().template sep())),
                decltype(Dir::end(std::declval<Outer&>().template sep()))
            >;
        };

        template <typename = std::make_index_sequence<Outer::arg_type::size()>>
        struct _unique;
        template <size_t... I>
        struct _unique<std::index_sequence<I...>> {
            using type = meta::make_union<typename _type<I>::type...>;
        };
        template <size_t... I> requires (!trivial)
        struct _unique<std::index_sequence<I...>> {
            using type = meta::make_union<
                concat_subrange<
                    Dir,
                    decltype(Dir::begin(std::declval<Outer&>().sep())),
                    decltype(Dir::end(std::declval<Outer&>().sep()))
                >,
                typename _type<I * 2>::type...
            >;
        };

    public:
        using unique = _unique<>::type;
        static constexpr size_t unique_alternatives = impl::visitable<unique>::alternatives::size();
        using iterator_category = std::conditional_t<
            meta::inherits<typename concat_traits<unique>::category, std::forward_iterator_tag>,
            std::conditional_t<
                meta::inherits<
                    typename concat_traits<unique>::category,
                    std::random_access_iterator_tag
                >,
                std::random_access_iterator_tag,
                typename concat_traits<unique>::category
            >,
            std::forward_iterator_tag
        >;
        using difference_type = ssize_t;
        using reference = concat_traits<unique>::reference;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<value_type>;

        template <size_t I> requires (I < alternatives)
        using type = _type<I>::type;

        Outer* outer = nullptr;
        difference_type index = alternatives;
        [[no_unique_address]] unique child;

        template <size_t I> requires (I < alternatives)
        [[nodiscard]] constexpr auto& arg()
            noexcept (requires{{outer->template arg<_type<I>::idx>()} noexcept;})
            requires (trivial || I % 2 == 0)
        {
            return outer->template arg<_type<I>::idx>();
        }

        template <size_t I> requires (I < alternatives)
        [[nodiscard]] constexpr auto& arg() const
            noexcept (requires{{outer->template arg<_type<I>::idx>()} noexcept;})
            requires (trivial || I % 2 == 0)
        {
            return outer->template arg<_type<I>::idx>();
        }

        [[nodiscard]] constexpr ssize_t sep_size() const noexcept {
            if constexpr (!dynamic) {
                return meta::tuple_size<typename Outer::sep_type>;
            } else {
                return outer->sep_size();
            }
        }

        template <size_t I> requires (I < alternatives)
        [[nodiscard]] constexpr type<I> get()
            noexcept (requires{
                {type<I>{Dir::begin(outer->sep()), Dir::end(outer->sep())}} noexcept;
            })
            requires (!trivial && I % 2 == 1)
        {
            return {Dir::begin(outer->sep()), Dir::end(outer->sep())};
        }

        template <size_t I> requires (I < alternatives)
        [[nodiscard]] constexpr type<I> get()
            noexcept (requires{{type<I>{Dir::begin(arg<I>()), Dir::end(arg<I>())}} noexcept;})
            requires (trivial || I % 2 == 0)
        {
            return {Dir::begin(arg<I>()), Dir::end(arg<I>())};
        }

    private:
        template <ssize_t I = 0> requires (I < alternatives)
        constexpr unique init()
            noexcept (requires(decltype(get<I>()) curr) {
                {get<I>()} noexcept -> meta::nothrow::convertible_to<unique>;
                {curr.begin != curr.end} noexcept -> meta::nothrow::truthy;
            } && (I + 2 >= alternatives || (
                requires{{init<I + 2>()} noexcept;} &&
                (trivial || I != 0 || requires{
                    {get<1>()} noexcept -> meta::nothrow::convertible_to<unique>;
                })
            )))
        {
            if (auto curr = get<I>(); curr.begin != curr.end) {
                return std::move(curr);
            }
            if constexpr (I + 2 < alternatives) {
                if constexpr (!trivial && I == 0) {
                    ++index;
                    if constexpr (!dynamic) {
                        return get<1>();
                    } else {
                        if (outer->sep_size() != 0) {
                            return get<1>();
                        }
                    }
                    ++index;
                } else {
                    index += 1 + !trivial;
                }
                return init<I + 2>();
            } else {
                /// NOTE: if we reach this point, then the last alternative is
                /// guaranteed to be empty
                ++index;
                return get<alternatives - 1>();
            }
        }

        using deref = impl::basic_vtable<concat_deref<reference>::template fn, unique_alternatives>;
        using increment = impl::basic_vtable<concat_increment, alternatives>;
        using decrement = impl::basic_vtable<concat_decrement, alternatives + 1>;
        using compare = impl::basic_vtable<concat_compare, unique_alternatives>;
        using iter_distance =
            impl::basic_vtable<concat_distance, (alternatives + 1) * (alternatives + 1)>;
        using sentinel_distance = impl::basic_vtable<concat_distance, alternatives + 1>;

    public:
        [[nodiscard]] constexpr concat_iterator() noexcept = default;
        [[nodiscard]] constexpr concat_iterator(Outer& outer)
            noexcept (requires{{init()} noexcept;})
            requires (requires{{init()};})
        :
            outer(&outer),
            index(0),
            child(init())
        {}

        [[nodiscard]] constexpr reference operator*() const
            noexcept (requires{
                {deref{impl::visitable<const unique&>::index(child)}(child)} noexcept;
            })
        {
            return (deref{impl::visitable<const unique&>::index(child)}(child));
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow{*this}} noexcept;})
            requires (requires{{impl::arrow{*this}};})
        {
            return impl::arrow{*this};
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const
            noexcept (requires(concat_iterator tmp) {
                {concat_iterator{*this}} noexcept;
                {tmp += n} noexcept;
                {*tmp} noexcept;
            })
            requires (requires(concat_iterator tmp) {
                {concat_iterator{*this}};
                {tmp += n};
                {*tmp};
            })
        {
            concat_iterator tmp {*this};
            tmp += n;
            return *tmp;
        }

        constexpr concat_iterator& operator++()
            noexcept (requires{{increment{size_t(index)}(*this)} noexcept;})
            requires (requires{{increment{size_t(index)}(*this)};})
        {
            increment{size_t(index)}(*this);
            return *this;
        }

        [[nodiscard]] constexpr concat_iterator operator++(int)
            noexcept (requires{
                {concat_iterator{*this}} noexcept;
                {++*this} noexcept;
            })
            requires (requires{
                {concat_iterator{*this}};
                {++*this};
            })
        {
            concat_iterator tmp {*this};
            ++*this;
            return tmp;
        }

        constexpr concat_iterator& operator+=(difference_type n)
            noexcept (requires{
                {decrement{size_t(index)}(*this, n)} noexcept;
                {increment{size_t(index)}(*this, n)} noexcept;
            })
            requires (requires{
                {decrement{size_t(index)}(*this, n)};
                {increment{size_t(index)}(*this, n)};
            })
        {
            if (n < 0) {
                n = -n;
                decrement{size_t(index)}(*this, n);
            } else if (n > 0) {
                increment{size_t(index)}(*this, n);
            }
            return *this;
        }

        [[nodiscard]] friend constexpr concat_iterator operator+(
            const concat_iterator& self,
            difference_type n
        )
            noexcept (requires(concat_iterator tmp) {
                {concat_iterator{self}} noexcept;
                {tmp += n} noexcept;
            })
            requires (requires(concat_iterator tmp) {
                {concat_iterator{self}};
                {tmp += n};
            })
        {
            concat_iterator tmp {self};
            tmp += n;
            return tmp;
        }

        [[nodiscard]] friend constexpr concat_iterator operator+(
            difference_type n,
            const concat_iterator& self
        )
            noexcept (requires(concat_iterator tmp) {
                {concat_iterator{self}} noexcept;
                {tmp += n} noexcept;
            })
            requires (requires(concat_iterator tmp) {
                {concat_iterator{self}};
                {tmp += n};
            })
        {
            concat_iterator tmp {self};
            tmp += n;
            return tmp;
        }

        constexpr concat_iterator& operator--()
            noexcept (requires{{decrement{size_t(index)}(*this)} noexcept;})
            requires (requires{{decrement{size_t(index)}(*this)};})
        {
            decrement{size_t(index)}(*this);
            return *this;
        }

        [[nodiscard]] constexpr concat_iterator operator--(int)
            noexcept (requires{
                {concat_iterator{*this}} noexcept;
                {--*this} noexcept;
            })
            requires (requires{
                {concat_iterator{*this}};
                {--*this};
            })
        {
            concat_iterator temp {*this};
            --*this;
            return temp;
        }

        constexpr concat_iterator& operator-=(difference_type n)
            noexcept (requires{{decrement{size_t(index)}(*this, n)} noexcept;})
            requires (requires{{decrement{size_t(index)}(*this, n)};})
        {
            decrement{size_t(index)}(*this, n);
            return *this;
        }

        [[nodiscard]] constexpr concat_iterator operator-(difference_type n) const
            noexcept (requires(concat_iterator tmp) {
                {concat_iterator{*this}} noexcept;
                {tmp -= n} noexcept;
            })
            requires (requires(concat_iterator tmp) {
                {concat_iterator{*this}};
                {tmp -= n};
            })
        {
            concat_iterator tmp {*this};
            tmp -= n;
            return tmp;
        }

        [[nodiscard]] constexpr difference_type operator-(const concat_iterator& other) const
            noexcept (requires{{
                iter_distance{size_t(index) * (alternatives + 1) + size_t(other.index)}(
                    *this,
                    other
                )
            } noexcept;})
            requires (requires{{
                iter_distance{size_t(index) * (alternatives + 1) + size_t(other.index)}(
                    *this,
                    other
                )
            };})
        {
            return iter_distance{size_t(index) * (alternatives + 1) + size_t(other.index)}(
                *this,
                other
            );
        }

        [[nodiscard]] friend constexpr difference_type operator-(
            const concat_iterator& self,
            NoneType
        )
            noexcept (requires{{sentinel_distance{size_t(self.index)}(None, self)} noexcept;})
            requires (requires{{sentinel_distance{size_t(self.index)}(None, self)};})
        {
            return -sentinel_distance{size_t(self.index)}(None, self);
        }

        [[nodiscard]] friend constexpr difference_type operator-(
            NoneType,
            const concat_iterator& self
        )
            noexcept (requires{{sentinel_distance{size_t(self.index)}(None, self)} noexcept;})
            requires (requires{{sentinel_distance{size_t(self.index)}(None, self)};})
        {
            return sentinel_distance{size_t(self.index)}(None, self);
        }

        [[nodiscard]] constexpr bool operator==(const concat_iterator& other) const
            noexcept (requires{{*this <=> other} noexcept;})
        {
            return (*this <=> other) == std::strong_ordering::equal;
        }

        [[nodiscard]] constexpr std::strong_ordering operator<=>(const concat_iterator& other) const
            noexcept (requires{
                {compare{impl::visitable<const unique&>::index(child)}(child, other.child)} noexcept;
            })
        {
            if (std::strong_ordering cmp = index <=> other.index; cmp != 0) return cmp;
            if (index < 0 || index >= ssize_t(alternatives)) return std::strong_ordering::equal;
            return compare{impl::visitable<const unique&>::index(child)}(child, other.child);
        }

        [[nodiscard]] friend constexpr bool operator==(
            const concat_iterator& self,
            NoneType
        ) noexcept {
            return self.index == ssize_t(alternatives);
        }

        [[nodiscard]] friend constexpr bool operator==(
            NoneType,
            const concat_iterator& self
        ) noexcept {
            return self.index == ssize_t(alternatives);
        }

        [[nodiscard]] friend constexpr auto operator<=>(
            const concat_iterator& self,
            NoneType
        ) noexcept {
            return self.index <=> ssize_t(alternatives);
        }

        [[nodiscard]] friend constexpr auto operator<=>(
            NoneType,
            const concat_iterator& self
        ) noexcept {
            return ssize_t(alternatives) <=> self.index;
        }
    };

    /* If the separator is tuple-like, then its size does not need to be stored as a
    member, nor does it need to be checked at runtime in the above algorithms.  If the
    separator is empty, then it may also be overlapped with the arguments, reducing
    binary size to just a tuple of the input arguments. */
    template <typename Sep, typename... A>
    struct concat_storage {
    protected:
        template <meta::not_reference Outer, range_direction Dir>
        friend struct concat_iterator;

        using arg_type = impl::basic_tuple<meta::as_range_or_scalar<A>...>;
        using sep_type = meta::as_range_or_scalar<Sep>;
        static constexpr bool trivial = false;
        static constexpr bool dynamic = true;

        [[no_unique_address]] arg_type m_args;
        [[no_unique_address]] impl::ref<sep_type> m_sep;
        [[no_unique_address]] ssize_t m_sep_size;

        constexpr ssize_t get_size()
            noexcept (requires{{ssize_t(meta::distance(*m_sep))} noexcept;})
            requires (requires{{ssize_t(meta::distance(*m_sep))};})
        {
            return ssize_t(meta::distance(*m_sep));
        }

    public:
        constexpr concat_storage(meta::forward<Sep> value, meta::forward<A>... args)
            noexcept (requires{
                {arg_type{std::forward<A>(args)...}} noexcept;
                {impl::ref<sep_type>{std::forward<Sep>(value)}} noexcept;
                {get_size()} noexcept;
            })
            requires (requires{
                {arg_type{std::forward<A>(args)...}};
                {impl::ref<sep_type>{std::forward<Sep>(value)}};
                {get_size()};
            })
        :
            m_args{std::forward<A>(args)...},
            m_sep(std::forward<Sep>(value)),
            m_sep_size(get_size())
        {}
    };
    template <typename Sep, typename... A>
        requires (meta::tuple_like<meta::as_range_or_scalar<Sep>>)
    struct concat_storage<Sep, A...> {
    protected:
        template <meta::not_reference Outer, range_direction Dir>
        friend struct concat_iterator;

        using arg_type = impl::basic_tuple<meta::as_range_or_scalar<A>...>;
        using sep_type = meta::as_range_or_scalar<Sep>;
        static constexpr bool trivial = meta::tuple_size<sep_type> == 0;
        static constexpr bool dynamic = false;

        [[no_unique_address]] arg_type m_args;
        [[no_unique_address]] impl::ref<sep_type> m_sep;
        static constexpr ssize_t m_sep_size = meta::tuple_size<sep_type>;

    public:
        constexpr concat_storage(meta::forward<Sep> value, meta::forward<A>... args)
            noexcept (requires{
                {arg_type{std::forward<A>(args)...}} noexcept;
                {impl::ref<sep_type>{std::forward<Sep>(value)}} noexcept;
            })
            requires (requires{
                {arg_type{std::forward<A>(args)...}};
                {impl::ref<sep_type>{std::forward<Sep>(value)}};
            })
        :
            m_args{std::forward<A>(args)...},
            m_sep(std::forward<Sep>(value))
        {}
    };

    /* The outermost `concat` type stores the arguments, separator, and relevant
    metadata, as well as managing indexing and iteration over the concatenated ranges.
    An instance of this type will only be generated when more than one argument is
    given to `iter::concat{}(...)`.  For zero or one argument, an identity range will
    be returned instead. */
    template <meta::not_rvalue Sep, meta::not_rvalue... As> requires (sizeof...(As) > 1)
    struct concat : concat_storage<Sep, As...> {
    private:
        using base = concat_storage<Sep, As...>;

        template <size_t... I>
        constexpr size_t _size(std::index_sequence<I...>) const
            noexcept (requires{{
                (meta::size(base::m_args.template get<I>()) + ... + 0)
            } noexcept -> meta::nothrow::convertible_to<size_t>;})
            requires (requires{{
                (meta::size(base::m_args.template get<I>()) + ... + 0)
            } -> meta::convertible_to<size_t>;})
        {
            return (meta::size(base::m_args.template get<I>()) + ... + 0);
        }

        template <size_t... I>
        constexpr ssize_t _ssize(std::index_sequence<I...>) const
            noexcept (requires{{
                (meta::ssize(base::m_args.template get<I>()) + ... + 0)
            } noexcept -> meta::nothrow::convertible_to<ssize_t>;})
            requires (requires{{
                (meta::ssize(base::m_args.template get<I>()) + ... + 0)
            } -> meta::convertible_to<ssize_t>;})
        {
            return (meta::ssize(base::m_args.template get<I>()) + ... + 0);
        }

        template <size_t I, size_t J>
        struct _get {
            template <typename Self>
            static constexpr decltype(auto) operator()(Self&& self)
                noexcept (requires{{meta::get<I>(
                    std::forward<Self>(self).template arg<J / (1 + !base::trivial)>()
                )} noexcept;})
                requires (I < meta::tuple_size<meta::unpack_type<
                    J / (1 + !base::trivial),
                    meta::as_range_or_scalar<As>...
                >>)
            {
                return (meta::get<I>(
                    std::forward<Self>(self).template arg<J / (1 + !base::trivial)>()
                ));
            }
            template <typename Self>
            static constexpr decltype(auto) operator()(Self&& self)
                noexcept (requires{{_get<
                    I - meta::tuple_size<meta::unpack_type<
                        J / (1 + !base::trivial),
                        meta::as_range_or_scalar<As>...
                    >>,
                    J + 1
                >{}(std::forward<Self>(self))} noexcept;})
                requires (I >= meta::tuple_size<meta::unpack_type<
                    J / (1 + !base::trivial),
                    meta::as_range_or_scalar<As>...
                >>)
            {
                return (_get<
                    I - meta::tuple_size<meta::unpack_type<
                        J / (1 + !base::trivial),
                        meta::as_range_or_scalar<As>...
                    >>,
                    J + 1
                >{}(std::forward<Self>(self)));
            }
        };
        template <size_t I, size_t J> requires (!base::trivial && J % 2 == 1)
        struct _get<I, J> {
            template <typename Self>
            static constexpr decltype(auto) operator()(Self&& self)
                noexcept (requires{{meta::get<I>(std::forward<Self>(self).sep())} noexcept;})
                requires (I < meta::tuple_size<meta::as_range_or_scalar<Sep>>)
            {
                return (meta::get<I>(std::forward<Self>(self).sep()));
            }
            template <typename Self>
            static constexpr decltype(auto) operator()(Self&& self)
                noexcept (requires{{_get<
                    I - meta::tuple_size<meta::as_range_or_scalar<Sep>>,
                    J + 1
                >{}(std::forward<Self>(self))} noexcept;})
                requires (I >= meta::tuple_size<meta::as_range_or_scalar<Sep>>)
            {
                return (_get<
                    I - meta::tuple_size<meta::as_range_or_scalar<Sep>>,
                    J + 1
                >{}(std::forward<Self>(self)));
            }
        };

        /// TODO: need to derive the proper reference type for `front()`, `back()`,
        /// and the subscript operator.

    public:
        using base::base;

        template <size_t I, typename Self> requires (I < sizeof...(As))
        [[nodiscard]] constexpr decltype(auto) arg(this Self&& self) noexcept {
            return (std::forward<Self>(self).m_args.template get<I>());
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) sep(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_sep);
        }

        [[nodiscard]] constexpr ssize_t sep_size() const noexcept {
            return base::m_sep_size;
        }

        [[nodiscard]] constexpr size_t size() const
            noexcept (requires{{
                _size(std::index_sequence_for<As...>{}) + sep_size() * (sizeof...(As) - 1)
            } noexcept;})
            requires (requires{{
                _size(std::index_sequence_for<As...>{}) + sep_size() * (sizeof...(As) - 1)
            };})
        {
            return _size(std::index_sequence_for<As...>{}) + sep_size() * (sizeof...(As) - 1);
        }

        [[nodiscard]] constexpr ssize_t ssize() const
            noexcept (requires{{
                _ssize(std::index_sequence_for<As...>{}) + sep_size() * (sizeof...(As) - 1)
            } noexcept;})
            requires (requires{{
                _ssize(std::index_sequence_for<As...>{}) + sep_size() * (sizeof...(As) - 1)
            };})
        {
            return _ssize(std::index_sequence_for<As...>{}) + sep_size() * (sizeof...(As) - 1);
        }

        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{
                sep_size() == 0 && _size(std::index_sequence_for<As...>{}) == 0
            } noexcept;})
            requires (requires{{
                sep_size() == 0 && _size(std::index_sequence_for<As...>{}) == 0
            };})
        {
            return sep_size() == 0 && _size(std::index_sequence_for<As...>{}) == 0;
        }

        template <ssize_t I, typename Self>
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            requires ((
                meta::tuple_like<meta::as_range_or_scalar<Sep>> &&
                ... &&
                meta::tuple_like<meta::as_range_or_scalar<As>>
            ) && impl::valid_index<(
                (meta::tuple_size<meta::as_range_or_scalar<Sep>> * (sizeof...(As) - 1)) +
                ... +
                meta::tuple_size<meta::as_range_or_scalar<As>>
            ), I>)
        {
            return (_get<
                impl::normalize_index<(
                    (meta::tuple_size<meta::as_range_or_scalar<Sep>> * (sizeof...(As) - 1)) +
                    ... +
                    meta::tuple_size<meta::as_range_or_scalar<As>>
                ), I>(),
                0
            >{}(std::forward<Self>(self)));
        }

        [[nodiscard]] constexpr concat_iterator<concat, range_forward> begin()
            noexcept (requires{{concat_iterator<concat, range_forward>{*this}} noexcept;})
            requires (requires{{concat_iterator<concat, range_forward>{*this}};})
        {
            return {*this};
        }

        [[nodiscard]] constexpr concat_iterator<const concat, range_forward> begin() const
            noexcept (requires{{concat_iterator<const concat, range_forward>{*this}} noexcept;})
            requires (requires{{concat_iterator<const concat, range_forward>{*this}};})
        {
            return {*this};
        }

        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }

        [[nodiscard]] constexpr concat_iterator<concat, range_reverse> rbegin()
            noexcept (requires{{concat_iterator<concat, range_reverse>{*this}} noexcept;})
            requires (requires{{concat_iterator<concat, range_reverse>{*this}};})
        {
            return {*this};
        }

        [[nodiscard]] constexpr concat_iterator<const concat, range_reverse> rbegin() const
            noexcept (requires{{concat_iterator<const concat, range_reverse>{*this}} noexcept;})
            requires (requires{{concat_iterator<const concat, range_reverse>{*this}};})
        {
            return {*this};
        }

        [[nodiscard]] static constexpr NoneType rend() noexcept { return {}; }
    };

}


namespace iter {

    /* Concatenate any number of arguments into a single range, with possible
    separators between each element.

    By default, no separator will be used, resulting in the arguments being directly
    adjacent in the output range.  A custom separator may be provided as the
    initializer to `concat{}` (e.g. `concat{"."}("a", "b", "c")`), which will be
    inserted between each argument in the resulting range (producing `"a.b.c"` as
    output).  If the separator's size is known at compile time (i.e. a scalar or any
    tuple-like type), then no runtime overhead will be incurred for storing or checking
    its size, and the separator may be omitted from the binary entirely.

    For both the separator and arguments, range inputs will be treated differently from
    non-range inputs, which are always converted into scalars.  In contrast, ranges
    will have their elements flattened into the concatenated range.  For example:

        ```
        iter::concat{"."}("ab", "cd", "ef");
        ```

    produces the range `["ab", ".", "cd", ".", "ef"]`, whereas:

        ```
        iter::concat{range(".")}(range("ab"), range("cd"), range("ef"));
        ```

    flattens to `['a', 'b', '.', 'c', 'd', '.', 'e', 'f']` (note the `char` elements).
    Range inputs of this form can be used to flatten arguments (as shown above), or to
    insert separators of more than one element, such as:

        ```
        iter::concat{range("::")}("ab", "cd", "ef");
        ```

    which gives `['a', 'b', ':', ':', 'c', 'd', ':', ':', 'e', 'f']` as output. */
    template <meta::not_void Sep = range<>> requires (meta::not_rvalue<Sep>)
    struct concat {
        [[no_unique_address]] Sep sep;

        [[nodiscard]] static constexpr range<> operator()() noexcept {
            return {};
        }

        template <typename A>
        [[nodiscard]] static constexpr decltype(auto) operator()(A&& a)
            noexcept (requires{{meta::to_range_or_scalar(std::forward<A>(a))} noexcept;})
            requires (requires{{meta::to_range_or_scalar(std::forward<A>(a))};})
        {
            return (meta::to_range_or_scalar(std::forward<A>(a)));
        }

        template <typename Self, typename... A> requires (sizeof...(A) > 1)
        [[nodiscard]] constexpr auto operator()(this Self&& self, A&&... a)
            noexcept (requires{{range<impl::concat<Sep, meta::remove_rvalue<A>...>>{
                std::forward<Self>(self).sep,
                std::forward<A>(a)...
            }} noexcept;})
            requires (requires{{range<impl::concat<Sep, meta::remove_rvalue<A>...>>{
                std::forward<Self>(self).sep,
                std::forward<A>(a)...
            }};})
        {
            return range<impl::concat<Sep, meta::remove_rvalue<A>...>>{
                std::forward<Self>(self).sep,
                std::forward<A>(a)...
            };
        }
    };

    template <typename T = range<>>
    concat(T&&) -> concat<meta::remove_rvalue<T>>;

}


}


_LIBCPP_BEGIN_NAMESPACE_STD

    namespace ranges {

        template <typename Sep, typename... As>
        constexpr bool enable_borrowed_range<bertrand::impl::concat<Sep, As...>> = (
            enable_borrowed_range<bertrand::meta::unqualify<
                bertrand::meta::as_range_or_scalar<Sep>
            >> &&
            ... &&
            enable_borrowed_range<bertrand::meta::unqualify<
                bertrand::meta::as_range_or_scalar<As>
            >>
        );

    }

    template <typename Sep, typename... As>
        requires (
            bertrand::meta::tuple_like<bertrand::meta::as_range_or_scalar<Sep>> &&
            ... &&
            bertrand::meta::tuple_like<bertrand::meta::as_range_or_scalar<As>>
        )
    struct tuple_size<bertrand::impl::concat<Sep, As...>> : std::integral_constant<size_t, (
        (bertrand::meta::tuple_size<bertrand::meta::as_range_or_scalar<Sep>> * (sizeof...(As) - 1)) +
        ... +
        bertrand::meta::tuple_size<bertrand::meta::as_range_or_scalar<As>>
    )> {};

    template <size_t I, typename Sep, typename... As>
        requires (I < tuple_size<bertrand::impl::concat<Sep, As...>>::value)
    struct tuple_element<I, bertrand::impl::concat<Sep, As...>> {
        using type = bertrand::meta::remove_rvalue<decltype((
            std::declval<bertrand::impl::concat<Sep, As...>>().template get<I>()
        ))>;
    };

_LIBCPP_END_NAMESPACE_STD


namespace bertrand::iter {

    static constexpr auto c = concat{4}(1, 2, range(std::array<int, 0>{}));
    static_assert(c.size() == 4);
    static_assert(sizeof(c) == sizeof(int) * 4);
    static_assert(c.end() - c.begin() == 4);
    static_assert([] {
        auto it = c.begin();
        if (*it++ != 1) return false;
        if (*it++ != 4) return false;
        if (*it++ != 2) return false;
        if (*it++ != 4) return false;
        // if (*it++ != 3) return false;
        if (it != c.end()) return false;

        for (auto&& v : c) {
            if (v != 1 && v != 2 && v != 3 && v != 4) {
                return false;
            }
        }

        return true;
    }());

    static_assert([] {
        auto it = c.rbegin();
        if (*it++ != 4) return false;
        --it; if (*it++ != 4) return false;
        if (*it++ != 2) return false;
        if (*it++ != 4) return false;
        if (*it++ != 1) return false;
        if (it != c.rend()) return false;

        return true;
    }());



    static_assert((c.begin() + 4) - (c.begin()) == 4);
    static_assert((c.begin()) - (c.begin() + 4) == -4);


    static constexpr auto c2 = concat{3}(1, 2.5);
    static_assert([] {
        auto&& [a, b, c] = c2;
        if (a != 1) return false;
        if (b != 3) return false;
        if (c != 2.5) return false;

        return true;
    }());


}



#endif  // BERTRAND_ITER_CONCAT_H