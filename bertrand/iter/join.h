#ifndef BERTRAND_ITER_JOIN_H
#define BERTRAND_ITER_JOIN_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/union.h"
#include "bertrand/iter/range.h"


namespace bertrand {


namespace impl {

    template <typename Sep, typename... A>
    concept join_concept =
        sizeof...(A) > 0 &&
        (meta::not_void<A> && ...) &&
        (meta::not_rvalue<Sep> && ... && meta::not_rvalue<A>);

    template <typename T>
    concept join_flatten = meta::range<T> && meta::range<meta::yield_type<T>>;

    enum class join_direction : uint8_t {
        FORWARD,
        REVERSE
    };

    template <typename T>
    constexpr bool _is_join_subrange = false;
    template <typename T>
    concept is_join_subrange = _is_join_subrange<meta::unqualify<T>>;
    template <typename T>
    concept is_join_nested = is_join_subrange<T> && join_flatten<
        meta::dereference_type<typename meta::unqualify<T>::begin_type>
    >;

    template <typename Sep, typename... Subranges>
    concept join_union_concept = sizeof...(Subranges) > 0 &&
        (meta::is_void<Sep> || (meta::unqualified<Sep> && is_join_subrange<Sep>)) &&
        ((meta::unqualified<Subranges> && is_join_subrange<Subranges>) && ...) &&
        (meta::is_void<Sep> || (Sep::direction == meta::unpack_type<0, Subranges...>::direction)) &&
        ((meta::unpack_type<0, Subranges...>::direction == Subranges::direction) && ...);

    template <typename Sep, typename... Subranges>
        requires (join_union_concept<Sep, Subranges...>)
    struct _join_union {
        using type = meta::union_type<Subranges...>;
        static constexpr bool trivial = meta::trivial_union<Subranges...>;
    };
    template <meta::not_void Sep, typename... Subranges>
        requires (join_union_concept<Sep, Subranges...>)
    struct _join_union<Sep, Subranges...> {
        using type = meta::union_type<Sep, Subranges...>;
        static constexpr bool trivial = meta::trivial_union<Sep, Subranges...>;
    };

    /* Join iterators and nested subranges store the active subrange in an internal
    union that filters out duplicates based on the begin and end types for each
    argument and separator.  If all share the same iterator types, then the union will
    be optimized away entirely, eliminating dispatch overhead.  If only some arguments
    share iterators, then the union size will be minimized on that basis, which
    increases the likelihood of dispatch optimizations.  The only downside is that the
    union's index no longer reflects the active argument being joined, which
    necessitates a separate tracking index. */
    template <typename Sep, typename... Subranges> requires (join_union_concept<Sep, Subranges...>)
    struct join_union {
        using type = _join_union<Sep, Subranges...>::type;
        static constexpr bool trivial = _join_union<Sep, Subranges...>::trivial;

        static constexpr size_t size() noexcept {
            return trivial ? 1 : visitable<type>::alternatives::size();
        }

        static constexpr ssize_t ssize() noexcept {
            return ssize_t(size());
        }

        [[no_unique_address]] Optional<type> data;

        /* Get the active index of the storage union.  Note that this does not
        necessarily correspond to the active argument in the joined range, due to
        duplicate subranges being filtered out.  Additionally, if a separator is
        present, then it will always be stored at index 0, followed by the unique
        subranges.  This index will be used to specialize any function template
        provided to `visit<F>()`. */
        constexpr size_t active() const noexcept {
            if constexpr (trivial) {
                return 0;
            } else {
                return data.__value.template get<1>().__value.index();
            }
        }

        /* Visit the inner union with a vtable function that accepts the `active()`
        index as a template parameter.  The function will then be called with the
        given arguments, and `get<I>()` can be used to safely access the inner
        subrange(s). */
        template <template <size_t> class F, typename... A>
        constexpr decltype(auto) visit(A&&... args) const
            noexcept (requires{
                {impl::basic_vtable<F, size()>{active()}(std::forward<A>(args)...)} noexcept;
            })
            requires (requires{
                {impl::basic_vtable<F, size()>{active()}(std::forward<A>(args)...)};
            })
        {
            return (impl::basic_vtable<F, size()>{active()})(std::forward<A>(args)...);
        }

        /* Access the current subrange as the indicated type, where `I` matches the
        semantics for `active()` and `visit()`. */
        template <size_t I, typename Self> requires (I < size())
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            if constexpr (trivial) {
                return (std::forward<Self>(self).data.__value.template get<1>());
            } else {
                return (std::forward<Self>(self).data.
                    __value.template get<1>().
                    __value.template get<I>()
                );
            }
        }
    };

    template <typename Sep, typename Begin, typename End>
    concept join_subrange_concept =
        (meta::is_void<Sep> || (meta::unqualified<Sep> && is_join_subrange<Sep>)) &&
        meta::unqualified<Begin> &&
        meta::unqualified<End> &&
        meta::iterator<Begin> &&
        meta::sentinel_for<End, Begin>;

    /* Join iterators have to store both the begin and end iterators for every argument
    and separator, so that they can smoothly transition from one to the next.  This
    class encapsulates those iterators for any non-range or non-nested value, and is
    used to specialize `join_union` for the outer iterator or nested subrange. */
    template <join_direction D, typename Sep, typename Begin, typename End>
        requires (join_subrange_concept<Sep, Begin, End>)
    struct join_subrange {
        using begin_type = Begin;
        using end_type = End;
        static constexpr join_direction direction = D;
        begin_type begin;
        end_type end;
        ssize_t index = 0;
    };

    template <join_direction D, typename Sep, typename Begin, typename End>
    constexpr bool _is_join_subrange<join_subrange<D, Sep, Begin, End>> = true;

    /* `join_forward` produces a `join_subrange` containing forward iterators over an
    arbitrary argument.  If the argument is not a range, then this will yield a trivial
    subrange over a single element.  Otherwise, if the argument is a range, then it
    will reuse the same iterators as the range itself.  If that range yields further
    ranges, then `join_forward` will advance the resulting subrange to the first
    non-empty element or separator before returning. */
    template <typename A>
    struct join_forward;
    template <typename A> requires (!meta::range<A>)
    struct join_forward<A> {
        using type = A;

        template <typename Sep>
        using subrange = join_subrange<
            join_direction::FORWARD,
            Sep,
            impl::contiguous_iterator<A>,
            impl::contiguous_iterator<A>
        >;

        A arg;

        template <typename Sep>
        constexpr auto begin()
            noexcept (requires{{subrange<Sep>{
                .begin = {std::addressof(arg)},
                .end = {std::addressof(arg) + 1}
            }} noexcept;})
            requires (requires{{subrange<Sep>{
                .begin = {std::addressof(arg)},
                .end = {std::addressof(arg) + 1}
            }};})
        {
            return subrange<Sep>{
                .begin = {std::addressof(arg)},
                .end = {std::addressof(arg) + 1}
            };
        }

        template <typename Sep>
        constexpr auto end()
            noexcept (requires{{subrange<Sep>{
                .begin = {std::addressof(arg) + 1},
                .m_end = {std::addressof(arg) + 1}
            }} noexcept;})
            requires (requires{{subrange<Sep>{
                .begin = {std::addressof(arg) + 1},
                .end = {std::addressof(arg) + 1}
            }};})
        {
            return subrange<Sep>{
                .begin = {std::addressof(arg) + 1},
                .end = {std::addressof(arg) + 1}
            };
        }
    };
    template <meta::range A>
    struct join_forward<A> {
        using type = join_forward<meta::yield_type<A>>::type;

        template <typename Sep>
        using subrange = join_subrange<
            join_direction::FORWARD,
            Sep,
            meta::unqualify<meta::begin_type<A>>,
            meta::unqualify<meta::end_type<A>>
        >;

        A arg;

        template <typename Sep>
        constexpr auto begin()
            noexcept (requires{{subrange<Sep>{
                .begin = arg.begin(),
                .end = arg.end()
            }} noexcept;})
            requires (requires{{subrange<Sep>{
                .begin = arg.begin(),
                .end = arg.end()
            }};})
        {
            return subrange<Sep>{
                .begin = arg.begin(),
                .end = arg.end()
            };
        }

        template <typename Sep>
        constexpr auto end()
            noexcept (requires{{subrange<Sep>{
                .begin = arg.end(),
                .end = arg.end()
            }} noexcept;})
            requires (requires{{subrange<Sep>{
                .begin = arg.end(),
                .end = arg.end()
            }};})
        {
            return subrange<Sep>{
                .begin = arg.end(),
                .end = arg.end()
            };
        }
    };
    template <typename A>
    join_forward(A&) -> join_forward<A&>;

    /* `join_reverse` works identically to `join_forward`, except that the subranges
    encapsulate reverse iterators rather than forward ones. */
    template <typename A>
    struct join_reverse;
    template <typename A> requires (!meta::range<A>)
    struct join_reverse<A> {
        using type = A;

        template <typename Sep>
        using subrange = join_subrange<
            join_direction::REVERSE,
            Sep,
            std::reverse_iterator<impl::contiguous_iterator<A>>,
            std::reverse_iterator<impl::contiguous_iterator<A>>
        >;

        A arg;

        template <typename Sep>
        constexpr auto begin()
            noexcept (requires{{subrange<Sep>{
                .begin = std::make_reverse_iterator(
                    impl::contiguous_iterator<A>{std::addressof(arg) + 1}
                ),
                .end = std::make_reverse_iterator(
                    impl::contiguous_iterator<A>{std::addressof(arg)}
                )
            }} noexcept;})
            requires (requires{{subrange<Sep>{
                .begin = std::make_reverse_iterator(
                    impl::contiguous_iterator<A>{std::addressof(arg) + 1}
                ),
                .end = std::make_reverse_iterator(
                    impl::contiguous_iterator<A>{std::addressof(arg)}
                )
            }};})
        {
            return subrange<Sep>{
                .begin = std::make_reverse_iterator(
                    impl::contiguous_iterator<A>{std::addressof(arg) + 1}
                ),
                .end = std::make_reverse_iterator(
                    impl::contiguous_iterator<A>{std::addressof(arg)}
                )
            };
        }

        template <typename Sep>
        constexpr auto end()
            noexcept (requires{{subrange<Sep>{
                .begin = std::make_reverse_iterator(
                    impl::contiguous_iterator<A>{std::addressof(arg)}
                ),
                .end = std::make_reverse_iterator(
                    impl::contiguous_iterator<A>{std::addressof(arg)}
                )
            }} noexcept;})
            requires (requires{{subrange<Sep>{
                .begin = std::make_reverse_iterator(
                    impl::contiguous_iterator<A>{std::addressof(arg)}
                ),
                .end = std::make_reverse_iterator(
                    impl::contiguous_iterator<A>{std::addressof(arg)}
                )
            }};})
        {
            return subrange<Sep>{
                .begin = std::make_reverse_iterator(
                    impl::contiguous_iterator<A>{std::addressof(arg)}
                ),
                .end = std::make_reverse_iterator(
                    impl::contiguous_iterator<A>{std::addressof(arg)}
                )
            };
        }
    };
    template <meta::range A>
        requires (meta::reverse_iterable<A> && (
            !meta::range<meta::reverse_yield_type<A>> ||
            meta::reverse_iterable<meta::reverse_yield_type<A>>
        ))
    struct join_reverse<A> {
        using type = join_reverse<meta::reverse_yield_type<A>>::type;

        template <typename Sep>
        using subrange = join_subrange<
            join_direction::REVERSE,
            Sep,
            meta::unqualify<meta::rbegin_type<A>>,
            meta::unqualify<meta::rend_type<A>>
        >;

        A arg;

        template <typename Sep>
        constexpr auto begin()
            noexcept (requires{{subrange<Sep>{
                .begin = arg.rbegin(),
                .end = arg.rend()
            }} noexcept;})
            requires (requires{{subrange<Sep>{
                .begin = arg.rbegin(),
                .end = arg.rend()
            }};})
        {
            return subrange<Sep>{
                .begin = arg.rbegin(),
                .end = arg.rend()
            };
        }

        template <typename Sep>
        constexpr auto end()
            noexcept (requires{{subrange<Sep>{
                .begin = arg.rend(),
                .end = arg.rend()
            }} noexcept;})
            requires (requires{{subrange<Sep>{
                .begin = arg.rend(),
                .end = arg.rend()
            }};})
        {
            return subrange<Sep>{
                .begin = arg.rend(),
                .end = arg.rend()
            };
        }
    };
    template <typename C>
    join_reverse(C&) -> join_reverse<C&>;

    template <join_direction D, typename Sep, typename Begin, typename End>
    struct _join_subrange {
        using type = join_subrange<
            join_direction::FORWARD,
            Sep,
            meta::unqualify<meta::begin_type<meta::dereference_type<Begin>>>,
            meta::unqualify<meta::end_type<meta::dereference_type<Begin>>>
        >;
    };
    template <meta::is_void Sep, typename Begin, typename End>
    struct _join_subrange<join_direction::REVERSE, Sep, Begin, End> {
        using type = join_subrange<
            join_direction::REVERSE,
            Sep,
            meta::unqualify<meta::rbegin_type<meta::dereference_type<Begin>>>,
            meta::unqualify<meta::rend_type<meta::dereference_type<Begin>>>
        >;
    };

    /* If a range of ranges is supplied as an argument or separator, then each of its
    recursive elements will be flattened into the output.  Such a subrange will consist
    of an outer iterator over the value at the current depth, as well as an inner
    iterator over its yield type, which will be cached within the subrange as long as
    it is needed.  The inner iterator is stored as another `join_subrange`, enabling
    recursion.  Additionally, if a separator is also present, then it will be inserted
    between each element of the outer iterator. */
    template <join_direction D, typename Sep, typename Begin, typename End>
        requires (
            join_subrange_concept<Sep, Begin, End> &&
            meta::range<meta::dereference_type<Begin>>
        )
    struct join_subrange<D, Sep, Begin, End> {
        using begin_type = Begin;
        using end_type = End;
        static constexpr join_direction direction = D;
        begin_type begin;
        end_type end;
        ssize_t index = 0;

        using separator_type = Sep;
        using storage_type =
            join_union<Sep, typename _join_subrange<direction, Sep, Begin, End>::type>;
        static constexpr bool has_sep = meta::not_void<Sep>;
        static constexpr size_t total_groups = 1 + has_sep;
        template <size_t G> requires (G < total_groups)
        static constexpr bool is_separator = G == 1;
        template <size_t G> requires (G < total_groups)
        static constexpr size_t normalize = G;

        [[no_unique_address]] Optional<meta::dereference_type<Begin>> data;
        [[no_unique_address]] storage_type subrange;

        /* Visit the subrange union with a vtable function that accepts
        `index % total_groups` as a template parameter.  This will always be either
        0 or 1, where 0 indicates a yielded subrange and 1 indicates a separator, if
        one is present.  The function will then be called with the outer subrange, and
        `get<G>()` can be used to safely access the nested type. */
        template <template <size_t> class F, typename... A>
        constexpr decltype(auto) visit(A&&... args) const
            noexcept (requires{{subrange.template visit<F>(std::forward<A>(args)...)} noexcept;})
            requires (requires{{subrange.template visit<F>(std::forward<A>(args)...)};})
        {
            return (subrange.template visit<F>(std::forward<A>(args)...));
        }

        /* Access the current subrange as the indicated type, where `G` is an index
        with the same semantics as `visit()`. */
        template <size_t G, typename Self> requires (G < total_groups)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            return std::forward<Self>(self).subrange.template get<G>();
        }

        /* Populate the `subrange` union with the group at index `G`.  The group can
        then be safely accessed via `get<G>()`. */
        template <size_t G, typename Iter> requires (!is_separator<G>)
        constexpr void init(const Iter& it)
            noexcept (requires{{data = *begin} noexcept;} && ((
                direction == join_direction::FORWARD && requires{
                    {subrange.data = join_forward{
                        data.__value.template get<1>()
                    }.template begin<Sep>()} noexcept;
                }
            ) || (
                direction == join_direction::REVERSE && requires{
                    {subrange.data = join_reverse{
                        data.__value.template get<1>()
                    }.template begin<Sep>()} noexcept;
                }
            )))
            requires (requires{{data = *begin};} && ((
                direction == join_direction::FORWARD && requires{
                    {subrange.data = join_forward{
                        data.__value.template get<1>()
                    }.template begin<Sep>()} noexcept;
                }
            ) || (
                direction == join_direction::REVERSE && requires{
                    {subrange.data = join_reverse{
                        data.__value.template get<1>()
                    }.template begin<Sep>()} noexcept;
                }
            )))
        {
            data = *begin;
            if constexpr (direction == join_direction::FORWARD) {
                subrange.data = join_forward{data.__value.template get<1>()}.template begin<Sep>();
            } else {
                subrange.data = join_reverse{data.__value.template get<1>()}.template begin<Sep>();
            }
        }

        /* Populate the `subrange` union with the group at index `G`.  The group can
        then be safely accessed via `get<G>()`. */
        template <size_t G, typename Iter> requires (is_separator<G>)
        constexpr void init(const Iter& it)
            noexcept ((direction == join_direction::FORWARD && requires{
                {subrange.data = join_forward{it.range->sep()}.template begin<Sep>()} noexcept;
            }) || (direction == join_direction::REVERSE && requires{
                {subrange.data = join_reverse{it.range->sep()}.template begin<Sep>()} noexcept;
            }))
            requires ((direction == join_direction::FORWARD && requires{
                {subrange.data = join_forward{it.range->sep()}.template begin<Sep>()};
            }) || (direction == join_direction::REVERSE && requires{
                {subrange.data = join_reverse{it.range->sep()}.template begin<Sep>()};
            }))
        {
            if constexpr (direction == join_direction::FORWARD) {
                subrange.data = join_forward{it.range->sep()}.template begin<Sep>();
            } else {
                subrange.data = join_reverse{it.range->sep()}.template begin<Sep>();
            }
        }
    };

    /* The yield type for the overall `join` range may need to be a union if any of the
    subrange yield types differ.  If a separator is provided, then its yield type must
    also be included in the union.  If all of the yield types are the same, then the
    union will collapse into a single type, and the `trivial` flag will be set to
    true. */
    template <typename R, join_direction D, typename>
    struct _join_yield_type;
    template <typename R, size_t... Is>
        requires (meta::is_void<typename meta::unqualify<R>::separator_type>)
    struct _join_yield_type<R, join_direction::FORWARD, std::index_sequence<Is...>> {
        using type = meta::union_type<
            typename join_forward<decltype((std::declval<R>().template arg<Is>()))>::type...
        >;
        static constexpr bool trivial = meta::trivial_union<
            typename join_forward<decltype((std::declval<R>().template arg<Is>()))>::type...
        >;
    };
    template <typename R, size_t... Is>
        requires (meta::not_void<typename meta::unqualify<R>::separator_type>)
    struct _join_yield_type<R, join_direction::FORWARD, std::index_sequence<Is...>> {
        using type = meta::union_type<
            typename join_forward<decltype((std::declval<R>().sep()))>::type,
            typename join_forward<decltype((std::declval<R>().template arg<Is>()))>::type...
        >;
        static constexpr bool trivial = meta::trivial_union<
            typename join_forward<decltype((std::declval<R>().sep()))>::type,
            typename join_forward<decltype((std::declval<R>().template arg<Is>()))>::type...
        >;
    };
    template <typename R, size_t... Is>
        requires (meta::is_void<typename meta::unqualify<R>::separator_type>)
    struct _join_yield_type<R, join_direction::REVERSE, std::index_sequence<Is...>> {
        using type = meta::union_type<
            typename join_reverse<decltype((std::declval<R>().template arg<Is>()))>::type...
        >;
        static constexpr bool trivial = meta::trivial_union<
            typename join_reverse<decltype((std::declval<R>().template arg<Is>()))>::type...
        >;
    };
    template <typename R, size_t... Is>
        requires (meta::not_void<typename meta::unqualify<R>::separator_type>)
    struct _join_yield_type<R, join_direction::REVERSE, std::index_sequence<Is...>> {
        using type = meta::union_type<
            typename join_reverse<decltype((std::declval<R>().sep()))>::type,
            typename join_reverse<decltype((std::declval<R>().template arg<Is>()))>::type...
        >;
        static constexpr bool trivial = meta::trivial_union<
            typename join_reverse<decltype((std::declval<R>().sep()))>::type,
            typename join_reverse<decltype((std::declval<R>().template arg<Is>()))>::type...
        >;
    };
    template <typename R, join_direction D>
    using join_yield_type = _join_yield_type<R, D, typename meta::unqualify<R>::indices>;

    template <typename R, typename Sep, typename... Subranges>
    concept join_iterator_concept =
        meta::lvalue<R> &&
        join_union_concept<Sep, Subranges...>;

    /* A simple tag that indicates to an iterator algorithm that a given group is
    currently uninitialized, and must be populated before the algorithm can
    continue. */
    struct join_fresh {};

    /* A simple enum that indicates the result of an increment or decrement operation,
    which bounds the recursion such that I never need to repeat comparisons. */
    enum class join_signal : uint8_t {
        GOOD,
        CONTINUE,
        BREAK
    };

    /* Join iterators work by storing a pointer to the joined range, an index recording
    the current sub-range, and a union of backing iterators, which store both the begin
    and end iterators for each range.  Scalars are promoted to ranges of length 1 for
    the purposes of this union.  When the iterator is incremented, the current begin
    iterator will be advanced, and if it equals the corresponding end iterator, the
    index will be incremented and the next range will be initialized, skipping any that
    are empty.  The same type will be used for both the begin and end iterators of the
    joined range so that it always models `common_range`.  End iterators in this case
    are simply represented as having an empty internal union and a group index equal to
    the number of arguments + number of separators in the outer range.

    Note that if a common type exists between all of the iterators, then the internal
    union will be optimized out, and the common iterator type will be stored directly,
    which avoids extra dispatching overhead.  Additionally, if all of the component
    ranges return a single type, then the overall joined iterator will dereference to
    that type directly.  Otherwise, it will return a union of the possible types across
    all of the ranges. */
    template <typename R, typename Sep, typename... Subranges>
        requires (join_iterator_concept<R, Sep, Subranges...>)
    struct join_iterator {
    private:
        using separator_type = Sep;
        using storage_type = join_union<Sep, Subranges...>;
        static constexpr join_direction direction = meta::unpack_type<0, Subranges...>::direction;
        using yield_type = join_yield_type<R, direction>::type;
        static constexpr bool has_sep = meta::not_void<Sep>;
        static constexpr size_t total_groups =
            sizeof...(Subranges) + (sizeof...(Subranges) - 1) * has_sep;
        template <size_t G> requires (G < total_groups)
        static constexpr bool is_separator = has_sep && (G % 2 == 1);
        template <size_t G> requires (G < total_groups)
        static constexpr size_t normalize = has_sep ? G / 2 : G;

    public:
        using iterator_category = range_category<
            typename meta::unqualify<R>::ranges,
            Subranges...
        >::type;
        using difference_type = range_difference<
            typename meta::unqualify<R>::ranges,
            Subranges...
        >::type;
        using value_type = meta::remove_reference<yield_type>;
        using reference = meta::as_lvalue<value_type>;
        using pointer = meta::address_type<value_type>;

    private:
        static constexpr bool bidirectional = meta::inherits<
            iterator_category,
            std::bidirectional_iterator_tag
        >;

        meta::as_pointer<R> range = nullptr;
        ssize_t index = total_groups;
        storage_type subrange;

        /* Visit the subrange union with a vtable function that accepts the current
        `index` as a template parameter.  The function will then be called with the
        iterator object, and `get<G>()` can be used to safely access the subrange. */
        template <template <size_t> class F, typename Self>
        constexpr decltype(auto) visit(this Self&& self)
            noexcept (requires{{impl::basic_vtable<F, total_groups>{size_t(self.index)}(
                std::forward<Self>(self)
            )} noexcept;})
            requires (requires{{impl::basic_vtable<F, total_groups>{size_t(self.index)}(
                std::forward<Self>(self)
            )};})
        {
            return (impl::basic_vtable<F, total_groups>{size_t(self.index)}(
                std::forward<Self>(self)
            ));
        }

        /* Access the current subrange as the indicated type, where `G` is an index
        with the same semantics as `visit()`. */
        template <size_t G, typename Self> requires (G < total_groups)
        constexpr decltype(auto) get(this Self&& self) noexcept {
            return (std::forward<Self>(self).subrange.template get<
                is_separator<G> ? 0 : normalize<G> + has_sep
            >());
        }

        /* Populate the `subrange` union with the group at index `G`.  The group can
        then be safely accessed via `get<G>()`. */
        template <size_t G> requires (!is_separator<G>)
        constexpr void init(const join_iterator& it)
            noexcept ((direction == join_direction::FORWARD && requires{
                {subrange.data = join_forward{
                    range->template arg<normalize<G>>()
                }.template begin<Sep>()} noexcept;
            }) || (direction == join_direction::REVERSE && requires{
                {subrange.data = join_reverse{
                    range->template arg<normalize<G>>()
                }.template begin<Sep>()} noexcept;
            }))
            requires ((direction == join_direction::FORWARD && requires{
                {subrange.data = join_forward{
                    range->template arg<normalize<G>>()
                }.template begin<Sep>()};
            }) || (direction == join_direction::REVERSE && requires{
                {subrange.data = join_reverse{
                    range->template arg<normalize<G>>()
                }.template begin<Sep>()};
            }))
        {
            if constexpr (direction == join_direction::FORWARD) {
                subrange.data = join_forward{
                    range->template arg<normalize<G>>()
                }.template begin<Sep>();
            } else {
                subrange.data = join_reverse{
                    range->template arg<normalize<G>>()
                }.template begin<Sep>();
            }
        }

        /* Populate the `subrange` union with the group at index `G`.  The group can
        then be safely accessed via `get<G>()`. */
        template <size_t G> requires (is_separator<G>)
        constexpr void init(const join_iterator& it)
            noexcept ((direction == join_direction::FORWARD && requires{
                {subrange.data = join_forward{range->sep()}.template begin<Sep>()} noexcept;
            }) || (direction == join_direction::REVERSE && requires{
                {subrange.data = join_reverse{range->sep()}.template begin<Sep>()} noexcept;
            }))
            requires ((direction == join_direction::FORWARD && requires{
                {subrange.data = join_forward{range->sep()}.template begin<Sep>()};
            }) || (direction == join_direction::REVERSE && requires{
                {subrange.data = join_reverse{range->sep()}.template begin<Sep>()};
            }))
        {
            if constexpr (direction == join_direction::FORWARD) {
                subrange.data = join_forward{range->sep()}.template begin<Sep>();
            } else {
                subrange.data = join_reverse{range->sep()}.template begin<Sep>();
            }
        }

        template <size_t I>
        struct deref {
            template <is_join_subrange S>
            static constexpr yield_type call(S&& s)
                noexcept (requires{{
                    *std::forward<S>(s).begin
                } noexcept -> meta::nothrow::convertible_to<yield_type>;})
                requires (requires{{
                    *std::forward<S>(s).begin
                } -> meta::convertible_to<yield_type>;})
            {
                return *std::forward<S>(s).begin;
            }

            template <is_join_nested S>
            static constexpr yield_type call(S&& s)
                noexcept (requires{{std::forward<S>(s).template visit<join_iterator::deref>(
                    std::forward<S>(s)
                )} noexcept;})
                requires (requires{{std::forward<S>(s).template visit<join_iterator::deref>(
                    std::forward<S>(s)
                )};})
            {
                return std::forward<S>(s).template visit<join_iterator::deref>(std::forward<S>(s));
            }

            template <typename P>
            [[nodiscard]] static constexpr yield_type operator()(P&& p)
                noexcept (requires{{call(std::forward<P>(p).subrange.template get<I>())} noexcept;})
                requires (requires{{call(std::forward<P>(p).subrange.template get<I>())};})
            {
                return call(std::forward<P>(p).subrange.template get<I>());
            }
        };

        template <size_t I>
        struct compare {
            template <is_join_subrange S>
            static constexpr std::strong_ordering call(const S& lhs, const S& rhs) noexcept {
                return lhs.index <=> rhs.index;
            }

            template <is_join_nested S>
            static constexpr std::strong_ordering call(const S& lhs, const S& rhs) noexcept {
                if (auto cmp = lhs.index <=> rhs.index; cmp != 0) return cmp;
                return lhs.template visit<join_iterator::compare>(lhs, rhs);
            }

            template <typename P>
            static constexpr std::strong_ordering operator()(const P& lhs, const P& rhs) noexcept {
                return call(lhs.subrange.template get<I>(), rhs.subrange.template get<I>());
            }
        };

        /// TODO: all this jump_forward/backward, increment/decrement stuff may need to
        /// pay closer attention to the index at each step, to ensure it is always
        /// valid.

        template <size_t G>
        struct jump_forward {
            template <typename S> requires (G < S::total_groups)
            static constexpr join_signal advance(const join_iterator& it, S& s)
                noexcept (requires{
                    {jump_forward<G>{}(it, s)} noexcept;
                } && (!is_join_nested<S> || S::template is_separator<G> || requires{
                    {++s.begin} noexcept;
                    {s.begin == s.end} noexcept -> meta::nothrow::convertible_to<bool>;
                }) && (G + 1 == S::total_groups || requires{
                    {jump_forward<G + 1>::advance(it, s)} noexcept;
                }))
                requires (requires{
                    {jump_forward<G>{}(it, s)};
                } && (!is_join_nested<S> || S::template is_separator<G> || requires{
                    {++s.begin};
                    {s.begin == s.end} -> meta::convertible_to<bool>;
                }) && (G + 1 == S::total_groups || requires{
                    {jump_forward<G + 1>::advance(it, s)};
                }))
            {
                // continue depth-first search within current group, which is guaranteed
                // to be fresh and initialized to the first element
                if (jump_forward<G>{}(it, s) == join_signal::GOOD) {
                    return join_signal::GOOD;
                }

                // increment index and inner iterator
                ++s.index;
                if constexpr (is_join_nested<S> && !S::template is_separator<G>) {
                    ++s.begin;
                    if (s.begin == s.end) {
                        return join_signal::BREAK;
                    }
                }

                // try again with next group until end of current iteration
                if constexpr (G + 1 == S::total_groups) {
                    return join_signal::CONTINUE;
                } else {
                    return jump_forward<G + 1>::advance(it, s);
                }
            }

            template <is_join_subrange S> requires (G < S::total_groups)
            static constexpr join_signal call(const join_iterator& it, S& s)
                noexcept (requires{
                    {s.begin == s.end} noexcept -> meta::nothrow::convertible_to<bool>;
                })
                requires (requires{
                    {s.begin == s.end} -> meta::convertible_to<bool>;
                })
            {
                return s.begin == s.end ? join_signal::CONTINUE : join_signal::GOOD;
            }

            template <is_join_nested S> requires (G < S::total_groups)
            static constexpr join_signal call(const join_iterator& it, S& s)
                noexcept (requires{
                    {s.begin != s.end} noexcept -> meta::nothrow::convertible_to<bool>;
                    {jump_forward<0>::advance(it, s)} noexcept;
                })
                requires (requires{
                    {s.begin != s.end} -> meta::convertible_to<bool>;
                    {jump_forward<0>::advance(it, s)};
                })
            {
                if (s.begin != s.end) {
                    while (true) {
                        join_signal r = jump_forward<0>::advance(it, s);
                        if (r == join_signal::GOOD) {
                            return r;
                        } else if (r == join_signal::BREAK) {
                            break;  // BREAK signal never escapes from `advance()`
                        }
                    }
                }
                return join_signal::CONTINUE;
            }

            template <typename P> requires (G < P::total_groups)
            static constexpr join_signal operator()(const join_iterator& it, P& p)
                noexcept (requires{
                    {p.template init<G>(it)} noexcept;
                    {call(it, p.template get<G>())} noexcept;
                })
                requires (requires{
                    {p.template init<G>(it)};
                    {call(it, p.template get<G>())};
                })
            {
                p.template init<G>(it);
                return call(it, p.template get<G>());
            }
        };

        template <size_t G>
        struct increment {
            template <is_join_subrange S> requires (G < S::total_groups)
            static constexpr join_signal call(const join_iterator& it, S& s)
                noexcept (requires{
                    {++s.begin} noexcept;
                    {s.begin != s.end} noexcept -> meta::nothrow::convertible_to<bool>;
                })
                requires (requires{
                    {++s.begin};
                    {s.begin != s.end} -> meta::convertible_to<bool>;
                })
            {
                ++s.begin;
                ++s.index;
                return s.begin == s.end ? join_signal::CONTINUE : join_signal::GOOD;
            }

            template <is_join_nested S> requires (G < S::total_groups)
            static constexpr join_signal call(const join_iterator& it, S& s)
                noexcept (requires{
                    {s.template visit<join_iterator::increment>(it, s)} noexcept;
                    {jump_forward<0>::advance(it, s)} noexcept;
                } && (G + 1 == S::total_groups || requires{
                    {jump_forward<G + 1>::advance(it, s)} noexcept;
                }))
                requires (requires{
                    {s.template visit<join_iterator::increment>(it, s)};
                    {jump_forward<0>::advance(it, s)};
                } && (G + 1 == S::total_groups || requires{
                    {jump_forward<G + 1>::advance(it, s)};
                }))
            {
                // recur for depth-first traversal
                if (s.template visit<join_iterator::increment>(it, s) == join_signal::GOOD) {
                    return join_signal::GOOD;
                }

                // if the inner range is exhausted, increment the outer range until we
                // find a non-empty inner range or exhaust the outer range
                join_signal r = join_signal::CONTINUE;
                if constexpr (G + 1 < S::total_groups) {
                    r = jump_forward<G + 1>::advance(it, s);
                }
                while (r != join_signal::BREAK) {
                    r = jump_forward<0>::advance(it, s);
                    if (r == join_signal::GOOD) {
                        return r;
                    }
                }

                // if we exhaust the outer range, backtrack to the previous level and
                // continue searching
                return join_signal::CONTINUE;
            }

            template <typename P> requires (G < P::total_groups)
            static constexpr join_signal operator()(const join_iterator& it, P& p)
                noexcept (requires{{call(it, p.template get<G>())} noexcept;})
                requires (requires{{call(it, p.template get<G>())};})
            {
                return call(it, p.template get<G>());
            }
        };

        template <size_t G>
        struct jump_backward {
            template <typename S> requires (G < S::total_groups)
            static constexpr join_signal advance(const join_iterator& it, S& s)
                noexcept (requires{
                    {jump_backward<G>{}(it, s)} noexcept;
                } && (!is_join_nested<S> || S::template is_separator<G> || requires{
                    {--s.begin} noexcept;
                }) && (G == 0 || requires{
                    {jump_backward<G - 1>::advance(it, s)} noexcept;
                }))
                requires (requires{
                    {jump_backward<G>{}(it, s)};
                } && (!is_join_nested<S> || S::template is_separator<G> || requires{
                    {--s.begin};
                }) && (G == 0 || requires{
                    {jump_backward<G - 1>::advance(it, s)};
                }))
            {
                // continue depth-first search within current group, which is guaranteed
                // to be fresh and initialized to the last element
                if (jump_backward<G>{}(it, s) == join_signal::GOOD) {
                    return join_signal::GOOD;
                }

                // decrement index and inner iterator
                --s.index;
                if constexpr (is_join_nested<S> && !S::template is_separator<G>) {
                    if (s.index < 0) {
                        return join_signal::BREAK;
                    }
                    --s.begin;
                }

                // try again with previous group until beginning of current iteration
                if constexpr (G == 0) {
                    return join_signal::CONTINUE;
                } else {
                    return jump_backward<G - 1>::advance(it, s);
                }
            }

            template <is_join_subrange S> requires (G < S::total_groups)
            static constexpr join_signal call(const join_iterator& it, S& s)
                noexcept (requires{
                    {s.begin == s.end} noexcept -> meta::nothrow::convertible_to<bool>;
                })
                requires (requires{
                    {s.begin == s.end} -> meta::convertible_to<bool>;
                })
            {
                return s.begin == s.end ? join_signal::CONTINUE : join_signal::GOOD;
            }

            template <is_join_nested S> requires (G < S::total_groups)
            static constexpr join_signal call(const join_iterator& it, S& s)
                noexcept (requires{
                    {s.begin != s.end} noexcept -> meta::nothrow::convertible_to<bool>;
                    {jump_backward<S::total_groups - 1 - S::has_sep>::advance(it, s)} noexcept;
                })
                requires (requires{
                    {s.begin != s.end} -> meta::convertible_to<bool>;
                    {jump_backward<S::total_groups - 1 - S::has_sep>::advance(it, s)};
                })
            {
                if (s.begin != s.end) {
                    while (true) {
                        // last iteration never includes final separator
                        join_signal r = jump_backward<S::total_groups - 1 - S::has_sep>::advance(
                            it,
                            s
                        );
                        if (r == join_signal::GOOD) {
                            return r;
                        } else if (r == join_signal::BREAK) {
                            break;  // BREAK signal never escapes from `advance()`
                        }
                    }
                }
                return join_signal::CONTINUE;
            }

            template <typename P> requires (G < P::total_groups)
            static constexpr join_signal operator()(const join_iterator& it, P& p)
                noexcept (requires(decltype((p.template get<G>())) s) {
                    {p.template init<G>(it)} noexcept;
                    {p.template get<G>()} noexcept;
                    {s.begin == s.end} noexcept -> meta::nothrow::convertible_to<bool>;
                    {call(it, p.template get<G>())} noexcept;
                } && (
                    requires(decltype((p.template get<G>())) s) {
                        {s.index = size_t(s.end - s.begin) - 1} noexcept;
                        {s.begin += s.index} noexcept;
                    } || (
                        !requires(decltype((p.template get<G>())) s) {
                            {s.index = size_t(s.end - s.begin) - 1};
                            {s.begin += s.index};
                        } && requires(decltype((p.template get<G>())) s) {
                            {s.begin} noexcept -> meta::nothrow::copyable;
                            {++s.begin} noexcept;
                        }
                    )
                ))
                requires (requires(decltype((p.template get<G>())) s) {
                    {p.template init<G>(it)};
                    {p.template get<G>()};
                    {s.begin == s.end} -> meta::convertible_to<bool>;
                    {call(it, p.template get<G>())};
                } && (
                    requires(decltype((p.template get<G>())) s) {
                        {s.index = size_t(s.end - s.begin) - 1};
                        {s.begin += s.index};
                    } || requires(decltype((p.template get<G>())) s) {
                        {s.begin} -> meta::copyable;
                        {++s.begin};
                    }
                ))
            {
                p.template init<G>(it);
                auto& s = p.template get<G>();
                if (s.begin == s.end) {
                    return join_signal::CONTINUE;
                }

                // initialize to last element in subrange
                using S = meta::unqualify<decltype(s)>;
                if constexpr (requires{
                    {s.index = size_t(s.end - s.begin) - 1};
                    {s.begin += s.index};
                }) {
                    s.index = size_t(s.end - s.begin) - 1;
                    s.begin += s.index;
                    if constexpr (is_join_nested<S>) {
                        s.index += (s.index - (s.index > 0)) * S::has_sep;
                    }
                } else {
                    auto next = s.begin;
                    ++next;
                    while (next != s.end) {
                        ++next;
                        ++s.begin;
                        if constexpr (is_join_nested<S>) {
                            s.index += S::total_groups;
                        } else {
                            ++s.index;
                        }
                    }
                    if constexpr (is_join_nested<S>) {
                        s.index -= S::has_sep && (s.index > 0);
                    }
                }

                return call(it, p.template get<G>());
            }
        };

        template <size_t G>
        struct decrement {
            template <is_join_subrange S> requires (G < S::total_groups)
            static constexpr join_signal call(const join_iterator& it, S& s)
                noexcept (requires{{--s.begin} noexcept;})
                requires (requires{{--s.begin};})
            {
                --s.index;
                if (s.index < 0) {
                    return join_signal::CONTINUE;
                }
                --s.begin;
                return join_signal::GOOD;
            }

            template <is_join_nested S> requires (G < S::total_groups)
            static constexpr join_signal call(const join_iterator& it, S& s)
                noexcept (requires{
                    {s.template visit<join_iterator::decrement>(it, s)} noexcept;
                    {jump_backward<S::total_groups - 1>::advance(it, s)} noexcept;
                } && (G > 0 || requires{
                    {jump_backward<G - 1>::advance(it, s)} noexcept;
                }))
                requires (requires{
                    {s.template visit<join_iterator::decrement>(it, s)};
                    {jump_backward<S::total_groups - 1>::advance(it, s)};
                } && (G > 0 || requires{
                    {jump_backward<G - 1>::advance(it, s)};
                }))
            {
                // recur for depth-first traversal
                if (s.template visit<join_iterator::decrement>(it, s) == join_signal::GOOD) {
                    return join_signal::GOOD;
                }

                // if the inner range is exhausted, decrement the outer range until we
                // find a non-empty inner range or exhaust the outer range
                join_signal r = join_signal::CONTINUE;
                if constexpr (G > 0) {
                    r = jump_backward<G - 1>::advance(it, s);
                }
                while (r != join_signal::BREAK) {
                    r = jump_backward<S::total_groups - 1>::advance(it, s);
                    if (r == join_signal::GOOD) {
                        return r;
                    }
                }

                // if we exhaust the outer range, backtrack to the previous level and
                // continue searching
                return join_signal::CONTINUE;

            }

            template <typename P> requires (G < P::total_groups)
            static constexpr join_signal operator()(const join_iterator& it, P& p)
                noexcept (requires{{call(it, p.template get<G>())} noexcept;})
                requires (requires{{call(it, p.template get<G>())};})
            {
                return call(it, p.template get<G>());
            }
        };

        template <size_t G>
        struct seek_forward {
            template <typename S> requires (G < S::total_groups)
            static constexpr join_signal advance(const join_iterator& it, difference_type& i, S& s)
                noexcept (requires{
                    {seek_forward<G>{}(it, s)} noexcept;
                } && (!is_join_nested<S> || S::template is_separator<G> || requires{
                    {++s.begin} noexcept;
                    {s.begin == s.end} noexcept -> meta::nothrow::convertible_to<bool>;
                }) && (G + 1 == S::total_groups || requires{
                    {seek_forward<G + 1>::advance(it, i, s)} noexcept;
                }))
                requires (requires{
                    {seek_forward<G>{}(it, s)};
                } && (!is_join_nested<S> || S::template is_separator<G> || requires{
                    {++s.begin};
                    {s.begin == s.end} -> meta::convertible_to<bool>;
                }) && (G + 1 == S::total_groups || requires{
                    {seek_forward<G + 1>::advance(it, i, s)};
                }))
            {
                // continue depth-first search within current group, which is guaranteed
                // to be fresh and initialized to the first element
                if (seek_forward<G>{}(it, i, s) == join_signal::GOOD) {
                    return join_signal::GOOD;
                }

                // increment index and inner iterator
                ++s.index;
                if constexpr (is_join_nested<S> && !S::template is_separator<G>) {
                    ++s.begin;
                    if (s.begin == s.end) {
                        return join_signal::BREAK;
                    }
                }

                // try again with next group until end of current iteration
                if constexpr (G + 1 == S::total_groups) {
                    return join_signal::CONTINUE;
                } else {
                    return seek_forward<G + 1>::advance(it, i, s);
                }
            }

            template <is_join_subrange S> requires (G < S::total_groups)
            static constexpr join_signal call(const join_iterator& it, difference_type& i, S& s)
                noexcept (requires{
                    {s.end - s.begin} noexcept -> meta::nothrow::convertible_to<difference_type>;
                    {s.begin += i} noexcept;
                })
                requires (requires{
                    {s.end - s.begin} -> std::convertible_to<difference_type>;
                    {s.begin += i};
                })
            {
                difference_type size = s.end - s.begin;
                if (i < size) {  // index falls within this innermost subrange
                    s.begin += i;
                    s.index += i;
                    return join_signal::GOOD;
                }
                i -= size;
                return join_signal::CONTINUE;
            }

            template <is_join_nested S> requires (G < S::total_groups)
            static constexpr join_signal call(const join_iterator& it, difference_type& i, S& s)
                noexcept (requires{
                    {s.begin != s.end} noexcept -> meta::nothrow::convertible_to<bool>;
                    {seek_forward<0>::advance(it, i, s)} noexcept;
                })
                requires (requires{
                    {s.begin != s.end} -> meta::convertible_to<bool>;
                    {seek_forward<0>::advance(it, i, s)};
                })
            {
                if (s.begin != s.end) {
                    while (true) {
                        join_signal r = seek_forward<0>::advance(it, i, s);
                        if (r == join_signal::GOOD) {
                            return r;
                        } else if (r == join_signal::BREAK) {
                            break;
                        }
                    }
                }
                return join_signal::CONTINUE;
            }

            template <typename P> requires (G < P::total_groups)
            static constexpr join_signal operator()(const join_iterator& it, difference_type& i, P& p)
                noexcept (requires{
                    {p.template init<G>(it)} noexcept;
                    {call(it, i, p.template get<G>())} noexcept;
                })
                requires (requires{
                    {p.template init<G>(it)};
                    {call(it, i, p.template get<G>())};
                })
            {
                p.template init<G>(it);
                return call(it, i, p.template get<G>());
            }
        };

        template <size_t G>
        struct iadd {
            template <is_join_subrange S> requires (G < S::total_groups)
            static constexpr join_signal call(const join_iterator& it, difference_type& i, S& s)
                noexcept (requires{
                    {s.end - s.begin} noexcept -> meta::nothrow::convertible_to<difference_type>;
                    {s.begin += i} noexcept;
                })
                requires (requires{
                    {s.end - s.begin} -> std::convertible_to<difference_type>;
                    {s.begin += i};
                })
            {
                difference_type size = s.end - s.begin;
                if (i < size) {  // index falls within this innermost subrange
                    s.begin += i;
                    s.index += i;
                    return join_signal::GOOD;
                }
                i -= size;
                return join_signal::CONTINUE;
            }

            template <is_join_nested S> requires (G < S::total_groups)
            static constexpr join_signal call(const join_iterator& it, difference_type& i, S& s)
                noexcept (requires{
                    {s.template visit<join_iterator::increment>(it, s)} noexcept;
                    {seek_forward<0>::advance(it, i, s)} noexcept;
                } && (G + 1 == S::total_groups || requires{
                    {seek_forward<G + 1>::advance(it, i, s)} noexcept;
                }))
                requires (requires{
                    {s.template visit<join_iterator::increment>(it, s)};
                    {seek_forward<0>::advance(it, i, s)};
                } && (G + 1 == S::total_groups || requires{
                    {seek_forward<G + 1>::advance(it, i, s)};
                }))
            {
                // recur for depth-first traversal
                if (s.template visit<join_iterator::iadd>(it, i, s) == join_signal::GOOD) {
                    return join_signal::GOOD;
                }

                // if the inner range is exhausted, increment the outer range until the
                // index falls within a future subrange or we exhaust the outer range
                join_signal r = join_signal::CONTINUE;
                if constexpr (G + 1 < S::total_groups) {
                    r = seek_forward<G + 1>::advance(it, i, s);
                }
                while (r != join_signal::BREAK) {
                    r = seek_forward<0>::advance(it, i, s);
                    if (r == join_signal::GOOD) {
                        return r;
                    }
                }

                // if we exhaust the outer range, backtrack to the previous level and
                // continue searching
                return join_signal::CONTINUE;
            }

            template <typename P> requires (G < P::total_groups)
            static constexpr join_signal operator()(const join_iterator& it, difference_type& i, P& p)
                noexcept (requires{{call(it, i, p.template get<G>())} noexcept;})
                requires (requires{{call(it, i, p.template get<G>())};})
            {
                return call(it, i, p.template get<G>());
            }
        };

        template <size_t G>
        struct seek_backward {
            template <typename S> requires (G < S::total_groups)
            static constexpr join_signal advance(const join_iterator& it, difference_type& i, S& s)
                noexcept (requires{
                    {seek_backward<G>{}(it, s)} noexcept;
                } && (!is_join_nested<S> || S::template is_separator<G> || requires{
                    {--s.begin} noexcept;
                }) && (G == 0 || requires{
                    {seek_backward<G - 1>::advance(it, i, s)} noexcept;
                }))
                requires (requires{
                    {seek_backward<G>{}(it, s)};
                } && (!is_join_nested<S> || S::template is_separator<G> || requires{
                    {--s.begin};
                }) && (G == 0 || requires{
                    {seek_backward<G - 1>::advance(it, i, s)};
                }))
            {
                // continue depth-first search within current group, which is guaranteed
                // to be fresh and initialized to the last element
                if (seek_backward<G>{}(it, i, s) == join_signal::GOOD) {
                    return join_signal::GOOD;
                }

                // decrement index and inner iterator
                --s.index;
                if constexpr (is_join_nested<S> && !S::template is_separator<G>) {
                    if (s.index < 0) {
                        return join_signal::BREAK;
                    }
                    --s.begin;
                }

                // try again with previous group until beginning of current iteration
                if constexpr (G == 0) {
                    return join_signal::CONTINUE;
                } else {
                    return seek_backward<G - 1>::advance(it, i, s);
                }
            }

            template <is_join_subrange S> requires (G < S::total_groups)
            static constexpr join_signal call(const join_iterator& it, difference_type& i, S& s)
                noexcept (requires{{s.begin -= i} noexcept;})
                requires (requires{{s.begin -= i};})
            {
                if (i <= s.index) {  // index falls within this innermost subrange
                    s.begin -= i;
                    s.index -= i;
                    return join_signal::GOOD;
                }
                i -= s.index;
                return join_signal::CONTINUE;
            }

            template <is_join_nested S> requires (G < S::total_groups)
            static constexpr join_signal call(const join_iterator& it, difference_type& i, S& s)
                noexcept (requires{
                    {s.begin != s.end} noexcept -> meta::nothrow::convertible_to<bool>;
                    {seek_backward<S::total_groups - 1 - S::has_sep>::advance(
                        it,
                        i,
                        s
                    )} noexcept;
                    {seek_backward<S::total_groups - 1>::advance(it, i, s)} noexcept;
                })
                requires (requires{
                    {s.begin != s.end} -> meta::convertible_to<bool>;
                    {seek_backward<S::total_groups - 1 - S::has_sep>::advance(
                        it,
                        i,
                        s
                    )};
                    {seek_backward<S::total_groups - 1>::advance(it, i, s)};
                })
            {
                if (s.begin != s.end) {
                    // last iteration never includes final separator
                    join_signal r = seek_backward<S::total_groups - 1 - S::has_sep>::advance(
                        it,
                        i,
                        s
                    );
                    if (r == join_signal::GOOD) {
                        return r;
                    }
                    while (r != join_signal::BREAK) {
                        r = seek_backward<S::total_groups - 1>::advance(it, i, s);
                        if (r == join_signal::GOOD) {
                            return r;
                        }
                    }
                }
                return join_signal::CONTINUE;
            }

            template <typename P> requires (G < P::total_groups)
            static constexpr join_signal operator()(const join_iterator& it, difference_type& i, P& p)
                noexcept (requires(decltype((p.template get<G>())) s) {
                    {p.template init<G>(it)} noexcept;
                    {p.template get<G>()} noexcept;
                    {s.begin == s.end} noexcept -> meta::nothrow::convertible_to<bool>;
                    {call(it, i, p.template get<G>())} noexcept;
                } && (
                    requires(decltype((p.template get<G>())) s) {
                        {s.index = size_t(s.end - s.begin) - 1} noexcept;
                        {s.begin += s.index} noexcept;
                    } || (
                        !requires(decltype((p.template get<G>())) s) {
                            {s.index = size_t(s.end - s.begin) - 1};
                            {s.begin += s.index};
                        } && requires(decltype((p.template get<G>())) s) {
                            {s.begin} noexcept -> meta::nothrow::copyable;
                            {++s.begin} noexcept;
                        }
                    )
                ))
                requires (requires(decltype((p.template get<G>())) s) {
                    {p.template init<G>(it)};
                    {p.template get<G>()};
                    {s.begin == s.end} -> meta::convertible_to<bool>;
                    {call(it, i, p.template get<G>())};
                } && (
                    requires(decltype((p.template get<G>())) s) {
                        {s.index = size_t(s.end - s.begin) - 1};
                        {s.begin += s.index};
                    } || requires(decltype((p.template get<G>())) s) {
                        {s.begin} -> meta::copyable;
                        {++s.begin};
                    }
                ))
            {
                p.template init<G>(it);
                auto& s = p.template get<G>();
                if (s.begin == s.end) {
                    return join_signal::CONTINUE;
                }

                // initialize to last element in subrange
                using S = meta::unqualify<decltype(s)>;
                if constexpr (requires{
                    {s.index = size_t(s.end - s.begin) - 1};
                    {s.begin += s.index};
                }) {
                    s.index = size_t(s.end - s.begin) - 1;
                    s.begin += s.index;
                    if constexpr (is_join_nested<S>) {
                        s.index += (s.index - (s.index > 0)) * S::has_sep;
                    }
                } else {
                    auto next = s.begin;
                    ++next;
                    while (next != s.end) {
                        ++next;
                        ++s.begin;
                        if constexpr (is_join_nested<S>) {
                            s.index += S::total_groups;
                        } else {
                            ++s.index;
                        }
                    }
                    if constexpr (is_join_nested<S>) {
                        s.index -= S::has_sep && (s.index > 0);
                    }
                }

                return call(it, i, p.template get<G>());
            }
        };

        template <size_t G>
        struct isub {
            template <is_join_subrange S> requires (G < S::total_groups)
            static constexpr join_signal call(const join_iterator& it, difference_type& i, S& s)
                noexcept (requires{{s.begin -= i} noexcept;})
                requires (requires{{s.begin -= i};})
            {
                if (i <= s.index) {  // index falls within this innermost subrange
                    s.begin -= i;
                    s.index -= i;
                    return join_signal::GOOD;
                }
                i -= s.index;
                return join_signal::CONTINUE;
            }

            template <is_join_nested S> requires (G < S::total_groups)
            static constexpr join_signal call(const join_iterator& it, difference_type& i, S& s)
                noexcept (requires{
                    {s.template visit<join_iterator::isub>(it, i, s)} noexcept;
                    {seek_backward<S::total_groups - 1>::advance(it, i, s)} noexcept;
                } && (G == 0 || requires{
                    {seek_backward<G - 1>::advance(it, i, s)} noexcept;
                }))
                requires (requires{
                    {s.template visit<join_iterator::isub>(it, i, s)};
                    {seek_backward<S::total_groups - 1>::advance(it, i, s)};
                } && (G == 0 || requires{
                    {seek_backward<G - 1>::advance(it, i, s)};
                }))
            {
                // recur for depth-first traversal
                if (s.template visit<join_iterator::isub>(it, i, s) == join_signal::GOOD) {
                    return join_signal::GOOD;
                }

                // if the inner range is exhausted, decrement the outer range until the
                // index falls within a previous subrange or we exhaust the outer range
                join_signal r = join_signal::CONTINUE;
                if constexpr (G > 0) {
                    r = seek_backward<G - 1>::advance(it, i, s);
                }
                while (r != join_signal::BREAK) {
                    r = seek_backward<S::total_groups - 1>::advance(it, i, s);
                    if (r == join_signal::GOOD) {
                        return r;
                    }
                }

                // if we exhaust the outer range, backtrack to the previous level and
                // continue searching
                return join_signal::CONTINUE;
            }

            template <typename P> requires (G < P::total_groups)
            static constexpr join_signal operator()(const join_iterator& it, difference_type& i, P& p)
                noexcept (requires{{call(it, i, p.template get<G>())} noexcept;})
                requires (requires{{call(it, i, p.template get<G>())};})
            {
                return call(it, i, p.template get<G>());
            }
        };

        /// TODO: proofread the `count` and `distance` implementations

        template <size_t G>
        struct count {
            template <is_join_nested S>
            static constexpr join_signal middle(
                difference_type& sum,
                const join_iterator& it,
                S& s
            )
                noexcept (requires(difference_type sum) {
                    {sum += count<G>{}(join_fresh{}, it, s)} noexcept;
                } && (S::template is_separator<G> || requires{
                    {++s.begin} noexcept;
                    {s.begin == s.end} noexcept -> meta::nothrow::convertible_to<bool>;
                }) && (G + 1 == S::total_groups || requires{
                    {count<G + 1>::middle(sum, it, s)} noexcept;
                }))
                requires (requires(difference_type sum) {
                    {sum += count<G>{}(join_fresh{}, it, s)};
                } && (S::template is_separator<G> || requires{
                    {++s.begin};
                    {s.begin == s.end} -> meta::convertible_to<bool>;
                }) && (G + 1 == S::total_groups || requires{
                    {count<G + 1>::middle(sum, it, s)};
                }))
            {
                sum += count<G>{}(join_fresh{}, it, s);
                ++s.index;
                if constexpr (!S::template is_separator<G>) {
                    ++s.begin;
                    if (s.begin == s.end) {
                        return join_signal::BREAK;
                    }
                }
                if constexpr (G + 1 == S::total_groups) {
                    return join_signal::CONTINUE;
                } else {
                    return count<G + 1>::middle(sum, it, s);
                }
            }

            template <is_join_nested S>
            static constexpr join_signal middle(
                difference_type& sum,
                const join_iterator& it,
                S& s,
                const S& end
            )
                noexcept (requires(difference_type sum) {
                    {sum += count<G>{}(join_fresh{}, it, s)} noexcept;
                } && (S::template is_separator<G> || requires{
                    {++s.begin} noexcept;
                }) && (G + 1 == S::total_groups || requires{
                    {count<G + 1>::middle(sum, it, s, end)} noexcept;
                }))
                requires (requires(difference_type sum) {
                    {sum += count<G>{}(join_fresh{}, it, s)};
                } && (S::template is_separator<G> || requires{
                    {++s.begin};
                }) && (G + 1 == S::total_groups || requires{
                    {count<G + 1>::middle(sum, it, s, end)};
                }))
            {
                if (s.index == end.index) {
                    return join_signal::BREAK;
                }
                sum += count<G>{}(join_fresh{}, it, s);
                ++s.index;
                if constexpr (!S::template is_separator<G>) {
                    ++s.begin;
                }
                if constexpr (G + 1 == S::total_groups) {
                    return join_signal::CONTINUE;
                } else {
                    return count<G + 1>::middle(sum, it, s, end);
                }
            }

            /// NOTE: `count{}()` can be called in four different ways:
            /// 1.  `count<G>{}(join_fresh{}, it, p)`: initializes and counts group `G`
            ///     in its entirety
            /// 2.  `count<G>{}(it, p)`: counts from the current position of `p` to
            ///     the end of group `G`
            /// 3.  `count<G>{}(join_fresh{}, it, p, end)`: initializes group `G` and
            ///     counts from the beginning of `p` to the position of `end`
            /// 4.  `count<G>{}(it, p, end)`: counts from the current position of
            ///     `p` to the position of `end` (which must be in range)

            /// (1)

            template <is_join_subrange S>
            static constexpr difference_type begin_to_end(
                join_fresh,
                const join_iterator& it,
                S& s
            )
                noexcept (requires{
                    {s.end - s.begin} noexcept -> meta::nothrow::convertible_to<difference_type>;
                })
                requires (requires{
                    {s.end - s.begin} -> meta::convertible_to<difference_type>;
                })
            {
                return s.end - s.begin;
            }

            template <is_join_nested S>
            static constexpr difference_type begin_to_end(join_fresh, const join_iterator& it, S& s)
                noexcept (requires(difference_type sum) {
                    {s.begin != s.end} noexcept -> meta::nothrow::convertible_to<bool>;
                    {count<0>::middle(sum, it, s)} noexcept;
                })
                requires (requires(difference_type sum) {
                    {s.begin != s.end} -> meta::convertible_to<bool>;
                    {count<0>::middle(sum, it, s)};
                })
            {
                difference_type sum = 0;
                if (s.begin != s.end) {
                    while (count<0>::middle(sum, it, s) != join_signal::BREAK);
                }
                return sum;
            }

            template <meta::not_const P> requires (G < P::total_groups)
            static constexpr difference_type operator()(
                join_fresh,
                const join_iterator& it,
                P& p
            )
                noexcept (requires{
                    {p.template init<G>(it)} noexcept;
                    {begin_to_end(join_fresh{}, it, p.template get<G>())} noexcept;
                })
                requires (requires{
                    {p.template init<G>(it)};
                    {begin_to_end(join_fresh{}, it, p.template get<G>())};
                })
            {
                p.template init<G>(it);
                return begin_to_end(join_fresh{}, it, p.template get<G>());
            }

            /// (2)

            template <is_join_subrange S>
            static constexpr difference_type curr_to_end(
                const join_iterator& it,
                S& s
            )
                noexcept (requires{
                    {s.end - s.begin} noexcept -> meta::nothrow::convertible_to<difference_type>;
                })
                requires (requires{
                    {s.end - s.begin} -> meta::convertible_to<difference_type>;
                })
            {
                return s.end - s.begin;
            }

            template <is_join_nested S>
            static constexpr difference_type curr_to_end(const join_iterator& it, S& s)
                noexcept (requires(difference_type sum) {
                    {s.template visit<join_iterator::count>(it, s)} noexcept;
                    {count<0>::middle(sum, it, s)} noexcept;
                } && (S::template is_separator<G> || requires{
                    {++s.begin} noexcept;
                    {s.begin == s.end} noexcept -> meta::nothrow::convertible_to<bool>;
                }) && (G + 1 == S::total_groups || requires(difference_type sum) {
                    {count<G + 1>::middle(sum, it, s)} noexcept;
                }))
                requires (requires(difference_type sum) {
                    {s.template visit<join_iterator::count>(it, s)};
                    {count<0>::middle(sum, it, s)};
                } && (S::template is_separator<G> || requires{
                    {++s.begin};
                    {s.begin == s.end} -> meta::convertible_to<bool>;
                }) && (G + 1 == S::total_groups || requires(difference_type sum) {
                    {count<G + 1>::middle(sum, it, s)};
                }))
            {
                // get the distance to the end of the innermost subrange
                difference_type sum = s.template visit<join_iterator::count>(it, s);
                ++s.index;
                if constexpr (!S::template is_separator<G>) {
                    ++s.begin;
                    if (s.begin == s.end) {
                        return sum;  // skip separator after last value
                    }
                }

                // finish this iteration if necessary
                join_signal r = join_signal::CONTINUE;
                if constexpr (G + 1 < S::total_groups) {
                    r = count<G + 1>::middle(sum, it, s);
                }

                // iterate over the remaining groups
                while (r != join_signal::BREAK) {
                    r = count<0>::middle(sum, it, s);
                }
                return sum;
            }

            template <meta::not_const P> requires (G < P::total_groups)
            static constexpr difference_type operator()(const join_iterator& it, P& p)
                noexcept (requires{{curr_to_end(it, p.template get<G>())} noexcept;})
                requires (requires{{curr_to_end(it, p.template get<G>())};})
            {
                return curr_to_end(it, p.template get<G>());
            }

            /// (3)

            template <is_join_subrange S>
            static constexpr difference_type begin_to_sentinel(
                join_fresh,
                const join_iterator& it,
                S& s,
                const S& end
            )
                noexcept (requires{{
                    end.index
                } noexcept -> meta::nothrow::convertible_to<difference_type>;})
                requires (requires{{
                    end.index
                } -> meta::convertible_to<difference_type>;})
            {
                return end.index;
            }

            template <is_join_nested S>
            static constexpr difference_type begin_to_sentinel(
                join_fresh,
                const join_iterator& it,
                S& s,
                const S& end
            )
                noexcept (requires(difference_type sum) {
                    {count<0>::middle(sum, it, s)} noexcept;
                    {count<0>::middle(sum, it, s, end)} noexcept;
                    {sum + s.template visit<join_iterator::count>(it, s, end)} noexcept;
                })
                requires (requires(difference_type sum) {
                    {count<0>::middle(sum, it, s)};
                    {count<0>::middle(sum, it, s, end)};
                    {sum + s.template visit<join_iterator::count>(it, s, end)};
                })
            {
                // count full groups until `s.index` is within one group of `end.index`
                difference_type sum = 0;
                s.index += S::total_groups;
                while (s.index <= end.index) {
                    count<0>::middle(sum, it, s);
                }
                s.index -= S::total_groups;

                // continue counting last partial group until `s.index == end.index`
                count<0>::middle(sum, it, s, end);

                // recursively visit one level deeper after indices equalize
                return sum + s.template visit<join_iterator::count>(join_fresh{}, it, s, end);
            }

            template <meta::not_const P> requires (G < P::total_groups)
            static constexpr difference_type operator()(
                join_fresh,
                const join_iterator& it,
                P& p,
                const P& end
            )
                noexcept (requires{
                    {p.template init<G>(it)} noexcept;
                    {begin_to_sentinel(
                        join_fresh{},
                        it,
                        p.template get<G>(),
                        end.template get<G>()
                    )} noexcept;
                })
                requires (requires{
                    {p.template init<G>(it)};
                    {begin_to_sentinel(
                        join_fresh{},
                        it,
                        p.template get<G>(),
                        end.template get<G>()
                    )};
                })
            {
                p.template init<G>(it);
                return begin_to_sentinel(
                    join_fresh{},
                    it,
                    p.template get<G>(),
                    end.template get<G>()
                );
            }

            /// (4)

            template <is_join_subrange S>
            static constexpr difference_type curr_to_sentinel(
                const join_iterator& it,
                const S& s,
                const S& end
            )
                noexcept (requires{{
                    end.index - s.index
                } noexcept -> meta::nothrow::convertible_to<difference_type>;})
                requires (requires{{
                    end.index - s.index
                } -> meta::convertible_to<difference_type>;})
            {
                return end.index - s.index;
            }

            template <is_join_nested S>
            static constexpr difference_type curr_to_sentinel(
                const join_iterator& it,
                const S& s,
                const S& end
            )
                noexcept (
                    meta::nothrow::copyable<S> &&
                    requires(difference_type sum, S tmp) {
                        {tmp.template visit<join_iterator::count>(it, tmp)} noexcept;
                        {count<0>::middle(sum, it, tmp)} noexcept;
                        {count<0>::middle(sum, it, tmp, end)} noexcept;
                        {count<0>::middle(sum, it, tmp, s)} noexcept;
                        {tmp.template visit<join_iterator::count>(it, tmp, end)} noexcept;
                        {tmp.template visit<join_iterator::count>(it, tmp, s)} noexcept;
                        {s.template visit<join_iterator::count>(it, s, end)} noexcept;
                    } && (S::template is_separator<G> || requires(S tmp) {
                        {++tmp.begin} noexcept;
                    }) && (G + 1 == S::total_groups || requires(difference_type sum, S tmp) {
                        {count<G + 1>::middle(sum, it, tmp, end)} noexcept;
                        {count<G + 1>::middle(sum, it, tmp, s)} noexcept;
                    })
                )
                requires (
                    meta::copyable<S> &&
                    requires(difference_type sum, S tmp) {
                        {tmp.template visit<join_iterator::count>(it, tmp)};
                        {count<0>::middle(sum, it, tmp)};
                        {count<0>::middle(sum, it, tmp, end)};
                        {count<0>::middle(sum, it, tmp, s)};
                        {tmp.template visit<join_iterator::count>(it, tmp, end)};
                        {tmp.template visit<join_iterator::count>(it, tmp, s)};
                        {s.template visit<join_iterator::count>(it, s, end)};
                    } && (S::template is_separator<G> || requires(S tmp) {
                        {++tmp.begin};
                    }) && (G + 1 == S::total_groups || requires(difference_type sum, S tmp) {
                        {count<G + 1>::middle(sum, it, tmp, end)};
                        {count<G + 1>::middle(sum, it, tmp, s)};
                    })
                )
            {
                if (s.index < end.index) {
                    S tmp = s;

                    // get the distance to the end of the innermost subrange for `tmp`
                    difference_type sum = tmp.template visit<join_iterator::count>(it, tmp);
                    ++tmp.index;
                    if constexpr (!S::template is_separator<G>) {
                        ++tmp.begin;
                    }

                    // finish this iteration if necessary
                    if constexpr (G + 1 < S::total_groups) {
                        count<G + 1>::middle(sum, it, tmp, end);
                    }

                    // count full groups until `tmp.index` is within one group of `end.index`
                    tmp.index += S::total_groups;
                    while (tmp.index <= end.index) {
                        count<0>::middle(sum, it, tmp);
                    }
                    tmp.index -= S::total_groups;

                    // continue counting last partial group until `tmp.index == end.index`
                    count<0>::middle(sum, it, tmp, end);

                    // recursively visit one level deeper after indices equalize
                    return sum + tmp.template visit<join_iterator::count>(it, tmp, end);

                } else if (s.index > end.index) {
                    S tmp = end;
                    difference_type sum = -tmp.template visit<join_iterator::count>(it, tmp);
                    ++tmp.index;
                    if constexpr (!S::template is_separator<G>) {
                        ++tmp.begin;
                    }
                    difference_type temp_sum = 0;
                    if constexpr (G + 1 < S::total_groups) {
                        count<G + 1>::middle(temp_sum, it, tmp, s);
                    }
                    tmp.index += S::total_groups;
                    while (tmp.index <= s.index) {
                        count<0>::middle(temp_sum, it, tmp);
                    }
                    tmp.index -= S::total_groups;
                    count<0>::middle(temp_sum, it, tmp, s);
                    return sum - temp_sum - tmp.template visit<join_iterator::count>(it, tmp, s);

                } else {
                    // indices are already equal - just visit one level deeper
                    return s.template visit<join_iterator::count>(it, s, end);
                }
            }

            template <typename P> requires (G < P::total_groups)
            static constexpr difference_type operator()(
                const join_iterator& it,
                const P& p,
                const P& end
            )
                noexcept (requires{{curr_to_sentinel(
                    it,
                    p.template get<G>(),
                    end.template get<G>()
                )} noexcept;})
                requires (requires{{curr_to_sentinel(
                    it,
                    p.template get<G>(),
                    end.template get<G>()
                )};})
            {
                return curr_to_sentinel(it, p.template get<G>(), end.template get<G>());
            }
        };

        template <size_t H>
        struct distance {
            static constexpr size_t LHS = H / total_groups;
            static constexpr size_t RHS = H % total_groups;

            template <size_t... Gs>
            static constexpr difference_type middle(std::index_sequence<Gs...>, join_iterator& tmp)
                noexcept (requires(difference_type sum) {{(
                    (sum += count<LHS + 1 + Gs>{}(join_fresh{}, tmp, tmp)),
                    ...
                )} noexcept;})
                requires (requires(difference_type sum) {{(
                    (sum += count<LHS + 1 + Gs>{}(join_fresh{}, tmp, tmp)),
                    ...
                )};})
            {
                // a fold expression with a comma operator forces strict left-to-right
                // evaluation, which is crucial because `distance_middle` advances `tmp`
                // by side effect.
                difference_type sum = 0;
                ((sum += count<LHS + 1 + Gs>{}(join_fresh{}, tmp, tmp)), ...);
                return sum;
            }

            static constexpr difference_type operator()(
                const join_iterator& lhs,
                const join_iterator& rhs
            )
                noexcept (LHS == RHS && requires{
                    {count<LHS>{}(lhs, lhs, rhs)} noexcept;
                } && (LHS != 0 || !bidirectional || requires(join_iterator tmp) {
                    {lhs} noexcept -> meta::nothrow::copyable;
                    {rhs} noexcept -> meta::nothrow::copyable;
                    {count<LHS>{}(join_fresh{}, tmp, tmp, rhs)} noexcept;
                    {count<LHS>{}(join_fresh{}, tmp, tmp, lhs)} noexcept;
                }) && (LHS != total_groups - 1 || requires(join_iterator tmp) {
                    {lhs} noexcept -> meta::nothrow::copyable;
                    {rhs} noexcept -> meta::nothrow::copyable;
                    {count<LHS>{}(tmp, tmp)} noexcept;
                }))
                requires (LHS == RHS && requires{
                    {count<LHS>{}(lhs, lhs, rhs)};
                } && (LHS != 0 || !bidirectional || requires(join_iterator tmp) {
                    {lhs} -> meta::copyable;
                    {rhs} -> meta::copyable;
                    {count<LHS>{}(join_fresh{}, tmp, tmp, rhs)};
                    {count<LHS>{}(join_fresh{}, tmp, tmp, lhs)};
                }) && (LHS != total_groups - 1 || requires(join_iterator tmp) {
                    {lhs} -> meta::copyable;
                    {rhs} -> meta::copyable;
                    {count<LHS>{}(tmp, tmp)};
                }))
            {
                if constexpr (LHS == 0 && bidirectional) {
                    if (lhs.index < 0) {
                        join_iterator tmp = lhs;
                        return -lhs.index + count<LHS>{}(join_fresh{}, tmp, tmp, rhs);
                    }
                    if (rhs.index < 0) {
                        join_iterator tmp = rhs;
                        return rhs.index - count<LHS>{}(join_fresh{}, tmp, tmp, lhs);
                    }
                } else if constexpr (LHS == total_groups - 1) {
                    if (rhs.index >= total_groups) {
                        join_iterator tmp = lhs;
                        return rhs.index - (total_groups - 1) + count<LHS>{}(tmp, tmp);
                    }
                    if (lhs.index >= total_groups) {
                        join_iterator tmp = rhs;
                        return -(lhs.index - (total_groups - 1) + count<LHS>{}(tmp, tmp));
                    }
                }
                return count<LHS>{}(lhs, lhs, rhs);
            }

            static constexpr difference_type operator()(
                const join_iterator& lhs,
                const join_iterator& rhs
            )
                noexcept (
                    meta::nothrow::copyable<join_iterator> &&
                    requires(join_iterator tmp, difference_type sum) {
                        {sum += count<LHS>{}(tmp, tmp)} noexcept;
                        {sum += middle(std::make_index_sequence<RHS - LHS - 1>{}, tmp)} noexcept;
                        {sum += count<RHS>{}(join_fresh{}, tmp, tmp, rhs)} noexcept;
                    } && (
                        LHS > 0 ||
                        !bidirectional ||
                        requires(join_iterator tmp, difference_type sum) {
                            {sum += count<LHS>{}(join_fresh{}, tmp, tmp)} noexcept;
                        }
                    ) && (
                        RHS < total_groups - 1 ||
                        !bidirectional ||
                        requires(join_iterator tmp, difference_type sum) {
                            {sum += count<RHS>{}(join_fresh{}, tmp, tmp)} noexcept;
                        }
                    )
                )
                requires (
                    LHS < RHS &&
                    meta::copyable<join_iterator> &&
                    requires(join_iterator tmp, difference_type sum) {
                        {sum += count<LHS>{}(tmp, tmp)};
                        {sum += middle(std::make_index_sequence<RHS - LHS - 1>{}, tmp)};
                        {sum += count<RHS>{}(join_fresh{}, tmp, tmp, rhs)};
                    } && (
                        LHS > 0 ||
                        !bidirectional ||
                        requires(join_iterator tmp, difference_type sum) {
                            {sum += count<LHS>{}(join_fresh{}, tmp, tmp)};
                        }
                    ) && (
                        RHS < total_groups - 1 ||
                        !bidirectional ||
                        requires(join_iterator tmp, difference_type sum) {
                            {sum += count<RHS>{}(join_fresh{}, tmp, tmp)};
                        }
                    )
                )
            {
                join_iterator tmp = lhs;
                difference_type sum = 0;
                if constexpr (LHS == 0 && bidirectional) {
                    if (tmp.index < 0) {
                        sum -= tmp.index;
                        sum += count<LHS>{}(join_fresh{}, tmp, tmp);
                    } else {
                        sum += count<LHS>{}(tmp, tmp);
                    }
                } else {
                    sum += count<LHS>{}(tmp, tmp);
                }
                sum += middle(std::make_index_sequence<RHS - LHS - 1>{}, tmp);
                if constexpr (RHS == total_groups - 1) {
                    if (rhs.index >= total_groups) {
                        sum += rhs.index - (total_groups - 1);
                        sum += count<RHS>{}(join_fresh{}, tmp, tmp);
                    } else {
                        sum += count<RHS>{}(join_fresh{}, tmp, tmp, rhs);
                    }
                } else {
                    sum += count<RHS>{}(join_fresh{}, tmp, tmp, rhs);
                }
                return sum;
            }

            static constexpr difference_type operator()(
                const join_iterator& lhs,
                const join_iterator& rhs
            )
                noexcept (
                    meta::nothrow::copyable<join_iterator> &&
                    requires(join_iterator tmp, difference_type sum) {
                        {sum -= count<RHS>{}(tmp, tmp)} noexcept;
                        {sum -= middle(std::make_index_sequence<LHS - RHS - 1>{}, tmp)} noexcept;
                        {sum -= count<LHS>{}(join_fresh{}, tmp, tmp, lhs)} noexcept;
                    } && (
                        RHS > 0 ||
                        !bidirectional ||
                        requires(join_iterator tmp, difference_type sum) {
                            {sum -= count<RHS>{}(join_fresh{}, tmp, tmp)} noexcept;
                        }
                    ) && (
                        LHS < total_groups - 1 ||
                        !bidirectional ||
                        requires(join_iterator tmp, difference_type sum) {
                            {sum -= count<LHS>{}(join_fresh{}, tmp, tmp)} noexcept;
                        }
                    )
                )
                requires (
                    LHS > RHS &&
                    meta::copyable<join_iterator> &&
                    requires(join_iterator tmp, difference_type sum) {
                        {sum -= count<RHS>{}(tmp, tmp)};
                        {sum -= middle(std::make_index_sequence<LHS - RHS - 1>{}, tmp)};
                        {sum -= count<LHS>{}(join_fresh{}, tmp, tmp, lhs)};
                    } && (
                        RHS > 0 ||
                        !bidirectional ||
                        requires(join_iterator tmp, difference_type sum) {
                            {sum -= count<RHS>{}(join_fresh{}, tmp, tmp)};
                        }
                    ) && (
                        LHS < total_groups - 1 ||
                        !bidirectional ||
                        requires(join_iterator tmp, difference_type sum) {
                            {sum -= count<LHS>{}(join_fresh{}, tmp, tmp)};
                        }
                    )
                )
            {
                join_iterator tmp = rhs;
                difference_type sum = 0;
                if constexpr (RHS == 0 && bidirectional) {
                    if (tmp.index < 0) {
                        sum += tmp.index;
                        sum -= count<RHS>{}(join_fresh{}, tmp, tmp);
                    } else {
                        sum -= count<RHS>{}(tmp, tmp);
                    }
                } else {
                    sum -= count<RHS>{}(tmp, tmp);
                }
                sum -= middle(std::make_index_sequence<LHS - RHS - 1>{}, tmp);
                if constexpr (LHS == total_groups - 1) {
                    if (lhs.index >= total_groups) {
                        sum -= lhs.index - (total_groups - 1);
                        sum -= count<LHS>{}(join_fresh{}, tmp, tmp);
                    } else {
                        sum -= count<LHS>{}(join_fresh{}, tmp, tmp, lhs);
                    }
                } else {
                    sum -= count<LHS>{}(join_fresh{}, tmp, tmp, lhs);
                }
                return sum;
            }
        };

    public:
        [[nodiscard]] constexpr join_iterator() = default;
        [[nodiscard]] constexpr join_iterator(R range)
            noexcept (requires{{jump_forward<0>::advance(*this, *this)} noexcept;})
            requires (requires{{jump_forward<0>::advance(*this, *this)};})
        :
            range(std::addressof(range)),
            index(0)
        {
            jump_forward<0>::advance(*this, *this);
        }
        [[nodiscard]] constexpr join_iterator(R range, NoneType) noexcept :
            range(std::addressof(range)),
            index(total_groups)
        {}

        [[nodiscard]] constexpr yield_type operator*()
            noexcept (requires{{subrange.template visit<deref>(*this)} noexcept;})
            requires (requires{{subrange.template visit<deref>(*this)};})
        {
            return subrange.template visit<deref>(*this);
        }

        [[nodiscard]] constexpr yield_type operator*() const
            noexcept (requires{{subrange.template visit<deref>(*this)} noexcept;})
            requires (requires{{subrange.template visit<deref>(*this)};})
        {
            return subrange.template visit<deref>(*this);
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{impl::arrow_proxy{**this}} noexcept;})
            requires (requires{{impl::arrow_proxy{**this}};})
        {
            return impl::arrow_proxy{**this};
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{impl::arrow_proxy{**this}} noexcept;})
            requires (requires{{impl::arrow_proxy{**this}};})
        {
            return impl::arrow_proxy{**this};
        }

        [[nodiscard]] constexpr std::strong_ordering operator<=>(const join_iterator& other) const
            noexcept
        {
            if (auto cmp = index <=> other.index; cmp != 0) return cmp;
            if (index >= total_groups || index < 0) return std::strong_ordering::equal;
            return subrange.template visit<compare>(*this, other);
        }

        [[nodiscard]] constexpr bool operator==(const join_iterator& other) const noexcept {
            return (*this <=> other) == 0;
        }

        constexpr join_iterator& operator++()
            noexcept (requires{
                {visit<increment>(*this, *this)} noexcept;
            } && (!bidirectional || requires{
                {jump_forward<0>::advance(*this, *this)} noexcept;
            }))
            requires (requires{
                {visit<increment>(*this, *this)};
            } && (!bidirectional || requires{
                {jump_forward<0>::advance(*this, *this)};
            }))
        {
            if (index >= total_groups) {
                ++index;
            } else {
                if constexpr (bidirectional) {
                    if (index < 0) {
                        ++index;
                        if (index == 0) {
                            jump_forward<0>::advance(*this, *this);
                        }
                    } else {
                        visit<increment>(*this, *this);
                    }
                } else {
                    visit<increment>(*this, *this);
                }
            }
            return *this;
        }

        [[nodiscard]] constexpr join_iterator operator++(int)
            noexcept (
                meta::nothrow::copyable<join_iterator> &&
                meta::nothrow::has_preincrement<join_iterator>
            )
            requires (
                meta::copyable<join_iterator> &&
                meta::has_preincrement<join_iterator>
            )
        {
            join_iterator tmp = *this;
            ++*this;
            return tmp;
        }

        constexpr join_iterator& operator--()
            noexcept (requires{
                {visit<decrement>(*this, *this)} noexcept;
                {jump_backward<total_groups - 1>::advance(*this, *this)} noexcept;
            })
            requires (requires{
                {visit<decrement>(*this, *this)};
                {jump_backward<total_groups - 1>::advance(*this, *this)};
            })
        {
            if (index >= total_groups) {
                --index;
                if (index == total_groups - 1) {
                    jump_backward<total_groups - 1>::advance(*this, *this);
                }
            } else if (index < 0) {
                --index;
            } else {
                visit<decrement>(*this, *this);
            }
            return *this;
        }

        [[nodiscard]] constexpr join_iterator operator--(int)
            noexcept (
                meta::nothrow::copyable<join_iterator> &&
                meta::nothrow::has_predecrement<join_iterator>
            )
            requires (
                meta::copyable<join_iterator> &&
                meta::has_predecrement<join_iterator>
            )
        {
            join_iterator tmp = *this;
            --*this;
            return tmp;
        }

        constexpr join_iterator& operator+=(difference_type i)
            noexcept (requires{
                {visit<isub>(*this, -i, *this)} noexcept;
                {visit<iadd>(*this, i, *this)} noexcept;
            } && (!bidirectional || requires{
                {seek_backward<total_groups - 1>::advance(*this, i, *this)} noexcept;
                {seek_forward<0>::advance(*this, i, *this)} noexcept;
            }))
            requires (requires{
                {visit<isub>(*this, -i, *this)};
                {visit<iadd>(*this, i, *this)};
            } && (!bidirectional || requires{
                {seek_backward<total_groups - 1>::advance(*this, i, *this)};
                {seek_forward<0>::advance(*this, i, *this)};
            }))
        {
            if (i < 0) {
                if (index < 0) {
                    index += i;
                    return *this;
                }
                join_signal r = join_signal::CONTINUE;
                if constexpr (bidirectional) {
                    if (index >= total_groups) {
                        index += i;
                        if (index < total_groups) {
                            i = total_groups - 1 - index;
                            index = total_groups - 1;
                            r = seek_backward<total_groups - 1>::advance(*this, i, *this);
                        }
                    } else {
                        r = visit<isub>(*this, -i, *this);
                    }
                } else {
                    r = visit<isub>(*this, -i, *this);
                }
                if (r != join_signal::GOOD) {
                    index -= i;  // index overflows past the beginning of the range
                };
            } else {
                if (index >= total_groups) {
                    index += i;
                    return *this;
                }
                join_signal r = join_signal::CONTINUE;
                if constexpr (bidirectional) {
                    if (index < 0) {
                        index += i;
                        if (index >= 0) {
                            i = index;
                            index = 0;
                            r = seek_forward<0>::advance(*this, i, *this);
                        }
                    } else {
                        r = visit<iadd>(*this, i, *this);
                    }
                } else {
                    r = visit<iadd>(*this, i, *this);
                }
                if (r != join_signal::GOOD) {
                    index += i;  // index overflows past the end of the range
                };
            }
            return *this;
        }

        [[nodiscard]] constexpr join_iterator& operator-=(difference_type i)
            noexcept (requires{{*this += -i} noexcept;})
            requires (requires{{*this += -i};})
        {
            return *this += -i;
        }

        [[nodiscard]] friend constexpr join_iterator operator+(
            const join_iterator& self,
            difference_type i
        )
            noexcept (
                meta::nothrow::copyable<join_iterator> &&
                meta::nothrow::has_iadd<join_iterator, difference_type>
            )
            requires (
                meta::copyable<join_iterator> &&
                meta::has_iadd<join_iterator, difference_type>
            )
        {
            join_iterator result = self;
            result += i;
            return result;
        }

        [[nodiscard]] friend constexpr join_iterator operator+(
            difference_type i,
            const join_iterator& self
        )
            noexcept (
                meta::nothrow::copyable<join_iterator> &&
                meta::nothrow::has_iadd<join_iterator, difference_type>
            )
            requires (
                meta::copyable<join_iterator> &&
                meta::has_iadd<join_iterator, difference_type>
            )
        {
            join_iterator result = self;
            result += i;
            return result;
        }

        [[nodiscard]] constexpr join_iterator operator-(difference_type i) const
            noexcept (
                meta::nothrow::copyable<join_iterator> &&
                meta::nothrow::has_isub<join_iterator, difference_type>
            )
            requires (
                meta::copyable<join_iterator> &&
                meta::has_isub<join_iterator, difference_type>
            )
        {
            join_iterator result = *this;
            result -= i;
            return result;
        }

        [[nodiscard]] constexpr yield_type operator[](difference_type i)
            noexcept (
                meta::nothrow::copyable<join_iterator> &&
                meta::nothrow::has_iadd<join_iterator, difference_type> &&
                meta::nothrow::has_dereference<join_iterator>
            )
            requires (
                meta::copyable<join_iterator> &&
                meta::has_iadd<join_iterator, difference_type> &&
                meta::has_dereference<join_iterator>
            )
        {
            join_iterator temp = *this;
            temp += i;
            return *temp;
        }

        [[nodiscard]] constexpr yield_type operator[](difference_type i) const
            noexcept (
                meta::nothrow::copyable<join_iterator> &&
                meta::nothrow::has_iadd<join_iterator, difference_type> &&
                meta::nothrow::has_dereference<join_iterator>
            )
            requires (
                meta::copyable<join_iterator> &&
                meta::has_iadd<join_iterator, difference_type> &&
                meta::has_dereference<join_iterator>
            )
        {
            join_iterator temp = *this;
            temp += i;
            return *temp;
        }

        [[nodiscard]] constexpr difference_type operator-(const join_iterator& other) const
            noexcept (requires{{
                impl::basic_vtable<distance, total_groups * total_groups>{
                    size_t(index * total_groups + other.index)
                }(*this, other)
            } noexcept;})
            requires (requires{{
                impl::basic_vtable<distance, total_groups * total_groups>{
                    size_t(index * total_groups + other.index)
                }(*this, other)
            };})
        {
            ssize_t l = index;
            ssize_t r = other.index;

            // if both iterators are out of bounds and in the same region, then the
            // distance is simply the difference between their overflowing indices
            if constexpr (bidirectional) {
                if (l < 0) {
                    if (r < 0) return l - r;
                    l = 0;
                    if (r >= total_groups) r = total_groups - 1;
                } else if (l >= total_groups) {
                    if (r >= total_groups) return l - r;
                    l = total_groups - 1;
                    if (r < 0) r = 0;
                } else if (r < 0) {
                    r = 0;
                } else if (r >= total_groups) {
                    r = total_groups - 1;
                }
            } else {
                if (l >= total_groups) {
                    if (r >= total_groups) return l - r;
                    l = total_groups - 1;
                } else if (r >= total_groups) {
                    r = total_groups - 1;
                }
            }

            // otherwise we dispatch based on the cartesian product of the normalized
            // indices, which effectively promotes them to compile time, allowing the
            // rest of the logic to index on that basis
            return impl::basic_vtable<distance, total_groups * total_groups>{
                size_t(l * total_groups + r)
            }(*this, other);
        }
    };

    /* `make_join_iterator` produces a `join_iterator` containing forward iterators
    over a `join` container.  It is responsible for deducing the proper subrange types
    and specializing the `join_iterator` template accordingly, such that the public
    `begin()` and `end()` methods for joined ranges devolve to
    `make_join_iterator{*this}.begin()` and `make_join_iterator{*this}.end()`,
    respectively. */
    template <meta::lvalue R, typename>
    struct _make_join_iterator;
    template <meta::lvalue R, size_t... Is>
    struct _make_join_iterator<R, std::index_sequence<Is...>> {
        using type = join_iterator<
            R,
            void,
            decltype(join_forward{std::declval<R>().template arg<Is>()}.template begin<void>())...
        >;
    };
    template <meta::lvalue R, size_t... Is>
        requires (meta::not_void<typename meta::unqualify<R>::separator_type>)
    struct _make_join_iterator<R, std::index_sequence<Is...>> {
        using separator = decltype(join_forward{std::declval<R>().sep()}.template begin<void>());
        using type = join_iterator<
            R,
            separator,
            decltype(join_forward{
                std::declval<R>().template arg<Is>()
            }.template begin<separator>())...
        >;
    };
    template <meta::lvalue R>
    struct make_join_iterator {
        using begin_type = _make_join_iterator<R, typename meta::unqualify<R>::indices>;
        using end_type = begin_type;

        R range;

        [[nodiscard]] constexpr begin_type begin()
            noexcept (requires{{begin_type(range)} noexcept;})
            requires (requires{{begin_type(range)};})
        {
            return begin_type(range);
        }

        [[nodiscard]] constexpr end_type end()
            noexcept (requires{{end_type(range, None)} noexcept;})
            requires (requires{{end_type(range, None)};})
        {
            return end_type(range, None);
        }
    };
    template <typename R>
    make_join_iterator(R&) -> make_join_iterator<R&>;

    /* `make_join_reversed` works identically to `make_join_iterator`, except that the
    subranges encapsulate reverse iterators rather than forward ones. */
    template <meta::lvalue R, typename>
    struct _make_join_reversed;
    template <meta::lvalue R, size_t... Is>
    struct _make_join_reversed<R, std::index_sequence<Is...>> {
        using type = join_iterator<
            R,
            void,
            decltype(join_reverse{
                std::declval<R>().template arg<Is>()
            }.template begin<void>())...
        >;
    };
    template <meta::lvalue R, size_t... Is>
        requires (meta::not_void<typename meta::unqualify<R>::separator_type>)
    struct _make_join_reversed<R, std::index_sequence<Is...>> {
        using separator = decltype(join_reverse{std::declval<R>().sep()}.template begin<void>());
        using type = join_iterator<
            R,
            separator,
            decltype(join_reverse{
                std::declval<R>().template arg<Is>()
            }.template begin<separator>())...
        >;
    };
    template <meta::lvalue R>
        requires (range_reverse_iterable<typename meta::unqualify<R>::argument_types>)
    struct make_join_reversed {
        using begin_type = _make_join_reversed<R, typename meta::unqualify<R>::indices>::type;
        using end_type = begin_type;

        R range;

        [[nodiscard]] constexpr begin_type begin()
            noexcept (requires{{begin_type(range)} noexcept;})
            requires (requires{{begin_type(range)};})
        {
            return begin_type(range);
        }

        constexpr end_type end()
            noexcept (requires{{end_type(range, None)} noexcept;})
            requires (requires{{end_type(range, None)};})
        {
            return end_type(range, None);
        }
    };
    template <typename R>
    make_join_reversed(R&) -> make_join_reversed<R&>;

    /* The type returned by the subscript operator for joined ranges may need to be a
    union if any of the subscript types differ.  If a separator is provided, then its
    subscript type must also be included in the union.  If all of the subscript types
    are the same, then the union will collapse into a single type, and the `trivial`
    flag will be set to true. */
    template <typename R, typename>
    struct _join_subscript_type;
    template <typename R, size_t... Is>
        requires (meta::is_void<typename meta::unqualify<R>::separator_type>)
    struct _join_subscript_type<R, std::index_sequence<Is...>> {
        using type = meta::union_type<
            decltype((std::declval<R>().template arg<Is>()[
                std::declval<typename meta::unqualify<R>::size_type>()
            ]))...
        >;
        static constexpr bool trivial = meta::trivial_union<
            decltype((std::declval<R>().template arg<Is>()[
                std::declval<typename meta::unqualify<R>::size_type>()
            ]))...
        >;
    };
    template <typename R, size_t... Is>
        requires (meta::not_void<typename meta::unqualify<R>::separator_type>)
    struct _join_subscript_type<R, std::index_sequence<Is...>> {
        using type = meta::union_type<
            decltype((std::declval<R>().sep()[
                std::declval<typename meta::unqualify<R>::size_type>()
            ])),
            decltype((std::declval<R>().template arg<Is>()[
                std::declval<typename meta::unqualify<R>::size_type>()
            ]))...
        >;
        static constexpr bool trivial = meta::trivial_union<
            decltype((std::declval<R>().sep()[
                std::declval<typename meta::unqualify<R>::size_type>()
            ])),
            decltype((std::declval<R>().template arg<Is>()[
                std::declval<typename meta::unqualify<R>::size_type>()
            ]))...
        >;
    };
    template <typename R>
    using join_subscript_type = _join_subscript_type<R, typename meta::unqualify<R>::indices>;

    template <typename Sep>
    constexpr size_t join_sep_size = 1;
    template <meta::is_void Sep>
    constexpr size_t join_sep_size<Sep> = 0;
    template <meta::range Sep> requires (meta::tuple_like<Sep>)
    constexpr size_t join_sep_size<Sep> = meta::tuple_size<Sep>;
    template <join_flatten Sep>
        requires (meta::tuple_like<Sep> && meta::tuple_like<meta::yield_type<Sep>>)
    constexpr size_t join_sep_size<Sep> =
        meta::tuple_size<Sep> * meta::tuple_size<meta::yield_type<Sep>>;

    template <typename T, typename Sep>
    constexpr size_t join_arg_size = 1;
    template <meta::range T, typename Sep> requires (meta::tuple_like<T>)
    constexpr size_t join_arg_size<T, Sep> = meta::tuple_size<T>;
    template <join_flatten T, typename Sep>
        requires (meta::tuple_like<T> && meta::tuple_like<meta::yield_type<T>>)
    constexpr size_t join_arg_size<T, Sep> =
        meta::tuple_size<meta::yield_type<T>> * meta::tuple_size<T> +
        join_sep_size<Sep> * (meta::tuple_size<T> - (meta::tuple_size<T> > 0));

    template <typename Sep, typename... A>
    constexpr size_t join_tuple_size = (
        join_arg_size<A, Sep> +
        ... +
        (join_sep_size<Sep> * (sizeof...(A) - (sizeof...(A) > 0))
    ));

    template <typename Sep, typename... A> requires (join_concept<Sep, A...>)
    struct join_storage {
        [[no_unique_address]] impl::ref<Sep> sep;
        [[no_unique_address]] impl::basic_tuple<A...> args;
    };

    template <meta::is_void Sep, typename... A> requires (join_concept<Sep, A...>)
    struct join_storage<Sep, A...> {
        [[no_unique_address]] impl::basic_tuple<A...> args;
    };

    /* Joined ranges store an arbitrary set of argument types as well as a possible
    separator to insert between each one.  The arguments are not required to be ranges,
    and will be inserted as single elements at their respective position if not.  If
    any of the arguments are ranges, then they will be iterated over in sequence. */
    template <typename Sep, typename... A> requires (join_concept<Sep, A...>)
    struct join {
        using separator_type = Sep;
        using argument_types = meta::pack<A...>;
        using size_type = size_t;
        using index_type = ssize_t;
        using indices = std::make_index_sequence<sizeof...(A)>;
        using ranges = range_indices<A...>;

        [[no_unique_address]] join_storage<Sep, A...> m_storage;

        /* Perfectly forward the stored separator, if one was given. */
        template <typename Self> requires (meta::not_void<Sep>)
        [[nodiscard]] constexpr decltype(auto) sep(this Self&& self) noexcept {
            return (*std::forward<Self>(self).m_storage.sep);
        }

        /* Perfectly forward the I-th joined argument. */
        template <size_t I, typename Self> requires (I < sizeof...(A))
        [[nodiscard]] constexpr decltype(auto) arg(this Self&& self) noexcept {
            return (std::forward<Self>(self).m_storage.args.template get<I>());
        }

    private:
        static constexpr bool sized = (
            (!meta::range<Sep> || (meta::has_size<Sep> && (
                !join_flatten<Sep> || meta::tuple_like<meta::yield_type<Sep>>
            ))) &&
            ... &&
            (!meta::range<A> || (meta::has_size<A> && (
                !join_flatten<A> || meta::tuple_like<meta::yield_type<A>>
            )))
        );
        static constexpr bool tuple_like = (
            (!meta::range<Sep> || meta::tuple_like<Sep>) &&
            ... &&
            (!meta::range<A> || meta::tuple_like<A>)
        );
        static constexpr bool sequence_like = (meta::sequence<Sep> || ... || meta::sequence<A>);

        template <size_t J>
        static constexpr size_type unpack_size =
            meta::tuple_size<meta::yield_type<meta::unpack_type<J, A...>>>;

        template <typename T>
        static constexpr bool has_size_impl(const T& value) noexcept {
            if constexpr (meta::sequence<T>) {
                return value.has_size();
            } else {
                return true;
            }
        }

        template <size_t... Is> requires (sequence_like && sizeof...(Is) == ranges::size())
        constexpr bool _has_size(std::index_sequence<Is...>) const noexcept {
            return (has_size_impl(arg<Is>()) && ...);
        }

        template <typename T>
        constexpr size_type size_impl(const T& arg) const
            noexcept (
                !meta::range<T> ||
                (meta::nothrow::size_returns<size_type, const T&> && (
                    !join_flatten<T> ||
                    (meta::tuple_like<meta::yield_type<T>> && (
                        !meta::range<Sep> ||
                        requires{{size_type(sep().size())} noexcept;}
                    ))
                ))
            )
            requires (
                !meta::range<T> ||
                (meta::size_returns<size_type, const T&> && (
                    !join_flatten<T> ||
                    (meta::tuple_like<meta::yield_type<T>> && (
                        !meta::range<Sep> ||
                        requires{{size_type(sep().size())};}
                    ))
                ))
            )
        {
            if constexpr (join_flatten<T>) {
                constexpr size_type element_size = meta::tuple_size<meta::yield_type<T>>;
                size_type n = arg.size();
                if constexpr (meta::is_void<Sep>) {
                    return n * element_size;
                } else if constexpr (meta::range<Sep>) {
                    return n * element_size + (n - (n > 0)) * size_type(sep().size());
                } else {
                    return n * element_size + (n - (n > 0));
                }
            } else if constexpr (meta::range<T>) {
                return arg.size();
            } else {
                return 1;
            }
        }

        template <size_t... Is> requires (sizeof...(Is) == indices::size())
        constexpr size_type _size(std::index_sequence<Is...>) const
            noexcept (requires{{(size_impl(arg<Is>()) + ... + 0)} noexcept;})
            requires (meta::is_void<Sep> && requires{{(size_impl(arg<Is>()) + ... + 0)};})
        {
            return (size_impl(arg<Is>()) + ... + 0);
        }

        template <size_t... Is> requires (sizeof...(Is) == indices::size())
        constexpr size_type _size(std::index_sequence<Is...>) const
            noexcept (requires{{(
                size_impl(arg<Is>()) +
                ... +
                (size_type(sep().size()) * (sizeof...(Is) - (sizeof...(Is) > 0)))
            )} noexcept;})
            requires (meta::range<Sep> && requires{{(
                size_impl(arg<Is>()) +
                ... +
                (size_type(sep().size()) * (sizeof...(Is) - (sizeof...(Is) > 0)))
            )};})
        {
            return (
                size_impl(arg<Is>()) +
                ... +
                (size_type(sep().size()) * (sizeof...(Is) - (sizeof...(Is) > 0)))
            );
        }

        template <size_t... Is> requires (sizeof...(Is) == indices::size())
        constexpr size_type _size(std::index_sequence<Is...>) const
            noexcept (requires{{(
                size_impl(arg<Is>()) +
                ... +
                (sizeof...(Is) - (sizeof...(Is) > 0))
            )} noexcept;})
            requires (meta::not_void<Sep> && !meta::range<Sep> && requires{{(
                size_impl(arg<Is>()) +
                ... +
                (sizeof...(Is) - (sizeof...(Is) > 0))
            )};})
        {
            return (
                size_impl(arg<Is>()) +
                ... +
                (sizeof...(Is) - (sizeof...(Is) > 0))
            );
        }

    public:
        /* If any of the input ranges are `sequence` types, then it's possible that
        `.size()` could throw a runtime error due to type erasure, which acts as a
        SFINAE barrier with respect to the underlying container.  In order to handle
        this, a joined range consisting of one or more sequences will expose the same
        `has_size()` accessor as the sequences themselves, and will return their
        logical conjunction. */
        [[nodiscard]] static constexpr bool has_size() noexcept requires (sized && !sequence_like) {
            return true;
        }

        /* If any of the input ranges are `sequence` types, then it's possible that
        `.size()` could throw a runtime error due to type erasure, which acts as a
        SFINAE barrier with respect to the underlying container.  In order to handle
        this, a joined range consisting of one or more sequences will expose the same
        `has_size()` accessor as the sequences themselves, and will return their
        logical conjunction. */
        [[nodiscard]] constexpr bool has_size() const noexcept requires (sized && sequence_like) {
            return _has_size(ranges{});
        }

        /* The overall size of the joined range as an unsigned integer.  This is only
        enabled if all of the arguments are either sized ranges or non-range inputs,
        and the separator is either absent, a scalar value, or a sized range.  If one
        or more unpacking operators are used, then the unpacked element type must be
        either scalar or tuple-like, so that the size can be computed in constant time.
        Otherwise, this method will fail to compile.

        Note that if any of the input ranges are `sequence` types (which may or may not
        be sized), then this method may throw an `IndexError` if `sequence.has_size()`
        evaluates to `false` for any of the sequences.  This is a consequence of type
        erasure on the underlying container, which acts as a SFINAE barrier for the
        compiler, and may be worked around via the `has_size()` method.  If that method
        returns `true`, then this method will never throw. */
        [[nodiscard]] static constexpr size_type size() noexcept requires (tuple_like) {
            return join_tuple_size<Sep, A...>;
        }

        /* The overall size of the joined range as an unsigned integer.  This is only
        enabled if all of the arguments are either sized ranges or non-range inputs,
        and the separator is either absent, a scalar value, or a sized range.  If one
        or more unpacking operators are used, then the unpacked element type must be
        either scalar or tuple-like, so that the size can be computed in constant time.
        Otherwise, this method will fail to compile.

        Note that if any of the input ranges are `sequence` types (which may or may not
        be sized), then this method may throw an `IndexError` if `sequence.has_size()`
        evaluates to `false` for any of the sequences.  This is a consequence of type
        erasure on the underlying container, which acts as a SFINAE barrier for the
        compiler, and may be worked around via the `has_size()` method.  If that method
        returns `true`, then this method will never throw. */
        [[nodiscard]] constexpr size_type size() const
            noexcept (requires{{_size(indices{})} noexcept;})
            requires (!tuple_like && sized)
        {
            return _size(indices{});
        }

        /* The overall size of the joined range as a signed integer.  This is only
        enabled if all of the arguments are either sized ranges or non-range inputs,
        and the separator is either absent, a scalar value, or a sized range. */
        [[nodiscard]] constexpr index_type ssize() const
            noexcept (requires{{index_type(size())} noexcept;})
            requires (sized)
        {
            return index_type(size());
        }

        /* True if the joined range contains no elements.  False otherwise. */
        [[nodiscard]] constexpr bool empty() const
            noexcept (requires{{begin() == end()} noexcept;})
        {
            return begin() == end();
        }

    private:
        template <size_t I> requires (I < sizeof...(A))
        static constexpr bool broadcast = !meta::range<meta::unpack_type<I, A...>>;

        template <size_t I> requires (I < sizeof...(A))
        static constexpr bool flatten = join_flatten<meta::unpack_type<I, A...>>;

        template <typename Self>
        using subscript_type = join_subscript_type<Self>::type;

        template <size_t I, typename Self> requires (I < join_sep_size<Sep>)
        constexpr decltype(auto) get_sep_impl(this Self&& self)
            noexcept ((join_flatten<Sep> && requires{
                {std::forward<Self>(self).sep().
                    template get<I / meta::tuple_size<meta::yield_type<Sep>>>().
                    template get<I % meta::tuple_size<meta::yield_type<Sep>>>()
                } noexcept;
            }) || (!join_flatten<Sep> && meta::range<Sep> && requires{
                {std::forward<Self>(self).sep().template get<I>()} noexcept;
            }) || (!meta::range<Sep> && requires{
                {std::forward<Self>(self).sep()} noexcept;
            }))
            requires ((join_flatten<Sep> && requires{
                {std::forward<Self>(self).sep().
                    template get<I / meta::tuple_size<meta::yield_type<Sep>>>().
                    template get<I % meta::tuple_size<meta::yield_type<Sep>>>()
                };
            }) || (!join_flatten<Sep> && meta::range<Sep> && requires{
                {std::forward<Self>(self).sep().template get<I>()};
            }) || (!meta::range<Sep> && requires{
                {std::forward<Self>(self).sep()};
            }))
        {
            if constexpr (join_flatten<Sep>) {
                return (std::forward<Self>(self).sep().
                    template get<I / meta::tuple_size<meta::yield_type<Sep>>>().
                    template get<I % meta::tuple_size<meta::yield_type<Sep>>>()
                );
            } else if constexpr (meta::range<Sep>) {
                return (std::forward<Self>(self).sep().template get<I>());
            } else {
                return (std::forward<Self>(self).sep());
            }
        }

        /* Index `I` matches a separator. */
        template <size_t I, size_t J, typename Self> requires (I < join_sep_size<Sep>)
        constexpr decltype(auto) get_sep(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).template get_sep_impl<I>()} noexcept;})
            requires (requires{{std::forward<Self>(self).template get_sep_impl<I>()};})
        {
            return std::forward<Self>(self).template get_sep_impl<I>();
        }

        template <size_t I, size_t J, typename Self>
        static constexpr size_t quotient = 
            (join_sep_size<Sep> + I) / (join_sep_size<Sep> + unpack_size<J>);

        template <size_t I, size_t J, typename Self>
        static constexpr size_t remainder =
            (join_sep_size<Sep> + I) % (join_sep_size<Sep> + unpack_size<J>);

        /* Index `I` matches the `J`-th joined argument, accounting for separators. */
        template <size_t I, size_t J, typename Self>
            requires (I < join_arg_size<meta::unpack_type<J, A...>, Sep>)
        constexpr decltype(auto) _get(this Self&& self)
            noexcept ((flatten<J> && (
                (
                    remainder<I, J, Self> < join_sep_size<Sep> &&
                    requires{{std::forward<Self>(self).
                        template get_sep_impl<remainder<I, J, Self>>()
                    } noexcept;}
                ) || (
                    remainder<I, J, Self> >= join_sep_size<Sep> &&
                    requires{{std::forward<Self>(self).template arg<J>().
                        template get<quotient<I, J, Self>>().
                        template get<remainder<I, J, Self> - join_sep_size<Sep>>()
                    } noexcept;}
                )
            )) || (!flatten<J> && !broadcast<J> && requires{
                {std::forward<Self>(self).template arg<J>().template get<I>()} noexcept;
            }) || (broadcast<J> && requires{
                {std::forward<Self>(self).template arg<J>()} noexcept;
            }))
            requires ((flatten<J> && (
                (
                    remainder<I, J, Self> < join_sep_size<Sep> &&
                    requires{{std::forward<Self>(self).
                        template get_sep_impl<remainder<I, J, Self>>()
                    };}
                ) || (
                    remainder<I, J, Self> >= join_sep_size<Sep> &&
                    requires{{std::forward<Self>(self).template arg<J>().
                        template get<quotient<I, J, Self>>().
                        template get<remainder<I, J, Self> - join_sep_size<Sep>>()
                    };}
                )
            )) || (!flatten<J> && !broadcast<J> && requires{
                {std::forward<Self>(self).template arg<J>().template get<I>()} noexcept;
            }) || (broadcast<J> && requires{
                {std::forward<Self>(self).template arg<J>()} noexcept;
            }))
        {
            if constexpr (flatten<J>) {
                constexpr size_t r = remainder<I, J, Self>;
                if constexpr (r < join_sep_size<Sep>) {
                    return (std::forward<Self>(self).template get_sep_impl<r>());
                } else {
                    return (std::forward<Self>(self).template arg<J>().
                        template get<quotient<I, J, Self>>().
                        template get<r - join_sep_size<Sep>>()
                    );
                }
            } else if constexpr (!broadcast<J>) {
                return (std::forward<Self>(self).template arg<J>().template get<I>());
            } else {
                return (std::forward<Self>(self).template arg<J>());
            }
        }

        /* Index `I` does NOT match the `J-th` joined argument. */
        template <size_t I, size_t J, typename Self>
            requires (I >= join_arg_size<meta::unpack_type<J, A...>, Sep>)
        constexpr decltype(auto) _get(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).template get_sep<
                I - join_arg_size<meta::unpack_type<J, A...>, Sep>,
                J + 1
            >()} noexcept;})
            requires (requires{{std::forward<Self>(self).template get_sep<
                I - join_arg_size<meta::unpack_type<J, A...>, Sep>,
                J + 1
            >()};})
        {
            return (std::forward<Self>(self).template get_sep<
                I - join_arg_size<meta::unpack_type<J, A...>, Sep>,
                J + 1
            >());
        }

        /* Index `I` does NOT match the current separator. */
        template <size_t I, size_t J, typename Self> requires (I >= join_sep_size<Sep>)
        constexpr decltype(auto) get_sep(this Self&& self)
            noexcept (requires{{
                std::forward<Self>(self).template _get<I - join_sep_size<Sep>, J>()
            } noexcept;})
            requires (requires{{
                std::forward<Self>(self).template _get<I - join_sep_size<Sep>, J>()
            };})
        {
            return (std::forward<Self>(self).template _get<I - join_sep_size<Sep>, J>());
        }

        constexpr size_type subscript_sep_size() const
            noexcept (
                !meta::range<Sep> ||
                (join_flatten<Sep> && requires{{
                    sep().size() * meta::tuple_size<meta::yield_type<Sep>>
                } noexcept -> meta::nothrow::convertible_to<size_type>;}) ||
                (!join_flatten<Sep> && requires{
                    {sep().size()} noexcept -> meta::nothrow::convertible_to<size_type>;
                })
            )
            requires (meta::not_void<Sep> && (
                !meta::range<Sep> ||
                (join_flatten<Sep> && requires{{
                    sep().size() * meta::tuple_size<meta::yield_type<Sep>>
                } -> meta::convertible_to<size_type>;}) ||
                (!join_flatten<Sep> && requires{
                    {sep().size()} -> meta::convertible_to<size_type>;
                })
            ))
        {
            if constexpr (join_flatten<Sep>) {
                return sep().size() * meta::tuple_size<meta::yield_type<Sep>>;
            } else if constexpr (meta::range<Sep>) {
                return sep().size();
            } else {
                return 1;
            }
        }

        template <size_t J>
        constexpr size_type subscript_size() const
            noexcept (
                broadcast<J> ||
                (
                    flatten<J> &&
                    requires{
                        {arg<J>().size()} noexcept -> meta::nothrow::convertible_to<size_type>;
                    } && (meta::is_void<Sep> || requires{{subscript_sep_size()} noexcept;})
                ) || (
                    !flatten<J> &&
                    requires{
                        {arg<J>().size()} noexcept -> meta::nothrow::convertible_to<size_type>;
                    }
                )
            )
            requires (
                broadcast<J> ||
                (
                    flatten<J> &&
                    requires{
                        {arg<J>().size()} -> meta::convertible_to<size_type>;
                    } && (meta::is_void<Sep> || requires{{subscript_sep_size()};})
                ) || (
                    !flatten<J> &&
                    requires{
                        {arg<J>().size()} -> meta::convertible_to<size_type>;
                    }
                )
            )
        {
            if constexpr (flatten<J>) {
                size_type n = arg<J>().size();
                if constexpr (meta::not_void<Sep>) {
                    return n * unpack_size<J> + (n - (n > 0)) * subscript_sep_size();
                } else {
                    return n * unpack_size<J>;
                }
            } else if constexpr (!broadcast<J>) {
                return arg<J>().size();
            } else {
                return 1;
            }
        }

        template <typename Self> requires (meta::not_void<Sep>)
        constexpr subscript_type<Self> subscript_sep(this Self&& self, size_type i)
            noexcept (
                (join_flatten<Sep> && requires{
                    {
                        std::forward<Self>(self).sep()
                            [i / meta::tuple_size<meta::yield_type<Sep>>()]
                            [i % meta::tuple_size<meta::yield_type<Sep>>()]
                    } noexcept -> meta::nothrow::convertible_to<subscript_type<Self>>;
                }) || (!join_flatten<Sep> && meta::range<Sep> && requires{
                    {
                        std::forward<Self>(self).sep()[i]
                    } noexcept -> meta::nothrow::convertible_to<subscript_type<Self>>;
                }) || (!meta::range<Sep> && requires{
                    {
                        std::forward<Self>(self).sep()
                    } noexcept -> meta::nothrow::convertible_to<subscript_type<Self>>;
                })
            )
            requires (
                (join_flatten<Sep> && requires{
                    {
                        std::forward<Self>(self).sep()
                            [i / meta::tuple_size<meta::yield_type<Sep>>()]
                            [i % meta::tuple_size<meta::yield_type<Sep>>()]
                    } -> meta::convertible_to<subscript_type<Self>>;
                }) || (!join_flatten<Sep> && meta::range<Sep> && requires{
                    {
                        std::forward<Self>(self).sep()[i]
                    } -> meta::convertible_to<subscript_type<Self>>;
                }) || (!meta::range<Sep> && requires{
                    {
                        std::forward<Self>(self).sep()
                    } -> meta::convertible_to<subscript_type<Self>>;
                })
            )
        {
            if constexpr (join_flatten<Sep>) {
                constexpr size_type n = meta::tuple_size<meta::yield_type<Sep>>;
                return std::forward<Self>(self).sep()[i / n][i % n];
            } else if constexpr (meta::range<Sep>) {
                return std::forward<Self>(self).sep()[i];
            } else {
                return std::forward<Self>(self).sep();
            }
        }

        template <size_t J, typename Self> requires (J < sizeof...(A))
        constexpr subscript_type<Self> subscript(this Self&& self, size_type i)
            noexcept (requires{{self.template subscript_size<J>()} noexcept;} && (
                meta::is_void<Sep> || requires{
                    {self.subscript_sep_size()} noexcept;
                    {std::forward<Self>(self).subscript_sep(i)} noexcept;
                }
            ) && (
                (
                    flatten<J> && requires{{
                        std::forward<Self>(self).template arg<J>()[i][i]
                    } noexcept -> meta::nothrow::convertible_to<subscript_type<Self>>;}
                ) || (
                    !flatten<J> && !broadcast<J> &&
                    requires{{
                        std::forward<Self>(self).template arg<J>()[i]
                    } noexcept -> meta::nothrow::convertible_to<subscript_type<Self>>;}
                ) || (
                    broadcast<J> &&
                    requires{{
                        std::forward<Self>(self).template arg<J>()
                    } noexcept -> meta::nothrow::convertible_to<subscript_type<Self>>;}
                )
            ) && (
                J + 1 >= sizeof...(A) || requires{
                    {std::forward<Self>(self).template subscript<J + 1>(i)} noexcept;
                }
            ))
            requires (requires{{self.template subscript_size<J>()};} && (
                meta::is_void<Sep> || requires{
                    {self.subscript_sep_size()};
                    {std::forward<Self>(self).subscript_sep(i)};
                }
            ) && (
                (
                    flatten<J> && requires{{
                        std::forward<Self>(self).template arg<J>()[i][i]
                    } -> meta::convertible_to<subscript_type<Self>>;}
                ) || (
                    !flatten<J> && !broadcast<J> &&
                    requires{{
                        std::forward<Self>(self).template arg<J>()[i]
                    } -> meta::convertible_to<subscript_type<Self>>;}
                ) || (
                    broadcast<J> &&
                    requires{{
                        std::forward<Self>(self).template arg<J>()
                    } -> meta::convertible_to<subscript_type<Self>>;}
                )
            ) && (
                J + 1 >= sizeof...(A) || requires{
                    {std::forward<Self>(self).template subscript<J + 1>(i)};
                }
            ))
        {
            size_type size = self.template subscript_size<J>();
            if (i < size) {
                if constexpr (flatten<J>) {
                    if constexpr (meta::not_void<Sep>) {
                        size_type sep_size = self.subscript_sep_size();
                        i += sep_size;
                        size_type j = i / (sep_size + unpack_size<J>);
                        i %= (sep_size + unpack_size<J>);
                        if (i < sep_size) {
                            return std::forward<Self>(self).subscript_sep(i);
                        }
                        return std::forward<Self>(self).template arg<J>()[j][i - sep_size];
                    } else {
                        constexpr size_type n = unpack_size<J>;
                        return std::forward<Self>(self).template arg<J>()[i / n][i % n];
                    }
                } else if constexpr (!broadcast<J>) {
                    return std::forward<Self>(self).template arg<J>()[i];
                } else {
                    return std::forward<Self>(self).template arg<J>();
                }
            }
            if constexpr (J + 1 < sizeof...(A)) {
                i -= size;
                if constexpr (meta::not_void<Sep>) {
                    size_type sep_size = self.subscript_sep_size();
                    if (i < sep_size) {
                        return std::forward<Self>(self).subscript_sep(i);
                    }
                    i -= sep_size;
                }
                return std::forward<Self>(self).template subscript<J + 1>(i);
            } else {
                throw IndexError();  // unreachable
            }
        }

    public:
        /* Access the `I`-th element of a tuple-like, joined range.  Non-range
        arguments will be forwarded according to the current cvref qualifications of
        the `join` range, while range arguments will be accessed using the provided
        index before forwarding.  If the index is invalid for one or more of the input
        ranges, then this method will fail to compile. */
        template <size_t I, typename Self> requires (tuple_like && I < join_tuple_size<Sep, A...>)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self)
            noexcept (requires{{std::forward<Self>(self).template _get<I, 0>()} noexcept;})
            requires (requires{{std::forward<Self>(self).template _get<I, 0>()};})
        {
            return (std::forward<Self>(self).template _get<I, 0>());
        }

        /* Index into the joined range.  Non-range arguments will be forwarded
        according to the current cvref qualifications of the `join` range, while range
        arguments will be accessed using the provided index before forwarding.  If the
        index is not supported for one or more of the input ranges, then this method
        will fail to compile. */
        template <typename Self>
        [[nodiscard]] constexpr subscript_type<Self> operator[](this Self&& self, size_type i)
            noexcept (requires{{std::forward<Self>(self).template subscript<0>(i)} noexcept;})
            requires (requires{{std::forward<Self>(self).template subscript<0>(i)};})
        {
            return std::forward<Self>(self).template subscript<0>(i);
        }
        
        /* Get a forward iterator over the joined range. */
        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{make_join_iterator{*this}.begin()} noexcept;})
            requires (requires{{make_join_iterator{*this}.begin()};})
        {
            return make_join_iterator{*this}.begin();
        }

        /* Get a forward iterator over the joined range. */
        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{make_join_iterator{*this}.begin()} noexcept;})
            requires (requires{{make_join_iterator{*this}.begin()};})
        {
            return make_join_iterator{*this}.begin();
        }

        /* Get a forward sentinel one past the end of the joined range. */
        [[nodiscard]] constexpr auto end()
            noexcept (requires{{make_join_iterator{*this}.end()} noexcept;})
            requires (requires{{make_join_iterator{*this}.end()};})
        {
            return make_join_iterator{*this}.end();
        }

        /* Get a forward sentinel one past the end of the joined range. */
        [[nodiscard]] constexpr auto end() const
            noexcept (requires{{make_join_iterator{*this}.end()} noexcept;})
            requires (requires{{make_join_iterator{*this}.end()};})
        {
            return make_join_iterator{*this}.end();
        }

        /* Get a reverse iterator over the joined range. */
        [[nodiscard]] constexpr auto rbegin()
            noexcept (requires{{make_join_reversed{*this}.begin()} noexcept;})
            requires (requires{{make_join_reversed{*this}.begin()};})
        {
            return make_join_reversed{*this}.begin();
        }

        /* Get a reverse iterator over the joined range. */
        [[nodiscard]] constexpr auto rbegin() const
            noexcept (requires{{make_join_reversed{*this}.begin()} noexcept;})
            requires (requires{{make_join_reversed{*this}.begin()};})
        {
            return make_join_reversed{*this}.begin();
        }

        /* Get a reverse sentinel one before the beginning of the joined range. */
        [[nodiscard]] constexpr auto rend()
            noexcept (requires{{make_join_reversed{*this}.end()} noexcept;})
            requires (requires{{make_join_reversed{*this}.end()};})
        {
            return make_join_reversed{*this}.end();
        }

        /* Get a reverse sentinel one before the beginning of the joined range. */
        [[nodiscard]] constexpr auto rend() const
            noexcept (requires{{make_join_reversed{*this}.end()} noexcept;})
            requires (requires{{make_join_reversed{*this}.end()};})
        {
            return make_join_reversed{*this}.end();
        }
    };

}


namespace iter {

    template <meta::not_rvalue Sep = void>
    struct join {
    private:
        template <typename... A>
        using container = impl::join<Sep, meta::remove_rvalue<A>...>;

        template <typename... A>
        using range = iter::range<container<A...>>;

    public:
        [[no_unique_address]] Sep sep;

        template <typename Self, typename... A> requires (impl::join_concept<Sep, A...>)
        [[nodiscard]] constexpr auto operator()(this Self&& self, A&&... a)
            noexcept (requires{{range<A...>{container<A...>{.m_storage = {
                .sep = std::forward<Self>(self).sep,
                .args = {std::forward<A>(a)...}
            }}}} noexcept;})
            requires (requires{{range<A...>{container<A...>{.m_storage = {
                .sep = std::forward<Self>(self).sep,
                .args = {std::forward<A>(a)...}
            }}}};})
        {
            return range<A...>{container<A...>{.m_storage = {
                .sep = std::forward<Self>(self).sep,
                .args = {std::forward<A>(a)...}
            }}};
        }
    };

    template <meta::is_void V> requires (meta::not_rvalue<V>)
    struct join<V> {
    private:
        template <typename... A>
        using container = impl::join<void, meta::remove_rvalue<A>...>;

        template <typename... A>
        using range = iter::range<container<A...>>;

    public:
        template <typename Self, typename... A> requires (impl::join_concept<void, A...>)
        [[nodiscard]] constexpr auto operator()(this Self&& self, A&&... a)
            noexcept (requires{{range<A...>{container<A...>{.m_storage = {
                .args = {std::forward<A>(a)...}
            }}}} noexcept;})
            requires (requires{{range<A...>{container<A...>{.m_storage = {
                .args = {std::forward<A>(a)...}
            }}}};})
        {
            return range<A...>{container<A...>{.m_storage = {
                .args = {std::forward<A>(a)...}
            }}};
        }
    };

    template <typename Sep>
    join(Sep&&) -> join<meta::remove_rvalue<Sep>>;

}

    
}


#endif  // BERTRAND_ITER_JOIN_H