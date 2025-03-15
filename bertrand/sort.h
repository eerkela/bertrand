#ifndef BERTRAND_SORT_H
#define BERTRAND_SORT_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/math.h"
#include "bertrand/allocate.h"


namespace bertrand {

namespace impl {

    template <typename Less, typename Begin, typename End>
    concept iter_sortable =
        meta::iterator<Begin> &&
        meta::sentinel_for<End, Begin> &&
        meta::copyable<Begin> &&
        meta::output_iterator<Begin, meta::as_rvalue<meta::dereference_type<Begin>>> &&
        meta::movable<meta::remove_reference<meta::dereference_type<Begin>>> &&
        meta::move_assignable<meta::remove_reference<meta::dereference_type<Begin>>> &&
        meta::destructible<meta::remove_reference<meta::dereference_type<Begin>>> &&
        (
            (
                meta::member_object_of<
                    Less,
                    meta::remove_reference<meta::dereference_type<Begin>>
                > &&
                meta::has_lt<meta::remove_member<Less>, meta::remove_member<Less>>
            ) || (
                meta::member_function_of<
                    Less,
                    meta::remove_reference<meta::dereference_type<Begin>>
                > &&
                meta::invocable<Less, meta::dereference_type<Begin>> &&
                meta::has_lt<
                    meta::invoke_type<Less, meta::dereference_type<Begin>>,
                    meta::invoke_type<Less, meta::dereference_type<Begin>>
                >
            ) || (
                !meta::member<Less> &&
                meta::invoke_returns<
                    bool,
                    meta::as_lvalue<Less>,
                    meta::dereference_type<Begin>,
                    meta::dereference_type<Begin>
                >
            )
        );

    template <typename Less, typename Range>
    concept sortable =
        meta::iterable<Range> &&
        iter_sortable<Less, meta::begin_type<Range>, meta::end_type<Range>>;

    /* A stable, adaptive, k-way merge sort algorithm for contiguous arrays based on
    work by Gelling, Nebel, Smith, and Wild ("Multiway Powersort", 2023), requiring
    O(n) scratch space for rotations.  A full description of the algorithm and its
    benefits can be found at:

        [1] https://www.wild-inter.net/publications/html/cawley-gelling-nebel-smith-wild-2023.pdf.html

    An earlier, 2-way version of this algorithm is currently in use as the default
    CPython sorting backend for the `sorted()` operator and `list.sort()` method as of
    Python 3.11.  A more complete introduction to that algorithm and how it relates to
    the newer 4-way version can be found in Munro, Wild ("Nearly-Optimal Mergesorts:
    Fast, Practical Sorting Methods That Optimally Adapt to Existing Runs", 2018):

        [2] https://www.wild-inter.net/publications/html/munro-wild-2018.pdf.html

    A full reference implementation for both of these algorithms is available at:

        [3] https://github.com/sebawild/powersort

    The k-way version presented here is adapted from the above implementations with the
    following changes:

        a)  The algorithm works on arbitrary ranges, not just random access iterators.
            If the iterator type does not support O(1) distance calculations, then a
            `std::ranges::distance()` call will be used to determine the initial size
            of the range.  All other iterator operations will be done in constant time.
        b)  A custom `less_than` predicate can be provided to the algorithm, which
            allows for sorting based on custom comparison functions, including
            lambdas, user-defined comparators, and pointers to members.
        c)  Merges are safe against exceptions thrown by the comparison function, and
            will attempt to transfer partially-sorted runs back into the output range
            via RAII.
        d)  Proper move semantics are used to transfer objects to and from the scratch
            space, instead of requiring the type to be default constructible and/or
            copyable.
        e)  The algorithm is generalized to arbitrary `k >= 2`, with a default value of
            4, in accordance with [2].  Higher `k` will asymptotically reduce the
            number of comparisons needed to sort the array by a factor of `log2(k)`, at
            the expense of deeper tournament trees.  There is likely an architecture-
            dependent sweet spot based on the size of the data and the cost of
            comparisons for a given type.  Further investigation is needed to determine
            the optimal value of `k` for a given situation, as well as possibly allow
            dynamic tuning based on the input data.
        f)  All tournament trees are swapped from winner trees to loser trees, which
            reduces branching in the inner loop and simplifies the implementation.
        g)  Sentinel values will be used by default if
            `std::numeric_limits<T>::has_infinity == true`, which maximizes performance
            as demonstrated in [2].

    Otherwise, the algorithm is designed to be a drop-in replacement for `std::sort`
    and `std::stable_sort`, and is generally competitive with or better than those
    algorithms, sometimes by a significant margin if any of the following conditions
    are true:

        1.  The data is already partially sorted, or is naturally ordered in
            ascending/descending runs.
        2.  Comparisons are expensive, such as for strings or complex objects.
        3.  The data has a sentinel value, expressed as
            `std::numeric_limits<T>::infinity()`.

    NOTE: the `min_run` template parameter dictates the minimum run length under
    which insertion sort will be used to grow the run.  [2] sets this to a default of
    24, which is replicated here.  Like `k`, it can be tuned based on the data. */
    template <size_t k = 4, size_t min_run = 24> requires (k >= 2 && min_run > 0)
    struct powersort {
    private:

        template <typename Begin, typename End, typename Less>
            requires (
                !meta::reference<Begin> &&
                !meta::reference<End> &&
                !meta::reference<Less>
            )
        struct merge_tree {
        private:
            using value_type = meta::remove_reference<meta::dereference_type<Begin>>;
            using pointer = meta::as_pointer<meta::dereference_type<Begin>>;
            using numeric = std::numeric_limits<meta::unqualify<value_type>>;
            static constexpr bool destroy = !meta::trivially_destructible<value_type>;

            /* Scratch space is allocated as an uninitialized buffer using `malloc()`
            and `free()` so as not to impose default constructibility on `value_type`. */
            struct deleter {
                static constexpr void operator()(pointer p) noexcept {
                    std::free(p);
                }
            };

            struct run {
                Begin iter;  // iterator to start of run
                size_t start;  // first index of the run
                size_t stop;  // one past last index of the run
                size_t power = 0;

                /* Initialize sentinel run. */
                constexpr run(Begin& iter, size_t start, size_t stop) :
                    iter(iter),
                    start(start),
                    stop(stop)
                {}

                /* Detect the next run beginning at `start` and not exceeding `stop`.
                `iter` is an iterator to the start index, which will be advanced to
                the end of the detected run as an out parameter.  If the run is shorter
                than the minimum length, it will be grown to the minimum length using
                insertion sort. */
                constexpr run(
                    Less& less_than,
                    pointer scratch,
                    Begin& iter,
                    size_t start,
                    size_t size
                ) :
                    iter(iter),
                    start(start),
                    stop(start)
                {
                    if (stop < size && ++stop < size) {
                        Begin next = iter;
                        ++next;
                        if (less_than(*next, *iter)) {  // strictly decreasing
                            do {
                                ++iter;
                                ++next;
                            } while (++stop < size && less_than(*next, *iter));

                            reverse(scratch, iter);

                        } else {  // weakly increasing
                            do {
                                ++iter;
                                ++next;
                            } while (++stop < size && !less_than(*next, *iter));
                        }
                    }

                    /// grow the run to minimum length
                    grow(less_than, scratch, iter, size);
                }

            private:

                constexpr void reverse(pointer scratch, Begin& iter) {
                    // if the iterator is bidirectional, then we can do an O(n / 2)
                    // pairwise swap
                    if constexpr (meta::bidirectional_iterator<Begin>) {
                        std::ranges::reverse(this->iter, iter);

                    // otherwise, if the iterator is forward-only, we have to do an
                    // O(2 * n) move into scratch space and then move back.
                    } else {
                        pointer begin = scratch;
                        pointer end = scratch + (stop - start);
                        Begin i = this->iter;
                        while (begin < end) {
                            new (begin++) value_type(std::move(*i++));
                        }
                        Begin j = this->iter;
                        while (end-- > scratch) {
                            *j = std::move(*(end));
                            if constexpr (destroy) { end->~value_type(); }
                            ++j;
                        }
                    }
                }

                constexpr void grow(
                    Less& less_than,
                    pointer scratch,
                    Begin& unsorted,
                    size_t size
                ) {
                    size_t limit = std::min(start + min_run, size);

                    // if the iterator is bidirectional, then we can rotate in-place
                    // from right to left
                    if constexpr (meta::bidirectional_iterator<Begin>) {
                        constexpr auto compare = [](
                            Less& less_than,
                            value_type& temp,
                            Begin& prev,
                            Begin& curr
                        ) {
                            try {
                                return less_than(temp, *prev);
                            } catch (...) {
                                *curr = std::move(temp);  // fill hole
                                throw;
                            }
                        };

                        while (stop < limit) {
                            // if the unsorted element is less than the previous
                            // element, we need to rotate it into the correct position
                            Begin prev = unsorted;
                            --prev;
                            if (less_than(*unsorted, *prev)) {
                                value_type temp = std::move(*unsorted);
                                Begin curr = unsorted;
                                size_t idx = stop;

                                // rotate hole to the left until we find a proper
                                // insertion point, recovering on error
                                while (idx > start && compare(less_than, temp, prev, curr)) {
                                    *curr-- = std::move(*prev--);
                                    --idx;
                                }

                                // fill hole at insertion point
                                *curr = std::move(temp);
                            }
                            ++unsorted;
                            ++stop;
                        }

                    // otherwise, we have to scan the sorted portion from left to right
                    // and move into scratch space to do a rotation
                    } else {
                        while (stop < limit) {
                            // scan sorted portion for insertion point
                            Begin curr = this->iter;
                            size_t idx = start;
                            while (idx < stop) {
                                // stop at the first element that is strictly greater
                                // than the unsorted element
                                if (less_than(*unsorted, *curr)) {
                                    // move subsequent elements into scratch space
                                    Begin temp = curr;
                                    pointer p = scratch;
                                    pointer p2 = scratch + stop - idx;
                                    while (p < p2) {
                                        new (p++) value_type(std::move(*temp++));
                                    }

                                    // move unsorted element to insertion point
                                    *curr++ = std::move(*unsorted);

                                    // move intervening elements back
                                    p = scratch;
                                    p2 = scratch + stop - idx;
                                    while (p < p2) {
                                        *curr++ = std::move(*p);
                                        if constexpr (destroy) { p->~value_type(); }
                                        ++p;
                                    }
                                    break;
                                }
                                ++curr;
                                ++idx;
                            }
                            ++unsorted;
                            ++stop;
                        }
                    }
                }
            };

            /* An exception-safe tournament tree generalized to arbitrary `N >= 2`.  If
            an error occurs during a comparison, then all runs will be transferred back
            into the output range in partially-sorted order via RAII. */
            template <size_t N, bool = numeric::has_infinity> requires (N >= 2)
            struct tournament_tree {
                static constexpr size_t R = N + (N % 2);
                Less& less_than;
                pointer scratch;
                Begin output;
                size_t size;
                std::array<pointer, R> begin;  // begin iterators for each run
                std::array<pointer, R> end;  // end iterators for each run
                std::array<size_t, R - 1> internal;  // internal nodes of tree
                size_t winner;  // leaf index of overall winner

                /// NOTE: the tree is represented as a loser tree, meaning the internal
                /// nodes store the leaf index of the losing run for that subtree, and
                /// the winner is bubbled up to the next level of the tree.  The root
                /// of the tree (runner-up) is always the first element of the
                /// `internal` buffer, and each subsequent level is compressed into the
                /// next 2^i elements, from left to right.  The last layer will be
                /// incomplete if `N` is not a power of two.

                template <meta::is<run>... Runs> requires (sizeof...(Runs) == N)
                constexpr tournament_tree(
                    Less& less_than,
                    pointer scratch,
                    Runs&... runs
                ) :
                    less_than(less_than),
                    scratch(scratch),
                    output(meta::unpack_arg<0>(runs...).iter),
                    size((0 + ... + (runs.stop - runs.start))),
                    begin([&]<size_t... Is>(std::index_sequence<Is...>) {
                        run& first = meta::unpack_arg<0>(runs...);
                        if constexpr (N % 2) {
                            return std::array<pointer, R>{
                                (scratch + (runs.start - first.start) + Is)...,
                                (scratch + size + N)  // extra sentinel
                            };
                        } else {
                            return std::array<pointer, R>{
                                (scratch + (runs.start - first.start) + Is)...
                            };
                        }
                    }(std::make_index_sequence<N>{})),
                    end(begin)
                {
                    // move all runs into scratch space.  Afterwards, the end iterators
                    // will point to the sentinel values for each run
                    [&]<size_t I = 0>(this auto&& self) {
                        if constexpr (I < R - 1) {
                            internal[I] = R;  // nodes are initialized to empty value
                        }

                        if constexpr (I < N) {
                            run& r = meta::unpack_arg<I>(runs...);
                            for (size_t i = r.start; i < r.stop; ++i) {
                                new (end[I]++) value_type(std::move(*r.iter++));
                            }
                            new (end[I]) value_type(numeric::infinity());
                            std::forward<decltype(self)>(self).template operator()<I + 1>();

                        // if `N` is odd, then we have to insert an additional
                        // sentinel at the end of the scratch space to give each
                        // internal node exactly two children
                        } else if constexpr (begin.size()) {
                            new (end[I]) value_type(numeric::infinity());
                        }
                    }();
                }

                /* Perform the merge. */
                constexpr void operator()() {
                    // Initialize the tournament tree
                    //                internal[0]               internal nodes store
                    //            /                \            losing leaf indices.
                    //      internal[1]          internal[2]    Leaf nodes store
                    //      /       \             /       \     scratch iterators
                    //         ...                   ...
                    // begin[0]   begin[1]   begin[2]   begin[3]   ...
                    for (size_t i = 0; i < R - 1; ++i) {
                        winner = i;
                        size_t node = i + (R - 1);
                        while (node > 0) {
                            size_t parent = (node - 1) / 2;
                            size_t loser = internal[parent];

                            // parent may be uninitialized, in which case the current
                            // node automatically loses
                            if (loser == R) {
                                internal[parent] = node;
                                break;  // ancestors are guaranteed to be empty
                            }

                            // otherwise, if the current winner loses against the
                            // parent, then we swap it and continue bubbling up
                            if (less_than(*begin[loser], *begin[winner])) {
                                internal[parent] = winner;
                                winner = loser;
                            }

                            node = parent;
                        }
                    }

                    // merge runs according to tournament tree
                    for (size_t i = 0; i < size; ++i) {
                        // move the overall winner into the output range
                        *output++ = std::move(*begin[winner]);
                        if constexpr (destroy) { begin[winner]->~value_type(); }
                        ++begin[winner];

                        // bubble up next winner
                        size_t node = winner + (R - 1);
                        while (node > 0) {
                            size_t parent = (node - 1) / 2;
                            size_t loser = internal[parent];
                            if (less_than(*begin[loser], *begin[winner])) {
                                internal[parent] = winner;
                                winner = loser;
                            }
                            node = parent;
                        }
                    }
                }

                /* If an error occurs during comparison, attempt to move the
                unprocessed portions of each run back into the output range and destroy
                sentinels. */
                constexpr ~tournament_tree() {
                    for (size_t i = 0; i < begin.size(); ++i) {
                        while (begin[i] != end[i]) {
                            *output++ = std::move(*begin[i]);
                            if constexpr (destroy) { begin[i]->~value_type(); }
                            ++begin[i];
                        }
                        if constexpr (destroy) { end[i]->~value_type(); }
                    }
                }
            };

            /* A specialized tournament tree for when the underlying type does not
            have a +inf sentinel value to guard comparisons.  Instead, this uses extra
            boundary checks and merges in stages, where `N` steadily decreases as runs
            are fully consumed. */
            template <size_t N> requires (N >= 2)
            struct tournament_tree<N, false> {
                Less& less_than;
                pointer scratch;
                Begin output;
                size_t size;
                std::array<pointer, N> begin;  // begin iterators for each run
                std::array<pointer, N> end;  // end iterators for each run
                std::array<size_t, N - 1 - (N % 2)> internal;  // internal nodes of tree
                size_t winner;  // leaf index of overall winner
                size_t smallest = std::numeric_limits<size_t>::max();  // length of smallest non-empty run

                /// NOTE: this specialization plays tournaments in `N - 1` distinct
                /// stages, where each stage ends when `smallest` reaches zero.  At
                /// At that point, empty runs are removed, and a smaller tournament
                /// tree is constructed with the remaining runs.  This continues until
                /// `N == 2`, in which case we proceed as for a binary merge.

                /// NOTE: because we can't pad the runs with an extra sentinel if N is
                /// odd, the last node in the `internal` array may be unbalanced, with
                /// only a single child.  This is mitigated by simply omitting that
                /// node and causing the leaf that would have been its only child to
                /// skip it during initialization/update of the tournament tree.

                template <meta::is<run>... Runs> requires (sizeof...(Runs) == N)
                constexpr tournament_tree(
                    Less& less_than,
                    pointer scratch,
                    Runs&... runs
                ) :
                    less_than(less_than),
                    scratch(scratch),
                    output(meta::unpack_arg<0>(runs...).iter),
                    size((0 + ... + (runs.stop - runs.start))),
                    begin{(scratch + (runs.start - meta::unpack_arg<0>(runs...).start))...},
                    end(begin)
                {
                    // move all runs into scratch space, without any extra sentinels.
                    // Record the minimum length of each run for the the first stage.
                    [&]<size_t I = 0>(this auto&& self) {
                        if constexpr (I < (N - 1 - (N % 2))) {
                            internal[I] = N;  // nodes are initialized to sentinel
                        }

                        if constexpr (I < N) {
                            run& r = meta::unpack_arg<I>(runs...);
                            for (size_t i = r.start; i < r.stop; ++i) {
                                new (end[I]++) value_type(std::move(*r.iter++));
                            }
                            smallest = std::min(smallest, r.stop - r.start);
                            std::forward<decltype(self)>(self).template operator()<I + 1>();
                        }
                    }();
                }

                /* Perform the merge. */
                constexpr void operator()() {
                    [&]<size_t I = N>(this auto&& self) {
                        if constexpr (I > 2) {
                            initialize<I>();  // build tournament tree for this stage
                            while (smallest) {
                                merge<I>();  // continue until a run is exhausted
                            }
                            advance<I>();  // pop empty run for next stage and recur
                            std::forward<decltype(self)>(self).template operator()<I - 1>();
                        }
                    }();

                    // finish with a binary merge
                    while (begin[0] != end[0] && begin[1] != end[1]) {
                        bool less = less_than(*begin[1], *begin[0]);
                        *output++ = std::move(*begin[less]);
                        if constexpr (destroy) { begin[less]->~value_type(); }
                        ++begin[less];
                    }
                    while (begin[0] != end[0]) {
                        *output++ = std::move(*begin[0]);
                        if constexpr (destroy) { begin[0]->~value_type(); }
                        ++begin[0];
                    }
                    while (begin[1] != end[1]) {
                        *output++ = std::move(*begin[1]);
                        if constexpr (destroy) { begin[1]->~value_type(); }
                        ++begin[1];
                    }
                }

                constexpr ~tournament_tree() {
                    for (size_t i = 0; i < begin.size(); ++i) {
                        while (begin[i] != end[i]) {
                            *output++ = std::move(*begin[i]);
                            if constexpr (destroy) { begin[i]->~value_type(); }
                            ++begin[i];
                        }
                    }
                }

            private:

                /* Regenerate the tournament tree for the next stage. */
                template <size_t I>
                constexpr void initialize() {
                    for (size_t i = 0; i < I - 1; ++i) {
                        winner = i;
                        size_t node = i + (I - 1);
                        while (node > 0) {
                            size_t parent = (node - 1) / 2;
                            if constexpr (I % 2) {
                                if (parent == I - 2) {
                                    parent = (parent - 1) / 2;  // skip unbalanced node
                                }
                            }
                            size_t loser = internal[parent];

                            // parent may be uninitialized, in which case the current
                            // node automatically loses
                            if (loser == N) {
                                internal[parent] = node;
                                break;  // ancestors are guaranteed to be empty
                            }

                            // otherwise, if the current winner loses against the
                            // parent, then we swap it and continue bubbling up
                            if (less_than(*begin[loser], *begin[winner])) {
                                internal[parent] = winner;
                                winner = loser;
                            }

                            node = parent;
                        }
                    }
                }

                /* Move the winner of the tournament tree into output and update the
                tree. */
                template <size_t I>
                constexpr void merge() {
                    // we can safely do `smallest` iterations before needing to check
                    // bounds
                    for (size_t i = 0; i < smallest; ++i) {
                        // move the overall winner into the output range
                        *output++ = std::move(*begin[winner]);
                        if constexpr (destroy) { begin[winner]->~value_type(); }
                        ++begin[winner];

                        // bubble up next winner
                        size_t node = winner + (I - 1);
                        while (node > 0) {
                            size_t parent = (node - 1) / 2;
                            if constexpr (I % 2) {
                                if (parent == I - 2) {
                                    parent = (parent - 1) / 2;  // skip unbalanced node
                                }
                            }
                            size_t loser = internal[parent];
                            if (less_than(*begin[loser], *begin[winner])) {
                                internal[parent] = winner;
                                winner = loser;
                            }
                            node = parent;
                        }
                    }

                    // Update `smallest` to the minimum length of all non-empty runs.
                    // If the result is zero, then it marks the end of the current
                    // stage.  Otherwise, it is the number of safe iterations that can
                    // be done before another boundscheck must be performed.
                    smallest = std::numeric_limits<size_t>::max();
                    for (size_t i = 0; i < I; ++i) {
                        smallest = std::min(smallest, size_t(end[i] - begin[i]));
                    }
                }

                /* End a merge stage by pruning empty runs, resetting the tournament
                tree, and recomputing the minimum length for the next stage.  */
                template <size_t I>
                constexpr void advance() {
                    smallest = std::numeric_limits<size_t>::max();
                    for (size_t i = 0; i < I - 1;) {
                        // if empty, pop from the array and left shift subsequent runs
                        size_t len = end[i] - begin[i];
                        if (!len) {
                            for (size_t j = i + 1; j < I; ++j) {
                                begin[j - 1] = begin[j];
                                end[j - 1] = end[j];
                            }

                        // otherwise, record the minimum length
                        } else {
                            smallest = std::min(smallest, len);
                            if (i < I - 2 - (I % 2)) {
                                internal[i] = N;  // reset internal nodes to sentinel
                            }
                            ++i;
                        }
                    }
                }
            };

            /* An optimized tournament tree for binary merges with sentinel values. */
            template <>
            struct tournament_tree<2, true> {
                Less& less_than;
                pointer scratch;
                Begin output;
                size_t size;
                std::array<pointer, 2> begin;
                std::array<pointer, 2> end;

                constexpr tournament_tree(
                    Less& less_than,
                    pointer scratch,
                    run& left,
                    run& right
                ) :
                    less_than(less_than),
                    scratch(scratch),
                    output(left.iter),
                    size((left.stop - left.start) + (right.stop - right.start)),
                    begin{scratch, scratch + (left.stop - left.start) + 1},
                    end(begin)
                {
                    for (size_t i = left.start; i < left.stop; ++i) {
                        new (end[0]++) value_type(std::move(*left.iter++));
                    }
                    new (end[0]) value_type(numeric::infinity());

                    for (size_t i = right.start; i < right.stop; ++i) {
                        new (end[1]++) value_type(std::move(*right.iter++));
                    }
                    new (end[1]) value_type(numeric::infinity());
                }

                constexpr void operator()() {
                    for (size_t i = 0; i < size; ++i) {
                        bool less = less_than(*begin[1], *begin[0]);
                        *output++ = std::move(*begin[less]);
                        if constexpr (destroy) { begin[less]->~value_type(); }
                        ++begin[less];
                    }
                }

                constexpr ~tournament_tree() {
                    for (size_t i = 0; i < begin.size(); ++i) {
                        while (begin[i] != end[i]) {
                            *output++ = std::move(*begin[i]);
                            if constexpr (destroy) { begin[i]->~value_type(); }
                            ++begin[i];
                        }
                        if constexpr (destroy) { end[i]->~value_type(); }
                    }
                }
            };

            /* An optimized tournament tree for binary merges without sentinel values. */
            template <>
            struct tournament_tree<2, false> {
                Less& less_than;
                pointer scratch;
                Begin output;
                size_t size;
                std::array<pointer, 2> begin;
                std::array<pointer, 2> end;

                constexpr tournament_tree(
                    Less& less_than,
                    pointer scratch,
                    run& left,
                    run& right
                ) :
                    less_than(less_than),
                    scratch(scratch),
                    output(left.iter),
                    size((left.stop - left.start) + (right.stop - right.start)),
                    begin{scratch, scratch + (left.stop - left.start)},
                    end(begin)
                {
                    for (size_t i = left.start; i < left.stop; ++i) {
                        new (end[0]++) value_type(std::move(*left.iter++));
                    }
                    for (size_t i = right.start; i < right.stop; ++i) {
                        new (end[1]++) value_type(std::move(*right.iter++));
                    }
                }

                constexpr void operator()() {
                    while (begin[0] < end[0] && begin[1] < end[1]) {
                        bool less = less_than(*begin[1], *begin[0]);
                        *output++ = std::move(*begin[less]);
                        if constexpr (destroy) { begin[less]->~value_type(); }
                        ++begin[less];
                    }
                    while (begin[0] < end[0]) {
                        *output++ = std::move(*begin[0]);
                        if constexpr (destroy) { begin[0]->~value_type(); }
                        ++begin[0];
                    }
                    while (begin[1] < end[1]) {
                        *output++ = std::move(*begin[1]);
                        if constexpr (destroy) { begin[1]->~value_type(); }
                        ++begin[1];
                    }
                }

                constexpr ~tournament_tree() {
                    for (size_t i = 0; i < begin.size(); ++i) {
                        while (begin[i] != end[i]) {
                            *output++ = std::move(*begin[i]);
                            if constexpr (destroy) { begin[i]->~value_type(); }
                            ++begin[i];
                        }
                        if constexpr (destroy) { end[i]->~value_type(); }
                    }
                }
            };

            static constexpr size_t ceil_log4(size_t n) noexcept {
                return (impl::log2(n - 1) >> 1) + 1;
            }

            static constexpr size_t get_power(
                size_t n,
                size_t prev_start,
                size_t next_start,
                size_t next_stop
            ) noexcept {
                /// NOTE: these implementations are taken straight from the reference,
                /// and have only been lightly edited for readability.

                // if a built-in compiler intrinsic is available, use it
                #if defined(__GNUC__) || defined(__clang__)
                    size_t l = prev_start + next_start;
                    size_t r = next_start + next_stop;
                    size_t a = (l << 30) / n;
                    size_t b = (r << 30) / n;
                    if constexpr (sizeof(size_t) <= sizeof(unsigned int)) {
                        return ((__builtin_clz(a ^ b) - 1) >> 1) + 1;
                    } else if constexpr (sizeof(size_t) <= sizeof(unsigned long)) {
                        return ((__builtin_clzl(a ^ b) - 1) >> 1) + 1;
                    } else if constexpr (sizeof(size_t) <= sizeof(unsigned long long)) {
                        return ((__builtin_clzll(a ^ b) - 1) >> 1) + 1;
                    }

                #elif defined(_MSC_VER)
                    size_t l = prev_start + next_start;
                    size_t r = next_start + next_stop;
                    size_t a = (l << 30) / n;
                    size_t b = (r << 30) / n;
                    unsigned long index;
                    if constexpr (sizeof(size_t) <= sizeof(unsigned long)) {
                        _BitScanReverse(&index, a ^ b);
                    } else if constexpr (sizeof(size_t) <= sizeof(uint64_t)) {
                        _BitScanReverse64(&index, a ^ b);
                    }
                    return ((index - 1) >> 1) + 1;

                #else
                    size_t l = prev_start + next_start;
                    size_t r = next_start + next_stop;
                    size_t n_common_bits = 0;
                    bool digit_a = l >= n;
                    bool digit_b = r >= n;
                    while (digit_a == digit_b) {
                        ++n_common_bits;
                        if (digit_a) {
                            l -= n;
                            r -= n;
                        }
                        l *= 2;
                        r *= 2;
                        digit_a = l >= n;
                        digit_b = r >= n;
                    }
                    return (n_common_bits >> 1) + 1;
                #endif
            }

            size_t size;
            std::unique_ptr<value_type, deleter> scratch;
            std::vector<run> stack;

        public:

            /* Allocate stack and scratch space for a range of the given size. */
            constexpr merge_tree(size_t size) :
                size(size),
                scratch(
                    reinterpret_cast<pointer>(
                        std::malloc(sizeof(value_type) * (size + k + 1))
                    ),
                    deleter{}
                )
            {
                if (!scratch) {
                    throw MemoryError("failed to allocate scratch space");
                }
                stack.reserve((k - 1) * (ceil_log4(size) + 1));
            }

            /* Execute the sorting algorithm. */
            constexpr void operator()(Begin& begin, Less& less_than) {
                stack.emplace_back(begin, 0, size);  // power 0 as sentinel entry

                // identify the first weakly increasing or strictly decreasing run
                // starting at `begin` and grow to minimum length using insertion sort
                run prev {less_than, scratch.get(), begin, 0, size};
                while (prev.stop < size) {
                    run next {less_than, scratch.get(), begin, prev.stop, size};

                    // compute previous run's power with respect to next run
                    prev.power = get_power(
                        size,
                        prev.start,
                        next.start,
                        next.stop
                    );

                    // invariant: powers on stack weakly increase from bottom to top.
                    // If violated, merge runs with equal power into `prev` until
                    // invariant is restored.  Only at most the top 3 runs will ever
                    // meet this criteria due to the structure of the merge tree.
                    while (stack.back().power > prev.power) {
                        run* top = &stack.back();
                        size_t same_power = 1;
                        while ((top - same_power)->power == top->power) {
                            ++same_power;
                        }
                        using F = void(*)(Less&, pointer, std::vector<run>&, run&);
                        using VTable = std::array<F, k - 1>;
                        constexpr VTable vtable = []<size_t... Is>(std::index_sequence<Is...>) {
                            // 0: 2-way
                            // 1: 3-way
                            // 2: 4-way
                            // ...
                            // (k-2): k-way
                            return VTable{[]<size_t... Js>(std::index_sequence<Js...>) {
                                // Is... => [0, k - 2] (inclusive)
                                // Js... => [0, Is] (inclusive)
                                return +[](
                                    Less& less_than,
                                    pointer scratch,
                                    std::vector<run>& stack,
                                    run& prev
                                ) {
                                    constexpr size_t I = Is;
                                    Begin temp = stack[stack.size() - I - 1].iter;
                                    tournament_tree<I + 2>{
                                        less_than,
                                        scratch,
                                        stack[stack.size() - I + Js - 1]...,
                                        prev
                                    }();
                                    prev.iter = temp;
                                };
                            }(std::make_index_sequence<Is + 1>{})...};
                        }(std::make_index_sequence<k - 1>{});

                        // merge runs with equal power by dispatching to vtable
                        vtable[same_power - 1](less_than, scratch.get(), stack, prev);
                        stack.erase(stack.end() - same_power, stack.end());  // pop merged runs
                    }

                    // push next run onto stack
                    stack.emplace_back(std::move(prev));
                    prev = std::move(next);
                }

                // Because runs typically increase in size exponentially as the stack
                // is emptied, we can manually merge the first few such that the stack
                // size is reduced to a multiple of `k - 1`, so that we can do `k`-way
                // merges the rest of the way.  This maximizes the benefit of the
                // tournament tree and minimizes total comparisons.
                using F = void(*)(Less&, pointer, std::vector<run>&, run&);
                using VTable = std::array<F, k - 1>;
                constexpr VTable vtable = []<size_t... Is>(std::index_sequence<Is...>) {
                    return VTable{
                        // 0: do nothing
                        +[](
                            Less& less_than,
                            pointer scratch,
                            std::vector<run>& stack,
                            run& prev
                        ) {},
                        // 1: 2-way
                        // 2: 3-way,
                        // 3: 4-way,
                        // ...
                        // (k-2): (k-1)-way
                        []<size_t... Js>(std::index_sequence<Js...>) {
                            /// Is... => [0, k - 2] (inclusive)
                            // Js... => [0, Is] (inclusive)
                            return +[](
                                Less& less_than,
                                pointer scratch,
                                std::vector<run>& stack,
                                run& prev
                            ) {
                                constexpr size_t I = Is;
                                tournament_tree<I + 2>{
                                    less_than,
                                    scratch,
                                    stack[stack.size() - I + Js - 1]...,
                                    prev
                                }();
                                prev.iter = stack[stack.size() - I - 1].iter;
                                stack.erase(stack.end() - I - 1, stack.end());  // pop merged runs
                            };
                        }(std::make_index_sequence<Is + 1>{})...
                    };
                }(std::make_index_sequence<k - 2>{});

                // vtable is only consulted for the first merge, after which we
                // devolve to k-way merges
                vtable[stack.size() % (k - 1)](less_than, scratch.get(), stack, prev);
                while (stack.size()) {
                    [&]<size_t... Is>(std::index_sequence<Is...>) {
                        tournament_tree<k>{
                            less_than,
                            scratch.get(),
                            stack[stack.size() - (k - 1) + Is]...,
                            prev
                        }();
                        prev.iter = stack[stack.size() - (k - 1)].iter;
                        stack.erase(stack.end() - (k - 1), stack.end());  // pop merged runs
                    }(std::make_index_sequence<k - 1>{});
                }
            }
        };

        template <typename Begin, meta::member T>
            requires (!meta::reference<Begin> && !meta::reference<T>)
        struct sort_by_member {
            T member;
            constexpr bool operator()(auto&& l, auto&& r) const noexcept {
                if constexpr (meta::member_function<T>) {
                    return ((l.*member)()) < ((r.*member)());
                } else {
                    return (l.*member) < (r.*member);
                }
            }
        };

    public:

        /* Execute the sort algorithm using unsorted values in the range [begin, end)
        and placing the result back into the same range.

        The `less_than` comparison function is used to determine the order of the
        elements.  It may be a pointer to an arbitrary member of the iterator's value
        type, in which case only that member will be compared.  Otherwise, it must be a
        function with the signature `bool(const T&, const T&)` where `T` is the value
        type of the iterator.  If no comparison function is given, it will default to a
        transparent `<` operator for each element.

        If an exception occurs during a comparison, the input range will be left in a
        valid but unspecified state, and may be partially sorted.  Any other exception
        (e.g. in a move constructor/assignment operator, destructor, or iterator
        operation) may result in undefined behavior. */
        template <typename Begin, typename End, typename Less = std::less<>>
            requires (impl::iter_sortable<Less, Begin, End>)
        static constexpr void operator()(Begin begin, End end, Less&& less_than = {}) {
            using B = meta::remove_reference<Begin>;
            using E = meta::remove_reference<End>;
            using L = meta::remove_reference<Less>;

            // get overall length of range (possibly O(n) if iterators do not support
            // O(1) distance)
            auto length = std::ranges::distance(begin, end);
            if (length < 2) {
                return;  // trivially sorted
            }

            // convert member pointers into proper comparisons
            if constexpr (meta::member<Less>) {
                using C = sort_by_member<B, L>;
                C cmp {std::forward<Less>(less_than)};
                merge_tree<B, E, C>{size_t(length)}(begin, cmp);
            } else {
                merge_tree<B, E, L>{size_t(length)}(begin, less_than);
            }
        }

        /* An equivalent of the iterator-based call operator that accepts a range and
        uses its `size()` to deduce the length of the range. */
        template <typename Range, typename Less = std::less<>>
            requires (impl::sortable<Less, Range>)
        static constexpr void operator()(Range& range, Less&& less_than = {}) {
            using B = meta::remove_reference<meta::begin_type<Range>>;
            using E = meta::remove_reference<meta::end_type<Range>>;
            using L = meta::remove_reference<Less>;

            // get overall length of range (possibly O(n) if the range is not
            // explicitly sized and iterators do not support O(1) distance)
            auto length = std::ranges::distance(range);
            if (length < 2) {
                return;  // trivially sorted
            }
            auto begin = std::ranges::begin(range);

            // convert member pointers into proper comparisons
            if constexpr (meta::member<Less>) {
                using C = sort_by_member<B, L>;
                C cmp {std::forward<Less>(less_than)};
                merge_tree<B, E, C>{size_t(length)}(begin, cmp);
            } else {
                merge_tree<B, E, L>{size_t(length)}(begin, less_than);
            }
        }
    };


}  // namespace impl


/* Sort an arbitrary range using an optimized, implementation-specific sorting
algorithm.

If the input range has a member `.sort()` method, this function will invoke it with the
given arguments.  Otherwise, it will fall back to a generalized sorting algorithm that
works on arbitrary output ranges, sorting them in-place.  The generalized algorithm
accepts an optional `less_than` comparison function, which can be used to provide
custom sorting criteria.  Such a function can be supplied as a function pointer,
lambda, or custom comparator type with the signature `bool(const T&, const T&)`
where `T` is the value type of the range.  Alternatively, it can also be supplied as
a pointer to a member of the value type or a member function that is callable without
arguments (i.e. a getter), in which case only that member will be considered for
comparison.  If no comparison function is given, it will default to a transparent `<`
operator for each pair of elements.

Currently, the default sorting algorithm is implemented as a heavily optimized,
run-adaptive, stable merge sort variant with a `k`-way powersort policy.  It requires
best case O(n) time due to optimal run detection and worst case O(n log n) time thanks
to a tournament tree that minimizes comparisons.  It needs O(n) extra scratch space,
and can work on arbitrary input ranges.  It is generally faster than `std::sort()` in
most cases, and has far fewer restrictions on its use.  Users should only need to
implement a custom member `.sort()` method if there is a better algorithm for a
particular type (which should be rare), or if they wish to embed it as a member method
for convenience.  In the latter case, users should call the powersort implemtation
directly to guard against infinite recursion, as follows:

```cpp

    template <bertrand::impl::sortable<MyType> Less = std::less<>>
    void MyType::sort(Less&& less_than = {}) {
        bertrand::impl::powersort<k, min_run_length>{}(*this, std::forward<Less>(less));
    }

```

The `impl::sortable<Less, MyType>` concept encapsulates all of the requirements for
sorting based on any of the predicates described above, and enforces them at compile
time, while `std::less<>` defaults to a transparent comparison. */
template <typename Range, impl::sortable<Range> Less = std::less<>>
    requires (!meta::has_member_sort<Range, Less>)
constexpr void sort(Range&& range, Less&& less_than = {}) noexcept(
    noexcept(impl::powersort{}(std::forward<Range>(range), std::forward<Less>(less_than)))
) {
    impl::powersort{}(std::forward<Range>(range), std::forward<Less>(less_than));
}


/* ADL version of `sort()`, which delegates to the implementation-specific
`range.sort()`.  All arguments as well as the return type (if any) will be perfectly
forwarded to that method.  */
template <typename Range, typename... Args>
    requires (meta::has_member_sort<Range, Args...>)
constexpr decltype(auto) sort(Range&& range, Args&&... args) noexcept(
    noexcept(std::forward<Range>(range).sort(std::forward<Args>(args)...))
) {
    return std::forward<Range>(range).sort(std::forward<Args>(args)...);
}


/* Iterator-based `sort()`, which always uses the fallback powersort implementation.
If the iterators do not support O(1) distance, the length of the range will be
computed in O(n) time before starting the sort algorithm. */
template <typename Begin, typename End, typename Less = std::less<>>
    requires (impl::iter_sortable<Less, Begin, End>)
constexpr void sort(Begin&& begin, End&& end, Less&& less_than = {}) noexcept(
    noexcept(impl::powersort{}(
        std::forward<Begin>(begin),
        std::forward<End>(end),
        std::forward<Less>(less_than)
    ))
) {
    impl::powersort{}(
        std::forward<Begin>(begin),
        std::forward<End>(end),
        std::forward<Less>(less_than)
    );
}


/* Produce a sorted version of a container with the specified less-than comparison
function.  If no explicit comparison function is given, it will default to a
transparent `<` operator for each element.  This operator must be explicitly enabled
for a given container type by specializing the `meta::detail::sorted<Less, T>` struct
with an appropriate `::type` alias that injects the comparison function into the
container configuration.

This overload directly copies or moves the contents of a previous, unsorted container,
and returns a new container of a corresponding type.  It returns the input as-is if the
container is already equivalent to its sorted type. */
template <meta::unqualified Less = std::less<>, typename T>
    requires (
        meta::is<
            T,
            typename meta::detail::sorted<Less, meta::unqualify<T>>::type
        > ||
        meta::constructible_from<
            typename meta::detail::sorted<Less, meta::unqualify<T>>::type,
            T
        >
    )
[[nodiscard]] decltype(auto) sorted(T&& container) noexcept(
    meta::is<typename meta::detail::sorted<Less, meta::unqualify<T>>::type, T> ||
    noexcept(typename meta::detail::sorted<Less, meta::unqualify<T>>::type(
        std::forward<T>(container)
    ))
) {
    using sorted_type = meta::detail::sorted<Less, meta::unqualify<T>>::type;
    if constexpr (meta::is<sorted_type, T>) {
        return std::forward<T>(container);
    } else {
        return sorted_type(std::forward<T>(container));
    }
}


/* Produce a sorted version of a container with the specified less-than comparison
function.  If no explicit comparison function is given, it will default to a
transparent `<` operator for each element.  This operator must be explicitly enabled
for a given container type by specializing the `meta::detail::sorted<Less, T>` struct
with an appropriate `::type` alias that injects the comparison function into the
container configuration.

This overload expects the user to provide the container type as an explicit template
parameter, possibly along with a custom comparison function.  All arguments will be
passed to the constructor for the container's sorted type. */
template <meta::unqualified T, meta::unqualified Less = std::less<>, typename... Args>
    requires (meta::constructible_from<
        typename meta::detail::sorted<Less, T>::type,
        Args...
    >)
[[nodiscard]] meta::detail::sorted<Less, T>::type sorted(Args&&... args) noexcept(
    noexcept(typename meta::detail::sorted<Less, T>::type(std::forward<Args>(args)...))
) {
    using sorted_type = meta::detail::sorted<Less, T>::type;
    return sorted_type(std::forward<Args>(args)...);
}


/* Produce a sorted version of a container with the specified less-than comparison
function.  If no explicit comparison function is given, it will default to a
transparent `<` operator for each element.  This operator must be explicitly enabled
for a given container type by specializing the `meta::detail::sorted<Less, T>` struct
with an appropriate `::type` alias that injects the comparison function into the
container configuration.

This overload expects the user to provide an unspecialized template class as the first
template parameter, possibly along with a custom comparison function.  The arguments
will be used to specialize the container type using CTAD, as if it were being
constructed with the given arguments. */
template <
    template <typename...> class Container,
    meta::unqualified Less = std::less<>,
    typename... Args
>
    requires (requires(Args... args) {
        Container(std::forward<Args>(args)...);
        typename meta::detail::sorted<
            Less,
            decltype(Container(std::forward<Args>(args)...))
        >::type;
        typename meta::detail::sorted<
            Less,
            decltype(Container(std::forward<Args>(args)...))
        >::type(std::forward<Args>(args)...);
    })
[[nodiscard]] auto sorted(Args&&... args) noexcept(
    noexcept(typename meta::detail::sorted<
        Less,
        decltype(Container(std::forward<Args>(args)...))
    >::type(std::forward<Args>(args)...))
) {
    using deduced_type = decltype(Container(std::forward<Args>(args)...));
    using sorted_type = meta::detail::sorted<Less, deduced_type>::type;
    return sorted_type(std::forward<Args>(args)...);
}


/* Produce a sorted version of a container with the specified less-than comparison
function.  If no explicit comparison function is given, it will default to a
transparent `<` operator for each element.  This operator must be explicitly enabled
for a given container type by specializing the `meta::detail::sorted<Less, T>` struct
with an appropriate `::type` alias that injects the comparison function into the
container configuration.

This overload expects the user to provide an unspecialized template class as the first
template parameter, possibly along with a custom comparison function.  The arguments
will be used to specialize the container type using CTAD, as if it were being
constructed with the given arguments. */
template <
    template <typename T, impl::capacity<T>, typename...> class Container,
    meta::unqualified Less = std::less<>,
    typename... Args
>
    requires (requires(Args... args) {
        Container(std::forward<Args>(args)...);
        typename meta::detail::sorted<
            Less,
            decltype(Container(std::forward<Args>(args)...))
        >::type;
        typename meta::detail::sorted<
            Less,
            decltype(Container(std::forward<Args>(args)...))
        >::type(std::forward<Args>(args)...);
    })
[[nodiscard]] auto sorted(Args&&... args) noexcept(
    noexcept(typename meta::detail::sorted<
        Less,
        decltype(Container(std::forward<Args>(args)...))
    >::type(std::forward<Args>(args)...))
) {
    using deduced_type = decltype(Container(std::forward<Args>(args)...));
    using sorted_type = meta::detail::sorted<Less, deduced_type>::type;
    return sorted_type(std::forward<Args>(args)...);
}


/* Produce a sorted version of a container with the specified less-than comparison
function.  If no explicit comparison function is given, it will default to a
transparent `<` operator for each element.  This operator must be explicitly enabled
for a given container type by specializing the `meta::detail::sorted<Less, T>` struct
with an appropriate `::type` alias that injects the comparison function into the
container configuration.

This overload expects the user to provide an unspecialized template class as the first
template parameter, possibly along with a custom comparison function.  The arguments
will be used to specialize the container type using CTAD, as if it were being
constructed with the given arguments. */
template <
    template <
        typename K,
        typename V,
        impl::capacity<std::pair<K, V>>,
        typename...
    > class Container,
    meta::unqualified Less = std::less<>,
    typename... Args
>
    requires (requires(Args... args) {
        Container(std::forward<Args>(args)...);
        typename meta::detail::sorted<
            Less,
            decltype(Container(std::forward<Args>(args)...))
        >::type;
        typename meta::detail::sorted<
            Less,
            decltype(Container(std::forward<Args>(args)...))
        >::type(std::forward<Args>(args)...);
    })
[[nodiscard]] auto sorted(Args&&... args) noexcept(
    noexcept(typename meta::detail::sorted<
        Less,
        decltype(Container(std::forward<Args>(args)...))
    >::type(std::forward<Args>(args)...))
) {
    using deduced_type = decltype(Container(std::forward<Args>(args)...));
    using sorted_type = meta::detail::sorted<Less, deduced_type>::type;
    return sorted_type(std::forward<Args>(args)...);
}


}


#endif
