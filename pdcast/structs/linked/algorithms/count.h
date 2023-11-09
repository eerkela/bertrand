// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_COUNT_H
#define BERTRAND_STRUCTS_ALGORITHMS_COUNT_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <type_traits>  // std::enable_if_t<>
#include "../../util/python.h"  // eq()
#include "../core/view.h"  // ViewTraits
#include "slice.h"  // normalize_slice()


namespace bertrand {
namespace structs {
namespace linked {


    /* Count the number of occurrences of an item within a linked list. */
    template <typename View, typename Item = typename View::Value>
    auto count(
        const View& view,
        const Item& item,
        std::optional<long long> start,
        std::optional<long long> stop
    )
        -> std::enable_if_t<ViewTraits<View>::listlike, size_t>
    {
        using Node = typename View::Node;

        // trivial case: empty list
        if (view.size() == 0) return 0;

        // normalize start/stop indices
        SliceIndices<View> indices = normalize_slice(view, start, stop);
        size_t norm_start = indices.start;
        size_t norm_stop = indices.stop;
        if (norm_start > norm_stop) {
            throw std::invalid_argument(
                "start index cannot be greater than stop index"
            );
        }

        // trivial case: empty slice
        if (norm_start == norm_stop) return 0;

        // if list is doubly-linked and stop is closer to tail than start is to head,
        // then we iterate backward from the tail
        if constexpr (NodeTraits<Node>::has_prev) {
            if (indices.backward) {
                // get backwards iterator to stop index
                size_t idx = view.size() - 1;
                auto it = view.rbegin();
                for (; idx >= norm_stop; --idx, ++it);

                // search until we hit start index
                size_t count = 0;
                for (; idx >= norm_start; --idx, ++it) {
                    count += util::eq(*it, item);  // branchless
                }
                return count;
            }
        }

        // otherwise, we iterate forward from the head
        size_t idx = 0;
        auto it = view.begin();
        for (; idx < norm_start; ++idx, ++it);

        // search until we hit item or stop index
        size_t count = 0;
        for (; idx < norm_stop; ++idx, ++it) {
            count += util::eq(*it, item);  // branchless
        }
        return count;
    }


    /* Count the number of occurrences of an item within a linked set or dictionary. */
    template <typename View, typename Item = typename View::Value>
    auto count(
        const View& view,
        const Item& item,
        std::optional<long long> start,
        std::optional<long long> stop
    )
        -> std::enable_if_t<ViewTraits<View>::setlike, size_t>
    {
        using Node = typename View::Node;

        // trivial case: empty set
        if (view.size() == 0) return 0;

        // normalize start/stop indices
        SliceIndices<View> indices = normalize_slice(view, start, stop);
        size_t norm_start = indices.start;
        size_t norm_stop = indices.stop;
        if (norm_start > norm_stop) {
            throw std::invalid_argument(
                "start index cannot be greater than stop index"
            );
        }

        // trivial case: empty slice
        if (norm_start == norm_stop) return 0;

        // check if item is contained in hash table
        Node* node = view.search(item);
        if (node == nullptr) return 0;

        // trivial case: slice covers whole set
        if (norm_start == 0 && norm_stop == view.size() - 1) return 1;

        // find index of item
        size_t idx = 0;
        for (auto it = view.begin(); idx < norm_stop; ++it, ++idx) {
            if (it.curr() == node) break;
        }
        return idx >= norm_start && idx < norm_stop;
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_COUNT_H include guard
