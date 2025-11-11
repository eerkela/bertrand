#ifndef BERTRAND_ITER_CONCAT_H
#define BERTRAND_ITER_CONCAT_H

#include "bertrand/iter/range.h"


namespace bertrand {


/// TODO: document all of this thoroughly


/// TODO: concat{} can use a standardized meta::to_range_or_scalar() method and
/// `meta::as_range_or_scalar<>` alias instead of implementing them manually.


namespace impl {
    struct concat_tag {};

    struct range_forward {
        template <typename C, typename B, typename E>
        [[nodiscard]] static constexpr ssize_t operator()(C& container, B& begin, E& end) noexcept {
            return 0;
        }
    };
    struct range_reverse {
        template <typename C, typename B, typename E>
        [[nodiscard]] static constexpr ssize_t operator()(C& container, B& begin, E& end)
            noexcept (requires(ssize_t size) {
                {meta::ssize(container) - 1} noexcept -> meta::nothrow::convertible_to<ssize_t>;
                {begin += size} noexcept;
            })
            requires (requires(ssize_t size) {
                {meta::ssize(container) - 1} -> meta::convertible_to<ssize_t>;
                {begin += size};
            })
        {
            ssize_t size = meta::ssize(container) - 1;
            begin += size;
            return size;
        }
        template <typename C, typename B, typename E>
        [[nodiscard]] static constexpr ssize_t operator()(C& container, B& begin, E& end)
            noexcept (requires{
                {B{begin}} noexcept;
                {++begin} noexcept;
                {begin != end} noexcept -> meta::nothrow::truthy;
            })
            requires (requires{
                {B{begin}};
                {++begin};
                {begin != end} -> meta::truthy;
            })
        {
            ssize_t size = 0;
            B tmp = begin;
            ++tmp;
            while (tmp != end) {
                ++begin;
                ++size;
            }
            return size;
        }
    };

    template <typename T>
    concept range_direction = std::same_as<T, range_forward> || std::same_as<T, range_reverse>;

    /// TODO: concat_trivial -> range_trivial
    template <typename T>
    concept concat_trivial = meta::tuple_like<T> && meta::tuple_size<T> == 0;

    /* Get a `begin` or `rbegin` iterator over a container depending on the status of
    the `Dir` flag. */
    template <range_direction Dir, typename T>
    [[nodiscard]] constexpr auto range_begin(T& value)
        noexcept (
            (std::same_as<Dir, range_forward> && requires{{meta::begin(value)} noexcept;}) ||
            (std::same_as<Dir, range_reverse> && requires{{meta::rbegin(value)} noexcept;})
        )
        requires (
            (std::same_as<Dir, range_forward> && requires{{meta::begin(value)};}) ||
            (std::same_as<Dir, range_reverse> && requires{{meta::rbegin(value)};})
        )
    {
        if constexpr (std::same_as<Dir, range_forward>) {
            return meta::begin(value);
        } else {
            return meta::rbegin(value);
        }
    }

    /* Get an `end` or `rend` iterator over a container depending on the status of the
    `Dir` flag. */
    template <range_direction Dir, typename T>
    [[nodiscard]] constexpr auto range_end(T& value)
        noexcept (
            (std::same_as<Dir, range_forward> && requires{{meta::end(value)} noexcept;}) ||
            (std::same_as<Dir, range_reverse> && requires{{meta::rend(value)} noexcept;})
        )
        requires (
            (std::same_as<Dir, range_forward> && requires{{meta::end(value)};}) ||
            (std::same_as<Dir, range_reverse> && requires{{meta::rend(value)};})
        )
    {
        if constexpr (std::same_as<Dir, range_forward>) {
            return meta::end(value);
        } else {
            return meta::rend(value);
        }
    }

    /// TODO: eliminate concat_get() and just inline the visitable calls, including
    /// possibly as a member method of the subrange/iterator types.

    template <size_t I, typename T> requires (I < T::alternatives)
    constexpr size_t _concat_get = (!T::trivial && I % 2 == 1) ?
        0 :
        impl::visitable<const typename T::unique&>::alternatives::template index<
            const typename T::template arg_type<I / (1 + !T::trivial)>&
        >();

    /* Cast the internal union of a `join_subrange` or `join_iterator` to the
    alternative encoded at index `I`.  Note that if a separator is present at the
    current level, then odd indices will map to that separator, while even indices will
    map to an argument in the outer `join` signature or a yield type thereof. */
    template <size_t I, typename T> requires (I < T::alternatives)
    constexpr decltype(auto) concat_get(T& self)
        noexcept (I < T::unique_alternatives ?
            requires{{impl::visitable<decltype((*self.child))>::template get<_concat_get<I, T>>(
                *self.child
            )} noexcept;} :
            requires{{impl::visitable<decltype((*self.child))>::template get<_concat_get<I, T>>(
                *self.child
            )};}
        )
    {
        return (impl::visitable<decltype((*self.child))>::template get<_concat_get<I, T>>(
            *self.child
        ));
    }

    /* Dereferencing a join iterator does not depend on the exact index, just the
    unique alternative. */
    template <typename T>
    struct concat_deref {
        template <size_t I>
        struct fn {
            template <typename U>
            static constexpr T operator()(const U& u)
                noexcept (requires{
                    {impl::visitable<const U&>::template get<I>(u).template deref<T>()} noexcept;
                })
            {
                return impl::visitable<const U&>::template get<I>(u).template deref<T>();
            }
        };
    };

    /* Comparing two join iterators first compares their indices, and only invokes the
    vtable function if they happen to match, meaning that it can get away with using
    unique alternatives, and forcing the argument types to match exactly. */
    template <size_t I>
    struct concat_compare {
        template <typename U>
        static constexpr std::strong_ordering operator()(const U& lhs, const U& rhs)
            noexcept (requires{{impl::visitable<const U&>::template get<I>(lhs).compare(
                impl::visitable<const U&>::template get<I>(rhs)
            )} noexcept;})
        {
            return impl::visitable<const U&>::template get<I>(lhs).compare(
                impl::visitable<const U&>::template get<I>(rhs)
            );
        }
    };

    template <size_t I>
    struct concat_increment {
        template <bool trivial>
        static constexpr size_t round = trivial ? I + 1 : (I | 1) + 1;
        template <bool trivial>
        static constexpr size_t norm = round<trivial> / (1 + !trivial);

        template <typename T>
        static constexpr void skip(T& self)
            noexcept (round<T::trivial> < T::alternatives ?
                requires(decltype(self.template arg<norm<T::trivial>>()) next) {
                    {next.begin != next.end} noexcept -> meta::nothrow::truthy;
                    {self.child = self.template arg<norm<T::trivial>>()} noexcept;
                    {concat_increment<round<T::trivial>>::skip(self)} noexcept;
                } :
                requires{{self.child = None} noexcept;}
            )
        {
            if constexpr (round<T::trivial> < T::alternatives) {
                self.index = round<T::trivial>;
                if (
                    auto next = self.template arg<norm<T::trivial>>();
                    next.begin != next.end
                ) {
                    self.child = std::move(next);
                } else {
                    concat_increment<round<T::trivial>>::skip(self);
                }
            } else {
                self.index = T::alternatives;
                self.child = None;
            }
        }

        template <typename T>
        static constexpr bool operator()(T& self)
            noexcept (requires{
                {concat_get<I>(self).increment()} noexcept;
                {skip(self)} noexcept;
            } && (T::trivial || I % 2 != 0 || I + 1 >= T::alternatives || requires{
                {self.child = self.sep()} noexcept;
            }))
        {
            if (concat_get<I>(self).increment()) {
                return true;
            }
            ++self.index;
            if constexpr (!T::trivial && I % 2 == 0 && I + 1 < T::alternatives) {
                if (self.outer->sep_size != 0) {
                    self.child = self.sep();
                    return true;
                }
                ++self.index;
            }
            skip(self);
            return self.child != None;
        }

        /// TODO: random-access increment with mutable `n`, which works mostly the
        /// same way as above, but needs to linearly scan over the intermediate
        /// subranges until `n` is exhausted.  Separators can be skipped over as long
        /// as `n` is greater than the separator size at that level.

        // template <typename T, typename Outer>
        // static constexpr bool operator()(T& self, ssize_t& n)
        //     noexcept (requires{{concat_get<I>(self).increment(n)} noexcept;})
        // {
        //     if (concat_get<I>(self).increment(n)) {
        //         return true;
        //     }
        //     /// TODO: similar to above, but lock that in before proceeding.  I may need
        //     /// to recursively call future specializations of `join_increment` in order
        //     /// to implement this.
        // }
    };

    template <size_t I>
    struct concat_decrement {
        /// TODO: implement this similar to concat_increment
    };

    /// TODO: remember that distance needs to encode both indices using a cartesian
    /// product, so that I can determine which subranges to sum over.

    template <size_t I>
    struct concat_distance {
        /// TODO: implement this similar to join_distance
    };

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

    template <typename T>
    struct _concat_wrap { using type = iter::range<impl::scalar_range<T>>; };
    template <meta::range T>
    struct _concat_wrap<T> { using type = T; };
    template <typename T>
    using concat_wrap = _concat_wrap<T>::type;

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

        template <typename Parent, range_direction Position>
        [[nodiscard]] constexpr concat_subrange(Parent& p, Position pos)
            noexcept (requires{
                {range_begin<Dir>(p)} noexcept -> meta::nothrow::convertible_to<Begin>;
                {range_end<Dir>(p)} noexcept -> meta::nothrow::convertible_to<End>;
                {pos(p, begin, end)} noexcept;
            })
        :
            begin(range_begin<Dir>(p)),
            end(range_end<Dir>(p)),
            index(pos(p, begin, end))
        {}

        template <typename T>
        [[nodiscard]] constexpr T deref() const
            noexcept (requires{{*begin} noexcept -> meta::nothrow::convertible_to<T>;})
            requires (requires{{*begin} -> meta::convertible_to<T>;})
        {
            return *begin;
        }

        constexpr bool increment()
            noexcept (requires{
                {++begin} noexcept;
                {begin != end} noexcept -> meta::nothrow::truthy;
            })
        {
            ++begin;
            ++index;
            return bool(begin != end);
        }

        constexpr bool increment(ssize_t& n)
            noexcept (requires{
                {end - begin} noexcept -> meta::nothrow::convertible_to<ssize_t>;
                {begin += n} noexcept;
            })
            requires (requires{
                {end - begin} -> meta::convertible_to<ssize_t>;
                {begin += n};
            })
        {
            ssize_t remaining = end - begin;
            if (n < remaining) {
                begin += n;
                index += n;
                n = 0;
                return true;
            }
            begin += remaining;
            index += remaining;
            n -= remaining;
            return false;
        }

        constexpr bool decrement()
            noexcept (requires{{--begin} noexcept;})
            requires (requires{{--begin};})
        {
            --begin;
            --index;
            return index >= 0;
        }

        constexpr bool decrement(ssize_t& n)
            noexcept (requires{{begin -= n} noexcept;})
            requires (requires{{begin -= n};})
        {
            if (n < index) {
                begin -= n;
                index -= n;
                n = 0;
                return true;
            }
            begin -= index;
            n -= index;
            index = 0;
            return false;
        }

        [[nodiscard]] constexpr ssize_t distance(const concat_subrange& other) const noexcept {
            return index - other.index;
        }

        [[nodiscard]] constexpr auto compare(const concat_subrange& other) const
            noexcept (requires{{index <=> other.index} noexcept;})
            requires (requires{{index <=> other.index};})
        {
            return index <=> other.index;
        }
    };

    template <meta::not_reference Outer, range_direction Dir>
    struct concat_iterator {
        static constexpr bool trivial = requires(Outer& outer) {
            {*outer.sep} -> concat_trivial;
        };

        template <size_t I> requires (I < Outer::arg_type::size())
        using arg_type = concat_subrange<
            Dir,
            decltype(range_begin<Dir>(std::declval<Outer&>().args.template get<I>())),
            decltype(range_end<Dir>(std::declval<Outer&>().args.template get<I>()))
        >;

    private:
        template <typename = std::make_index_sequence<Outer::arg_type::size()>>
        struct _unique;
        template <size_t... I>
        struct _unique<std::index_sequence<I...>> {
            using separator = void;
            using unique = meta::make_union<arg_type<I>...>;
        };
        template <size_t... I> requires (!trivial)
        struct _unique<std::index_sequence<I...>> {
            using separator = concat_subrange<
                Dir,
                decltype(range_begin<Dir>(*std::declval<Outer&>().sep)),
                decltype(range_end<Dir>(*std::declval<Outer&>().sep))
            >;
            using unique = meta::make_union<separator, arg_type<I>...>;
        };

    public:
        using separator = _unique<>::separator;
        using unique = _unique<>::unique;
        static constexpr size_t alternatives =
            Outer::arg_type::size() + (Outer::arg_type::size() - 1) * !trivial;
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

        Outer* outer;
        ssize_t index;
        [[no_unique_address]] Optional<unique> child;

        template <range_direction D = range_forward>
        [[nodiscard]] constexpr separator sep()
            noexcept (requires{{separator{*outer->sep, D{}}} noexcept;})
            requires (!trivial)
        {
            return {*outer->sep, D{}};
        }

        template <size_t I, range_direction D = range_forward>
        [[nodiscard]] constexpr arg_type<I> arg()
            noexcept (requires{{arg_type<I>{outer->args.template get<I>(), D{}}} noexcept;})
            requires (I < Outer::arg_type::size())
        {
            return {outer->args.template get<I>(), D{}};
        }

    private:
        template <ssize_t I = 0> requires (I < Outer::arg_type::ssize())
        constexpr Optional<unique> init()
            noexcept (requires(decltype(arg<I>()) first) {
                {arg<I>()} noexcept -> meta::nothrow::convertible_to<Optional<unique>>;
                {first.begin != first.end} noexcept -> meta::nothrow::truthy;
            } && (I + 1 == Outer::arg_type::ssize() || (
                requires{{init<I + 1>()} noexcept;} &&
                (trivial || I != 0 || requires{
                    {sep()} noexcept -> meta::nothrow::convertible_to<Optional<unique>>;
                })
            )))
            requires (std::same_as<Dir, range_forward>)
        {
            if (auto first = arg<I>(); first.begin != first.end) {
                return std::move(first);
            }
            if constexpr (I + 1 < Outer::arg_type::ssize()) {
                if constexpr (!trivial && I == 0) {
                    ++index;
                    if (outer->sep_size != 0) {
                        return sep();
                    }
                    ++index;
                } else {
                    index += 1 + !trivial;
                }
                return init<I + 1>();
            } else {
                ++index;
                return None;
            }
        }

        template <ssize_t I = Outer::arg_type::ssize() - 1> requires (I >= 0)
        constexpr Optional<unique> init()
            noexcept (requires(decltype(arg<I>()) first) {
                {arg<I>()} noexcept -> meta::nothrow::convertible_to<Optional<unique>>;
                {first.begin != first.end} noexcept -> meta::nothrow::truthy;
            } && (I == 0 || (
                requires{{init<I - 1>()} noexcept;} &&
                (trivial || I != Outer::arg_type::ssize() - 1 || requires{
                    {sep()} noexcept -> meta::nothrow::convertible_to<Optional<unique>>;
                })
            )))
            requires (std::same_as<Dir, range_reverse>)
        {
            if (auto first = arg<I>(); first.begin != first.end) {
                return std::move(first);
            }
            if constexpr (I > 0) {
                if constexpr (!trivial && I == Outer::arg_type::ssize() - 1) {
                    ++index;
                    if (outer->sep_size != 0) {
                        return sep();
                    }
                    ++index;
                } else {
                    index += 1 + !trivial;
                }
                return init<I - 1>();
            } else {
                ++index;
                return None;
            }
        }

        static constexpr decltype(auto) _deref(const unique& child)
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

        static constexpr std::strong_ordering _compare(const unique& lhs, const unique& rhs)
            noexcept (requires{{impl::basic_vtable<concat_compare, unique_alternatives>{
                impl::visitable<const unique&>::index(lhs)
            }(lhs, rhs)} noexcept;})
        {
            return impl::basic_vtable<concat_compare, unique_alternatives>{
                impl::visitable<const unique&>::index(lhs)
            }(lhs, rhs);
        }

    public:
        [[nodiscard]] constexpr concat_iterator(Outer* outer = nullptr) noexcept :
            outer(outer),
            index(alternatives)
        {}

        [[nodiscard]] constexpr concat_iterator(Outer& outer)
            noexcept (requires{{init()} noexcept;})
            requires (requires{{init()};})
        :
            outer(&outer),
            index(0),
            child(init())
        {}

        [[nodiscard]] constexpr decltype(auto) operator*() const
            noexcept (requires{{_deref(*child)} noexcept;})
        {
            return (_deref(*child));
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
            return (*tmp);
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
            noexcept (requires{{_compare(*child, *other.child)} noexcept;})
        {
            if (std::strong_ordering cmp = index <=> other.index; cmp != 0) return cmp;
            if (index < 0 || index >= ssize_t(alternatives)) return std::strong_ordering::equal;
            return _compare(*child, *other.child);
        }
    };

    template <meta::not_rvalue Sep, meta::not_rvalue... A> requires (sizeof...(A) > 1)
    struct concat {
        using sep_type = concat_wrap<Sep>;
        using arg_type = impl::basic_tuple<concat_wrap<A>...>;
        using sep_size_type = ssize_t;

        [[no_unique_address]] impl::ref<sep_type> sep;
        [[no_unique_address]] arg_type args;
        [[no_unique_address]] sep_size_type sep_size;

    private:
        constexpr ssize_t get_sep_size()
            noexcept (!meta::range<Sep> || requires{{ssize_t(meta::distance(*sep))} noexcept;})
            requires (!meta::range<Sep> || requires{{ssize_t(meta::distance(*sep))};})
        {
            if constexpr (meta::range<Sep>) {
                return ssize_t(meta::distance(*sep));
            } else {
                return 1;
            }
        }

    public:
        [[nodiscard]] constexpr concat(meta::forward<Sep> sep, meta::forward<A>... args)
            noexcept (requires{
                {sep_type(std::forward<Sep>(sep))} noexcept;
                {arg_type{std::forward<A>(args)...}} noexcept;
                {get_sep_size()} noexcept;
            })
            requires (requires{
                {sep_type(std::forward<Sep>(sep))};
                {arg_type{std::forward<A>(args)...}};
                {get_sep_size()};
            })
        :
            sep(std::forward<Sep>(sep)),
            args{std::forward<A>(args)...},
            sep_size(get_sep_size())
        {}

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

        [[nodiscard]] constexpr concat() = default;
        [[nodiscard]] constexpr concat(meta::forward<Sep> sep)
            noexcept (requires{{Sep(std::forward<Sep>(sep))} noexcept;})
            requires (requires{{Sep(std::forward<Sep>(sep))};})
        :
            sep(std::forward<Sep>(sep))
        {}

        [[nodiscard]] static constexpr range<> operator()() noexcept {
            return {};
        }

        template <typename A>
        [[nodiscard]] static constexpr decltype(auto) operator()(A&& a)
            noexcept (meta::range<A> || requires{{iter::range(std::forward<A>(a))} noexcept;})
            requires (meta::range<A> || requires{{iter::range(std::forward<A>(a))};})
        {
            if constexpr (meta::range<A>) {
                return (std::forward<A>(a));
            } else {
                return iter::range(std::forward<A>(a));
            }
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



#endif  // BERTRAND_ITER_CONCAT_H