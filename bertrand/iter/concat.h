#ifndef BERTRAND_ITER_CONCAT_H
#define BERTRAND_ITER_CONCAT_H

#include "bertrand/iter/range.h"


namespace bertrand {


/// TODO: document all of this thoroughly


/// TODO: concat{} can use a standardized meta::to_range_or_scalar() method and
/// `meta::as_range_or_scalar<>` alias instead of reimplementing them.


namespace impl {

    template <size_t I, typename T> requires (I < T::alternatives)
    constexpr size_t _concat_get = (!T::trivial && I % 2 == 1) ?
        0 :
        impl::visitable<const typename T::unique&>::alternatives::template index<
            const typename T::template type<I>&
        >();

    /* Cast the internal union of a `concat_iterator` to the alternative encoded at
    index `I`.  Separators are encoded as odd indices if they are present, and the
    underlying union may be collapsed to just the unique alternatives as an
    optimization, which complicates decoding.  Additionally, if the direction is
    reversed, then the encoded index will count from right to left rather than left to
    right. */
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
    the vtable function if they happen to match, meaning that it can get away with
    using unique alternatives, and forcing the argument types to match exactly. */
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

    template <size_t I, size_t alternatives>
    constexpr size_t concat_next = I + 1 < alternatives ? I + 1 : I;
    template <size_t I>
    constexpr size_t concat_prev = I > 0 ? I - 1 : I;

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
        static constexpr size_t next = (trivial ? I + 1 : (I | 1) + 1);

        template <typename T>
        static constexpr void skip(T& self)
            noexcept (
                next<T::trivial> >= T::alternatives ||
                requires(decltype(self.template get<next<T::trivial>, range_first>()) sub) {
                    {sub.begin != sub.end} noexcept -> meta::nothrow::truthy;
                    {self.child = self.template get<next<T::trivial>, range_first>()} noexcept;
                    {concat_increment<next<T::trivial>>::skip(self)} noexcept;
                }
            )
        {
            if constexpr (next<T::trivial> < T::alternatives) {
                self.index = next<T::trivial>;
                if (
                    auto sub = self.template get<next<T::trivial>, range_first>();
                    sub.begin != sub.end
                ) {
                    self.child = std::move(sub);
                    return;
                }
                concat_increment<next<T::trivial>>::skip(self);
            } else {
                /// NOTE: if we reach this point, then the last alternative is
                /// guaranteed to be empty
                self.index = T::alternatives;
                self.child = self.template get<T::alternatives - 1, range_first>();
            }
        }

        template <typename T>
        static constexpr void operator()(T& self)
            noexcept (requires(decltype((concat_get<I>(self))) curr) {
                {++curr.begin} noexcept;
                {curr.begin != curr.end} noexcept -> meta::nothrow::truthy;
            } && (I + 1 >= T::alternatives || (requires{{skip(self)} noexcept;} && (
                T::trivial || I % 2 != 0 || requires{
                    {self.child = self.template get<I + 1, range_first>()} noexcept;
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
                        self.child = self.template get<I + 1, range_first>();
                        return;
                    } else {  // size must be checked at run time
                        if (self.outer->sep_size() != 0) {
                            self.child = self.template get<I + 1, range_first>();
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
                self.template get<concat_next<I, T::alternatives>, range_first>()
            )) curr) {
                {
                    self.child = self.template get<concat_next<I, T::alternatives>, range_first>()
                } noexcept;
                {curr.end - curr.begin} noexcept -> meta::nothrow::convertible_to<ssize_t>;
                {curr.begin += n} noexcept;
            })
            requires (requires(decltype((
                self.template get<concat_next<I, T::alternatives>, range_first>()
            )) curr) {
                {self.child = self.template get<concat_next<I, T::alternatives>, range_first>()};
                {curr.end - curr.begin} -> meta::convertible_to<ssize_t>;
                {curr.begin += n};
            })
        {
            if constexpr (I + 1 < T::alternatives) {  // next alternative exists
                ++self.index;
                if constexpr (!T::trivial && I % 2 == 0) {  // next alternative is a separator
                    ssize_t size = self.sep_size();
                    if (n < size) {
                        auto curr = self.template get<I + 1, range_first>();
                        curr.begin += n;
                        curr.index += n;
                        self.child = std::move(curr);
                        return;
                    }
                    n -= size;
                } else {  // next alternative is an argument
                    auto curr = self.template get<I + 1, range_first>();
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
                auto curr = self.template get<I, range_first>(self);
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
    first, and then retreat to either the previous argument or separator if it causes
    the index to decrement past zero, possibly requiring recursive calls for prior
    indices (bypassing separators).
    
    Random-access decrements are optimized to skip over intermediate subranges where
    possible, but still require linear time in the number of arguments, albeit with a
    low constant factor due to precomputed separator sizes and iterator indices. */
    template <size_t I>
    struct concat_decrement {
        template <bool trivial>
        static constexpr size_t prev = (trivial ? I - 1 : (I - 1) & ~1);

        template <typename T>
        static constexpr void skip(T& self)
            noexcept (
                I > 0 ||
                requires(decltype(self.template get<prev<T::trivial>, range_last>()) sub) {
                    {sub.begin != sub.end} noexcept -> meta::nothrow::truthy;
                    {self.child = self.template get<prev<T::trivial>, range_last>()} noexcept;
                    {concat_decrement<prev<T::trivial>>::skip(self)} noexcept;
                }
            )
        {
            if constexpr (I > 0) {
                self.index = prev<T::trivial>;
                if (
                    auto sub = self.template get<prev<T::trivial>, range_last>();
                    sub.begin != sub.end
                ) {
                    self.child = std::move(sub);
                    return;
                }
                concat_decrement<prev<T::trivial>>::skip(self);
            } else {
                /// NOTE: if we reach this point, then the first alternative is
                /// guaranteed to be empty
                self.index = 0;
                self.child = self.template get<0, range_first>(self);
            }
        }

        template <typename T>
        static constexpr void operator()(T& self)
            noexcept (requires(decltype((concat_get<I>(self))) curr) {
                {--curr.begin} noexcept;
            } && (I <= 0 || (requires{{skip(self)} noexcept;} && (
                T::trivial || I % 2 != 0 || requires{
                    {self.child = self.template get<I - 1, range_last>()} noexcept;
                }
            ))))
        {
            // inner increment
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
                        self.child = self.template get<I - 1, range_last>();
                        return;
                    } else {  // size must be checked at run time
                        if (self.outer->sep_size() != 0) {
                            self.child = self.template get<I - 1, range_last>();
                            return;
                        }
                        --self.index;
                    }
                }
                skip(self);
            }
        }

        template <typename T>
        static constexpr void skip(T& self, ssize_t& n)
            noexcept (requires(decltype((self.template get<concat_prev<I>, range_first>())) curr) {
                {
                    self.child = self.template get<concat_prev<I>, range_first>()
                } noexcept;
                {curr.end - curr.begin} noexcept -> meta::nothrow::convertible_to<ssize_t>;
                {curr.begin += n} noexcept;
            })
            requires (requires(decltype((self.template get<concat_prev<I>, range_first>())) curr) {
                {self.child = self.template get<concat_prev<I>, range_first>()};
                {curr.end - curr.begin} -> meta::convertible_to<ssize_t>;
                {curr.begin += n};
            })
        {
            if constexpr (I > 0) {  // previous alternative exists
                --self.index;
                if constexpr (!T::trivial && I % 2 == 0) {  // previous alternative is a separator
                    ssize_t size = self.sep_size();
                    if (n < size) {
                        auto curr = self.template get<I - 1, range_first>();
                        size -= n;
                        curr.begin += size;
                        curr.index += size;
                        self.child = std::move(curr);
                        return;
                    }
                    n -= size;
                } else {
                    auto curr = self.template get<I - 1, range_first>();
                    ssize_t size = curr.end - curr.begin;
                    if (n < size) {
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
                self.child = self.template get<I, range_first>(self);
            }
        }

        template <typename T>
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
    };

    /// TODO: remember that distance needs to encode both indices using a cartesian
    /// product, so that I can determine which subranges to sum over.

    template <size_t I>
    struct concat_distance {
        /// TODO: implement this similar to join_distance
    };

    /// TODO: concat_category and concat_reference can probably be simplified by just
    /// traversing the separator and argument ranges and combining their categories and
    /// reference types directly.  This recursive approach is only needed for
    /// flatten{}.

    template <typename>
    struct _concat_category;
    template <typename... U>
    struct _concat_category<meta::pack<U...>> {
        using type = meta::common_type<typename meta::remove_reference<U>::category...>;
    };
    template <typename U>
    using concat_category = _concat_category<typename impl::visitable<const U&>::alternatives>::type;

    template <typename>
    struct _concat_reference;
    template <typename... U>
    struct _concat_reference<meta::pack<U...>> {
        using type = meta::concat<typename meta::remove_reference<U>::reference...>;
    };
    template <typename U>
    using concat_reference = _concat_reference<typename impl::visitable<const U&>::alternatives>::type;

    template <range_direction Dir, meta::unqualified Begin, meta::unqualified End>
    struct concat_subrange {
        using direction = Dir;
        using begin_type = Begin;
        using end_type = End;
        using category = meta::iterator_category<begin_type>;
        using reference = meta::pack<meta::dereference_type<const begin_type&>>;
        static constexpr bool trivial = true;

        begin_type begin;
        end_type end;
        ssize_t index = 0;

        [[nodiscard]] constexpr concat_subrange() = default;

        template <typename Parent, range_position Position>
        [[nodiscard]] constexpr concat_subrange(Parent& p, Position pos)
            noexcept (requires{
                {Dir::begin(p)} noexcept -> meta::nothrow::convertible_to<Begin>;
                {Dir::end(p)} noexcept -> meta::nothrow::convertible_to<End>;
                {pos(p, begin, end)} noexcept;
            })
        :
            begin(Dir::begin(p)),
            end(Dir::end(p)),
            index(pos(p, begin, end))
        {}

        [[nodiscard]] constexpr ssize_t distance(const concat_subrange& other) const noexcept {
            return index - other.index;
        }
    };

    template <meta::not_reference Outer, range_direction Dir>
    struct concat_iterator {
        using outer_type = Outer;
        using direction = Dir;
        static constexpr bool trivial = outer_type::trivial;
        static constexpr bool dynamic = outer_type::dynamic;
        static constexpr size_t alternatives =
            outer_type::arg_type::size() + (outer_type::arg_type::size() - 1) * !trivial;

    private:
        template <size_t I>
        struct _type {
            static constexpr size_t idx = I / (1 + !trivial);
            using type = concat_subrange<
                direction,
                decltype(direction::begin(std::declval<outer_type&>().template arg<idx>())),
                decltype(direction::end(std::declval<outer_type&>().template arg<idx>()))
            >;
        };
        template <size_t I>
            requires ((trivial || I % 2 == 0) && std::same_as<direction, range_reverse>)
        struct _type<I> {
            static constexpr size_t idx = (outer_type::arg_type::size() - 1 - I) / (1 + !trivial);
            using type = concat_subrange<
                direction,
                decltype(direction::begin(std::declval<outer_type&>().template arg<idx>())),
                decltype(direction::end(std::declval<outer_type&>().template arg<idx>()))
            >;
        };
        template <size_t I> requires (!trivial && I % 2 == 1)
        struct _type<I> {
            using type = concat_subrange<
                direction,
                decltype(direction::begin(std::declval<outer_type&>().template sep())),
                decltype(direction::end(std::declval<outer_type&>().template sep()))
            >;
        };

        template <typename = std::make_index_sequence<outer_type::arg_type::size()>>
        struct _unique;
        template <size_t... I>
        struct _unique<std::index_sequence<I...>> {
            using separator = void;
            using unique = meta::make_union<typename _type<I>::type...>;
        };
        template <size_t... I> requires (!trivial)
        struct _unique<std::index_sequence<I...>> {
            using separator = concat_subrange<
                direction,
                decltype(direction::begin(std::declval<outer_type&>().sep())),
                decltype(direction::end(std::declval<outer_type&>().sep()))
            >;
            using unique = meta::make_union<separator, typename _type<I>::type...>;
        };

    public:
        using separator = _unique<>::separator;
        using unique = _unique<>::unique;
        static constexpr size_t unique_alternatives =
            impl::visitable<unique>::alternatives::size();

        using iterator_category = std::conditional_t<
            meta::inherits<concat_category<unique>, std::forward_iterator_tag>,
            std::conditional_t<
                meta::inherits<concat_category<unique>, std::random_access_iterator_tag>,
                std::random_access_iterator_tag,
                concat_category<unique>
            >,
            std::forward_iterator_tag
        >;
        using difference_type = ssize_t;
        using reference = concat_reference<unique>::template eval<meta::make_union>;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<value_type>;

        template <size_t I> requires (I < alternatives)
        using type = _type<I>::type;

        outer_type* outer;
        difference_type index;
        [[no_unique_address]] unique child;

        [[nodiscard]] constexpr ssize_t sep_size() const noexcept {
            if constexpr (!dynamic) {
                return meta::tuple_size<typename outer_type::sep_type>;
            } else {
                return outer->sep_size();
            }
        }

        template <size_t I, range_position Position> requires (I < alternatives)
        [[nodiscard]] constexpr type<I> get()
            noexcept (requires{{type<I>{outer->sep(), Position{}}} noexcept;})
            requires (!trivial && I % 2 == 1)
        {
            return {outer->sep(), Position{}};
        }

        template <size_t I, range_position Position> requires (I < alternatives)
        [[nodiscard]] constexpr type<I> get()
            noexcept (requires{
                {type<I>{outer->template arg<_type<I>::idx>(), Position{}}} noexcept;
            })
            requires (trivial || I % 2 == 0)
        {
            return {outer->template arg<_type<I>::idx>(), Position{}};
        }

    private:
        template <ssize_t I = 0> requires (I < alternatives)
        constexpr unique init()
            noexcept (requires(decltype(get<I>()) curr) {
                {get<I, range_first>()} noexcept -> meta::nothrow::convertible_to<unique>;
                {curr.begin != curr.end} noexcept -> meta::nothrow::truthy;
            } && (I + 2 >= alternatives || (
                requires{{init<I + 2>()} noexcept;} &&
                (trivial || I != 0 || requires{
                    {get<1, range_first>()} noexcept -> meta::nothrow::convertible_to<unique>;
                })
            )))
        {
            if (auto curr = get<I, range_first>(); curr.begin != curr.end) {
                return std::move(curr);
            }
            if constexpr (I + 2 < alternatives) {
                if constexpr (!trivial && I == 0) {
                    ++index;
                    if constexpr (!dynamic) {
                        return get<1, range_first>();
                    } else {
                        if (outer->sep_size() != 0) {
                            return get<1, range_first>();
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
                return get<alternatives - 1, range_first>();
            }
        }

    public:
        [[nodiscard]] constexpr concat_iterator(outer_type* outer = nullptr) noexcept :
            outer(outer),
            index(alternatives)
        {}

        [[nodiscard]] constexpr concat_iterator(outer_type& outer)
            noexcept (requires{{init()} noexcept;})
            requires (requires{{init()};})
        :
            outer(&outer),
            index(0),
            child(init())
        {}

        [[nodiscard]] constexpr reference operator*() const
            noexcept (requires{{
                impl::basic_vtable<concat_deref<reference>::template fn, unique_alternatives>{
                    impl::visitable<const unique&>::index(child)
                }(child)
            } noexcept;})
        {
            return (impl::basic_vtable<concat_deref<reference>::template fn, unique_alternatives>{
                impl::visitable<const unique&>::index(child)
            }(child));
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow{*this}} noexcept;})
            requires (requires{{impl::arrow{*this}};})
        {
            return impl::arrow{*this};
        }

        /// TODO: this may be able to be optimized to avoid duplicate dispatching,
        /// and then possibly reused to implement the subscript operator for concat?

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
            noexcept (requires{{
                impl::basic_vtable<concat_increment, alternatives>{size_t(index)}(*this)
            } noexcept;})
            requires (requires{{
                impl::basic_vtable<concat_increment, alternatives>{size_t(index)}(*this)
            };})
        {
            impl::basic_vtable<concat_increment, alternatives>{size_t(index)}(*this);
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
            noexcept (requires{{
                impl::basic_vtable<concat_increment, alternatives>{size_t(index)}(*this, n)
            } noexcept;})
            requires (requires{{
                impl::basic_vtable<concat_increment, alternatives>{size_t(index)}(*this, n)
            };})
        {
            impl::basic_vtable<concat_increment, alternatives>{size_t(index)}(*this, n);
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
            noexcept (requires{{
                impl::basic_vtable<concat_decrement, alternatives>{size_t(index)}(*this)
            } noexcept;})
            requires (requires{{
                impl::basic_vtable<concat_decrement, alternatives>{size_t(index)}(*this)
            };})
        {
            /// TODO: what about decrementing an end iterator, where `curr` is none?
            /// This should probably just initialize to the last valid position.
            impl::basic_vtable<concat_decrement, alternatives>{size_t(index)}(*this);
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
            noexcept (requires{{
                impl::basic_vtable<concat_decrement, alternatives>{size_t(index)}(*this, n)
            } noexcept;})
            requires (requires{{
                impl::basic_vtable<concat_decrement, alternatives>{size_t(index)}(*this, n)
            };})
        {
            impl::basic_vtable<concat_decrement, alternatives>{size_t(index)}(*this, n);
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

        /// TODO: distance needs to encode both indices using a cartesian product

        [[nodiscard]] constexpr difference_type operator-(const concat_iterator& other) const
            noexcept (requires{{
                impl::basic_vtable<concat_decrement, alternatives>{size_t(index)}(*this, other)
            } noexcept;})
            requires (requires{{
                impl::basic_vtable<concat_decrement, alternatives>{size_t(index)}(*this, other)
            };})
        {
            return impl::basic_vtable<concat_decrement, alternatives>{size_t(index)}(*this, other);
        }

        [[nodiscard]] constexpr bool operator==(const concat_iterator& other) const
            noexcept (requires{{*this <=> other} noexcept;})
        {
            return (*this <=> other) == std::strong_ordering::equal;
        }

        [[nodiscard]] constexpr std::strong_ordering operator<=>(const concat_iterator& other) const
            noexcept (requires{{impl::basic_vtable<concat_compare, unique_alternatives>{
                impl::visitable<const unique&>::index(child)
            }(child, other.child)} noexcept;})
        {
            if (std::strong_ordering cmp = index <=> other.index; cmp != 0) return cmp;
            if (index < 0 || index >= ssize_t(alternatives)) return std::strong_ordering::equal;
            return impl::basic_vtable<concat_compare, unique_alternatives>{
                impl::visitable<const unique&>::index(child)
            }(child, other.child);
        }
    };

    /// TODO: if the separator is tuple-like, then the `sep_size` member can be
    /// omitted.

    /// TODO: all the concat helpers should be private and declare the relevant
    /// friends, since this container is publicly accessible on dereference.




    /// TODO: actually, no memory will really be saved by unifying the concat
    /// implementations.  Instead, I should split it into a tuple-like separator and
    /// non-tuple like seperator, which controls whether the sep_size member is
    /// compiled, and then another specialization for an empty tuple-like separator,
    /// which will also omit the separator itself.


    template <meta::not_rvalue Sep, meta::not_rvalue... As> requires (sizeof...(As) > 0)
    struct concat {
    private:
        template <meta::not_reference Outer, range_direction Dir>
        friend struct concat_iterator;

        using sep_type = meta::as_range_or_scalar<Sep>;
        using arg_type = impl::basic_tuple<meta::as_range_or_scalar<As>...>;
        static constexpr bool trivial = meta::empty_range<sep_type>;
        static constexpr bool dynamic = !meta::tuple_like<sep_type>;

        using sep_size_type = std::conditional_t<dynamic, ssize_t, NoneType>;

        [[no_unique_address]] impl::ref<sep_type> m_sep;
        [[no_unique_address]] arg_type m_args;
        [[no_unique_address]] sep_size_type m_sep_size;

        constexpr sep_size_type get_sep_size()
            noexcept (!dynamic || requires{{ssize_t(meta::distance(*m_sep))} noexcept;})
            requires (!dynamic || requires{{ssize_t(meta::distance(*m_sep))};})
        {
            if constexpr (dynamic) {
                return ssize_t(meta::distance(*m_sep));
            } else {
                return None;
            }
        }

    public:
        [[nodiscard]] constexpr concat(meta::forward<Sep> sep, meta::forward<As>... args)
            noexcept (requires{
                {sep_type{std::forward<Sep>(sep)}} noexcept;
                {arg_type{std::forward<As>(args)...}} noexcept;
            })
            requires (requires{
                {sep_type{std::forward<Sep>(sep)}};
                {arg_type{std::forward<As>(args)...}};
            })
        :
            m_sep{std::forward<Sep>(sep)},
            m_args{std::forward<As>(args)...},
            m_sep_size(get_sep_size())
        {}

        [[nodiscard]] constexpr ssize_t sep_size() const noexcept {
            if constexpr (dynamic) {
                return m_sep_size;
            } else {
                return meta::tuple_size<sep_type>;
            }
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) sep(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_sep);
        }

        template <size_t I, typename Self> requires (I < sizeof...(As))
        [[nodiscard]] constexpr decltype(auto) arg(this Self&& self) noexcept {
            return (std::forward<Self>(self).m_args.template get<I>());
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

        [[nodiscard]] constexpr concat_iterator<concat, range_forward> end() noexcept {
            return {this};
        }

        [[nodiscard]] constexpr concat_iterator<const concat, range_forward> end() const noexcept {
            return {this};
        }

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

        [[nodiscard]] constexpr concat_iterator<concat, range_reverse> rend() noexcept {
            return {this};
        }

        [[nodiscard]] constexpr concat_iterator<const concat, range_reverse> rend() const noexcept {
            return {this};
        }
    };

}


namespace iter {

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






namespace bertrand::iter {


    struct Foo {};
    struct Foo2 {};
    struct Bar : Foo, Foo2 {
        int value = 42;
    };
    struct Baz : Bar {};
    static_assert(sizeof(Baz) == sizeof(int));

    template <typename...>
    struct test : Foo {};
    template <typename T, typename... Ts>
    struct test<T, Ts...> : test<Ts...> {
        T value;
        [[no_unique_address]] NoneType r;
    };


    static_assert(sizeof(range<std::tuple<int, int>>) == sizeof(std::tuple<int, int>));


    static_assert(sizeof(impl::basic_tuple<range<int>>) == sizeof(int));

    static_assert(sizeof(test<range<int>>) == sizeof(int));



    static constexpr auto c = concat{}(1, 2, 3);
    static_assert(sizeof(c) == sizeof(int) * 3);
    static_assert([] {
        auto it = c.begin();
        if (*it++ != 1) return false;
        // if (*it++ != 4) return false;
        if (*it++ != 2) return false;
        // if (*it++ != 4) return false;
        if (*it++ != 3) return false;
        if (it != c.end()) return false;


        for (auto v : c) {
            if (v != 1 && v != 2 && v != 3 && v != 4) {
                return false;
            }
        }

        return true;
    }());


}



#endif  // BERTRAND_ITER_CONCAT_H