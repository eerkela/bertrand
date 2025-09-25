#ifndef BERTRAND_ITER_FIND_H
#define BERTRAND_ITER_FIND_H

#include "bertrand/iter/range.h"


namespace bertrand {


/// TODO: docs need to be rewritten to reflect `find{}` returning a range of
/// ranges, and `.front()` returning the first such range for convenience.
/// `.front().front()` would then return the first element of the first matching
/// subrange, which is the common case for single-value searches, and can be
/// abstracted into a `.value()` method that may be available on the range type
/// and recursively calls `.front()` for all nested ranges.  That way,
/// `find{v}(c).value()` would return the actual value that was searched, which
/// becomes the common case for scalar searches.  `value()` could also return
/// an optional if the base case for `range()` is defined that way.


namespace impl {

    template <meta::lvalue T>
    struct find_iterator {
        using indices = meta::unqualify<T>::indices;
        using container = decltype((*std::declval<T>().container));
        using iterator = meta::begin_type<container>;
        using sentinel = meta::end_type<container>;
        using subrange_type = subrange<iterator, sentinel>;
        using iterator_category = std::forward_iterator_tag;
        using difference_type = subrange_type::difference_type;
        struct value_type : impl::subrange<iterator, meta::as_unsigned<difference_type>> {
            difference_type offset;
            constexpr auto operator->() const = delete;
            [[nodiscard]] constexpr difference_type index() const noexcept {
                return
                    impl::subrange<iterator, meta::as_unsigned<difference_type>>::index() +
                    offset;
            }
        };
        using reference = value_type&;
        using pointer = value_type*;

    private:
        [[no_unique_address]] meta::as_pointer<T> ptr = nullptr;
        [[no_unique_address]] subrange_type subrange;
        [[no_unique_address]] iterator next = subrange.start();
        [[no_unique_address]] difference_type idx = 0;

        template <typename V> requires (!meta::range<V>)
        constexpr bool match(const V& value)
            noexcept (requires{
                {subrange.front() == value} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {next = subrange.start()} noexcept;
                {++next} noexcept;
                {idx = subrange.index() + 1} noexcept;
            })
            requires (requires{
                {subrange.front() == value} -> meta::explicitly_convertible_to<bool>;
                {next = subrange.start()};
                {++next};
                {idx = subrange.index() + 1};
            })
        {
            if (subrange.front() == value) {
                next = subrange.start();
                idx = subrange.index();
                do {
                    ++next;
                    ++idx;
                } while (next != subrange.stop() && *next == value);
                return true;
            }
            return false;
        }

        template <typename V> requires (!meta::range<V>)
        constexpr bool match(const V& value)
            noexcept (requires{
                {value(subrange.front())} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {next = subrange.start()} noexcept;
                {++next} noexcept;
                {idx = subrange.index() + 1} noexcept;
            })
            requires (
                !requires{{subrange.front() == value} -> meta::explicitly_convertible_to<bool>;} &&
                requires{
                    {value(subrange.front())} -> meta::explicitly_convertible_to<bool>;
                    {next = subrange.start()};
                    {++next};
                    {idx = subrange.index() + 1};
                }
            )
        {
            if (value(subrange.front())) {
                next = subrange.start();
                idx = subrange.index();
                do {
                    ++next;
                    ++idx;
                } while (next != subrange.stop() && value(*next));
                return true;
            }
            return false;
        }

        template <meta::range V>
        constexpr bool match(const V& value)
            noexcept (requires(decltype(value.begin()) it, decltype(value.end()) end) {
                {!subrange.empty()} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {value.empty()} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {next = subrange.start()} noexcept;
                {++next} noexcept;
                {idx = subrange.index() + 1} noexcept;
                {value.begin()} noexcept;
                {value.end()} noexcept;
                {*next == *it} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {++it} noexcept;
                {++idx} noexcept;
                {it == end} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
                {next == subrange.stop()} noexcept -> meta::nothrow::explicitly_convertible_to<bool>;
            })
            requires (requires(decltype(value.begin()) it, decltype(value.end()) end) {
                {!subrange.empty()} -> meta::explicitly_convertible_to<bool>;
                {value.empty()} -> meta::explicitly_convertible_to<bool>;
                {next = subrange.start()};
                {++next};
                {idx = subrange.index() + 1};
                {value.begin()};
                {value.end()};
                {*next == *it} -> meta::explicitly_convertible_to<bool>;
                {++it};
                {++idx};
                {it == end} -> meta::explicitly_convertible_to<bool>;
                {next == subrange.stop()} -> meta::explicitly_convertible_to<bool>;
            })
        {
            if (!subrange.empty()) {
                if (value.empty()) {
                    next = subrange.start();
                    ++next;
                    idx = subrange.index() + 1;
                    return true;
                }
                next = subrange.start();
                idx = subrange.index();
                auto it = value.begin();
                auto end = value.end();
                while (*next == *it) {
                    ++it;
                    ++next;
                    ++idx;
                    if (it == end) {
                        return true;
                    }
                    if (next == subrange.stop()) {
                        break;
                    }
                }
            }
            return false;
        }

        template <size_t... Is>
        constexpr void advance(std::index_sequence<Is...>)
            noexcept (requires{
                {subrange.empty()} noexcept;
                {subrange.increment()} noexcept;
                {(match(ptr->args.template get<Is>()) || ...)} noexcept;
            })
            requires (requires{
                {subrange.empty()};
                {subrange.increment()};
                {(match(ptr->args.template get<Is>()) || ...)};
            })
        {
            while (!subrange.empty() && !(match(ptr->args.template get<Is>()) || ...)) {
                subrange.increment();
            }
        }

    public:
        [[nodiscard]] constexpr find_iterator() = default;
        [[nodiscard]] constexpr find_iterator(T self)
            noexcept (requires{
                {std::addressof(self)} noexcept;
                {subrange_type(
                    std::ranges::begin(*self.container),
                    std::ranges::end(*self.container)
                )} noexcept;
                {iterator(subrange.start())} noexcept;
                {difference_type(0)} noexcept;
                {advance(indices{})} noexcept;
            })
            requires (requires{
                {std::addressof(self)};
                {subrange_type(
                    std::ranges::begin(*self.container),
                    std::ranges::end(*self.container)
                )};
                {iterator(subrange.start())};
                {difference_type(0)};
                {advance(indices{})};
            })
        :
            ptr(std::addressof(self)),
            subrange(
                std::ranges::begin(*self.container),
                std::ranges::end(*self.container)
            ),
            next(subrange.start()),
            idx(0)
        {
            advance(indices{});
        }

        [[nodiscard]] constexpr value_type operator*() const
            noexcept (requires{{value_type{
                {
                    subrange.start(),
                    meta::to_unsigned(idx - subrange.index())
                },
                subrange.index()
            }} noexcept;})
            requires (requires{{value_type{
                {
                    subrange.start(),
                    meta::to_unsigned(idx - subrange.index())
                },
                subrange.index()
            }};})
        {
            return value_type{
                {
                    subrange.start(),
                    meta::to_unsigned(idx - subrange.index())
                },
                subrange.index()
            };
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow{**this}} noexcept;})
            requires (requires{{impl::arrow{**this}};})
        {
            return impl::arrow{**this};
        }

        constexpr find_iterator& operator++()
            noexcept (requires{
                {subrange.start() = next} noexcept;
                {subrange.index() = idx} noexcept;
                {advance(indices{})} noexcept;
            })
            requires (requires{
                {subrange.start() = next};
                {subrange.index() = idx};
                {advance(indices{})};
            })
        {
            subrange.start() = next;
            subrange.index() = idx;
            advance(indices{});
            return *this;
        }

        [[nodiscard]] constexpr find_iterator operator++(int)
            noexcept (requires(find_iterator tmp) {
                {find_iterator{*this}} noexcept;
                {++tmp} noexcept;
            })
            requires (requires(find_iterator tmp) {
                {find_iterator{*this}};
                {++tmp};
            })
        {
            find_iterator tmp = *this;
            ++*this;
            return tmp;
        }

        [[nodiscard]] friend constexpr bool operator<(const find_iterator& self, NoneType)
            noexcept (requires{{self.subrange < NoneType{}} noexcept;})
            requires (requires{{self.subrange < NoneType{}};})
        {
            return self.subrange < NoneType{};
        }

        [[nodiscard]] friend constexpr bool operator<(NoneType, const find_iterator& self)
            noexcept (requires{{NoneType{} < self.subrange} noexcept;})
            requires (requires{{NoneType{} < self.subrange};})
        {
            return NoneType{} < self.subrange;
        }

        [[nodiscard]] friend constexpr bool operator<=(const find_iterator& self, NoneType)
            noexcept (requires{{self.subrange <= NoneType{}} noexcept;})
            requires (requires{{self.subrange <= NoneType{}};})
        {
            return self.subrange <= NoneType{};
        }

        [[nodiscard]] friend constexpr bool operator<=(NoneType, const find_iterator& self)
            noexcept (requires{{NoneType{} <= self.subrange} noexcept;})
            requires (requires{{NoneType{} <= self.subrange};})
        {
            return NoneType{} <= self.subrange;
        }

        [[nodiscard]] friend constexpr bool operator==(const find_iterator& self, NoneType)
            noexcept (requires{{self.subrange == NoneType{}} noexcept;})
            requires (requires{{self.subrange == NoneType{}};})
        {
            return self.subrange == NoneType{};
        }

        [[nodiscard]] friend constexpr bool operator==(NoneType, const find_iterator& self)
            noexcept (requires{{NoneType{} == self.subrange} noexcept;})
            requires (requires{{NoneType{} == self.subrange};})
        {
            return NoneType{} == self.subrange;
        }

        [[nodiscard]] friend constexpr bool operator!=(const find_iterator& self, NoneType)
            noexcept (requires{{self.subrange != NoneType{}} noexcept;})
            requires (requires{{self.subrange != NoneType{}};})
        {
            return self.subrange != NoneType{};
        }

        [[nodiscard]] friend constexpr bool operator!=(NoneType, const find_iterator& self)
            noexcept (requires{{NoneType{} != self.subrange} noexcept;})
            requires (requires{{NoneType{} != self.subrange};})
        {
            return NoneType{} != self.subrange;
        }

        [[nodiscard]] friend constexpr bool operator>=(const find_iterator& self, NoneType)
            noexcept (requires{{self.subrange >= NoneType{}} noexcept;})
            requires (requires{{self.subrange >= NoneType{}};})
        {
            return self.subrange >= NoneType{};
        }

        [[nodiscard]] friend constexpr bool operator>=(NoneType, const find_iterator& self)
            noexcept (requires{{NoneType{} >= self.subrange} noexcept;})
            requires (requires{{NoneType{} >= self.subrange};})
        {
            return NoneType{} >= self.subrange;
        }

        [[nodiscard]] friend constexpr bool operator>(const find_iterator& self, NoneType)
            noexcept (requires{{self.subrange > NoneType{}} noexcept;})
            requires (requires{{self.subrange > NoneType{}};})
        {
            return self.subrange > NoneType{};
        }

        [[nodiscard]] friend constexpr bool operator>(NoneType, const find_iterator& self)
            noexcept (requires{{NoneType{} > self.subrange} noexcept;})
            requires (requires{{NoneType{} > self.subrange};})
        {
            return NoneType{} > self.subrange;
        }

        [[nodiscard]] constexpr bool operator==(const find_iterator& other) const
            noexcept (requires{{subrange == other.subrange} noexcept;})
            requires (requires{{subrange == other.subrange};})
        {
            return subrange == other.subrange;
        }

        [[nodiscard]] constexpr auto operator<=>(const find_iterator& other) const
            noexcept (requires{{subrange <=> other.subrange} noexcept;})
            requires (requires{{subrange <=> other.subrange};})
        {
            return subrange <=> other.subrange;
        }
    };

    /* The result of an `iter::find{A...}(C)` operation.  See that function for
    more information. */
    template <meta::not_rvalue C, meta::not_rvalue... A> requires (meta::iterable<C>)
    struct find {
        using indices = std::index_sequence_for<A...>;

        [[no_unique_address]] impl::ref<C> container;
        [[no_unique_address]] impl::basic_tuple<A...> args;

        constexpr void swap(find& other)
            noexcept (requires{
                {container.swap(other.container)} noexcept;
                {args.swap(other.args)} noexcept;
            })
            requires (requires{
                {container.swap(other.container)};
                {args.swap(other.args)};
            })
        {
            container.swap(other.container);
            args.swap(other.args);
        }

        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{find_iterator<const find&>(*this)} noexcept;})
            requires (requires{{find_iterator<const find&>(*this)};})
        {
            return find_iterator<const find&>(*this);
        }

        [[nodiscard]] static constexpr NoneType end() noexcept {
            return {};
        }

        [[nodiscard]] constexpr auto front() const
            noexcept (requires{{*begin()} noexcept;})
            requires (requires{{*begin()};})
        {
            return *begin();
        }
    };

    template <typename C, typename... A>
    find(C&&, impl::basic_tuple<A...>&&) -> find<meta::remove_rvalue<C>, meta::remove_rvalue<A>...>;

}


namespace iter {

    /* Search an iterable container for the first occurrence of a particular value or
    subsequence.

    The initializer may be either a single value (which will be linearly searched using
    the equality operator), a function predicate returning a value explicitly
    convertible to `bool`, or a flat range of values.  In the range case, the entire
    subsequence must be present in order for a match to be found, and the resulting
    subrange will start at the first element of the subsequence.  All subranges will
    be non-overlapping.

    If the container is supplied as an rvalue, then its lifetime will be extended to
    match that of the return value.

    This algorithm returns an `impl::find` object, which acts like an iterator to the
    first subrange matching at least one of the search conditions.  It exhibits the
    following behavior:

        1.  Dereferencing the `find` object yields the first value in the current
            subrange, allowing this function to be used intuitively for single-value
            lookups.  The `front()` method is equivalent to the dereference operator.
        2.  Comparisons against `None`, `nullopt`, `nullptr`, the `empty()` method, or
            contextual conversions to bool indicate whether a match was found.
        3.  `find` objects are always totally ordered with respect to one another, as
            long as they refer to the same container (otherwise the behavior is
            undefined).
        4.  The `subrange()` method can be used to access the current subrange, which
            begins at `start()` and ends at `stop()`.  The `stop()` value is always
            equivalent to the container's `end()` iterator.
        5.  `index()` can be used to determine the current index within the original
            container.
        6.  `data()` may be conditionally exposed if the container's iterators are
            contiguous, and always returns a pointer to `front()`.
        7.  Incrementing the `find` object will advance it to the next non-overlapping,
            matching subrange, as will the `increment()` member function.
        8.  Iterating over the `find` object yields the current `subrange()` followed
            by all subsequent non-overlapping, matching subranges.  Incrementing the
            iterator has the same effect as for the parent `find` object.
     */
    template <meta::not_rvalue... Ts>
    struct find {
        [[no_unique_address]] impl::basic_tuple<Ts...> v;

        [[nodiscard]] constexpr find() = default;
        [[nodiscard]] constexpr find(meta::forward<Ts>... k)
            noexcept (requires{{impl::basic_tuple<Ts...>{std::forward<Ts>(k)...}} noexcept;})
            requires (requires{{impl::basic_tuple<Ts...>{std::forward<Ts>(k)...}};})
        :
            v{std::forward<Ts>(k)...}
        {}

    private:
        template <typename Self, typename C, size_t... Is>
        constexpr decltype(auto) call(this Self&& self, C&& c, std::index_sequence<Is...>)
            noexcept (requires{{iter::range(iter::range(impl::find{
                std::forward<C>(c),
                impl::basic_tuple{std::forward<Self>(self).v.template get<Is>()...}
            }))} noexcept;})
            requires (requires{{iter::range(iter::range(impl::find{
                std::forward<C>(c),
                impl::basic_tuple{std::forward<Self>(self).v.template get<Is>()...}
            }))};})
        {
            return (iter::range(iter::range(impl::find{
                std::forward<C>(c),
                impl::basic_tuple{std::forward<Self>(self).v.template get<Is>()...}
            })));
        }

    public:
        template <typename Self, meta::iterable C>
        [[nodiscard]] constexpr decltype(auto) operator()(this Self&& self, C&& c)
            noexcept (requires{{std::forward<Self>(self).call(
                std::forward<C>(c),
                std::index_sequence_for<Ts...>{}
            )} noexcept;})
            requires (requires{{std::forward<Self>(self).call(
                std::forward<C>(c),
                std::index_sequence_for<Ts...>{}
            )};})
        {
            return (std::forward<Self>(self).call(
                std::forward<C>(c),
                std::index_sequence_for<Ts...>{}
            ));
        }
    };

    template <typename... Ts>
    find(Ts&&...) -> find<meta::remove_rvalue<Ts>...>;



    /// TODO: split{} is just the converse of find{}







    // static constexpr impl::find foo {
    //     .container = std::array{1, 2, 3},
    //     .args = impl::basic_tuple{2, 3}
    // };

    // static_assert([] {
    //     for (auto&& s : foo) {

    //     }

    //     return true;
    // }());


    static_assert([] {
        using sv = std::string_view;
        auto f = find{[](sv c) { return c == "b" || c == "c"; }}(
            std::array{sv{"a"}, sv{"b"}, sv{"c"}}
        );
        // auto f = find{"b", "c"}(
        //     std::array{sv{"a"}, sv{"b"}, sv{"c"}}
        // );
        if (f.value() != "b") return false;

        size_t i = 0;
        for (auto&& s : f) {
            if (s->front() != "b" && s->front() != "c") {
                return false;
            }
            for (auto&& v : s) {
                if (v != sv{"b"} && v != sv{"c"}) {
                    return false;
                }
            }
            ++i;
        }
        if (i != 1) return false;

        // auto r = f->front();
        // if (r[0] != "b") return false;

        // auto it = f->begin();
        // ++it;
        // if (it != f->end()) return false;


        // if (f == None) return false;

        // auto s = (f->subrange.start());
        // if (s != f->m_container.begin() + 1) return false;
        // if (s != sv{"b"}) return false;

        // if (f->front() != "b") return false;

        // for (auto&& s : f) {
        //     if (s != sv{"b"}) {
        //         return false;
        //     }
        // }

        // if (*f != sv{"b"}) return false;
        // if (f.index() != 1) return false;
        // ++f;
        // if (*f != sv{"c"}) return false;
        // if (f.index() != 2) return false;
        // if (f->size() != 1) return false;

        // for (auto&& s : f) {
        //     if (s.index() != 1 && s.index() != 2) {
        //         return false;
        //     }
        // }
        return true;
    }());

    // static_assert(find{"b"}(
    //     std::array<std::string_view, 3>{"a", "b", "c"}
    // )->size() == 1);


}


}


#endif  // BERTRAND_ITER_FIND_H