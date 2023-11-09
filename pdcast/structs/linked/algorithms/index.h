// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_INDEX_H
#define BERTRAND_STRUCTS_ALGORITHMS_INDEX_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <stdexcept>  // std::invalid_argument
#include <type_traits>  // std::enable_if_t<>
#include "../../util/python.h"  // eq()
#include "../../util/repr.h"  // repr()
#include "../core/view.h"  // ViewTraits
#include "slice.h"  // normalize_slice()


namespace bertrand {
namespace structs {
namespace linked {


    /* Get the index of an item within a linked list. */
    template <typename View, typename Item = typename View::Value>
    auto index(
        const View& view,
        const Item& item,
        std::optional<long long> start,
        std::optional<long long> stop
    )
        -> std::enable_if_t<ViewTraits<View>::listlike, size_t>
    {
        using Node = typename View::Node;

        // convenience function for throwing not-found error
        auto not_found = [](const Item& item) {
            std::ostringstream msg;
            msg << util::repr(item) << " is not in list";
            return std::invalid_argument(msg.str());
        };

        // trivial case: empty list
        if (view.size() == 0) throw not_found(item);

        // normalize start/stop indices
        SliceIndices<View> indices = normalize_slice(view, start, stop);
        size_t norm_start = indices.start;
        size_t norm_stop = indices.stop;
        if (norm_start > norm_stop) {
            throw std::invalid_argument(
                "start index cannot be greater than stop index"
            );
        }

        // if list is doubly-linked and stop is closer to tail than start is to head,
        // then we iterate backward from the tail
        if constexpr (NodeTraits<Node>::has_prev) {
            if (indices.backward) {
                // get backwards iterator to stop index
                size_t idx = view.size() - 1;
                auto it = view.rbegin();
                for (; idx > norm_stop; --idx, ++it);

                // search until we hit start index
                bool found = false;
                size_t last_observed;
                for (; idx >= norm_start; --idx, ++it) {
                    if (util::eq(*it, item)) {
                        found = true;
                        last_observed = idx;
                    }
                }
                if (found) {
                    return last_observed;
                }

                // item not found
                throw not_found(item);
            }
        }

        // otherwise, get forwards iterator to start index
        size_t idx = 0;
        auto it = view.begin();
        for (; idx < norm_start; ++idx, ++it);

        // search until we hit item or stop index
        for (; idx < norm_stop; ++idx, ++it) {
            if (util::eq(*it, item)) {
                return idx;
            }
        }

        // item not found
        throw not_found(item);
    }


    /* Get the index of an item within a linked set or dictionary. */
    template <typename View, typename Item = typename View::Value>
    auto index(
        const View& view,
        const Item& item,
        std::optional<long long> start,
        std::optional<long long> stop
    )
        -> std::enable_if_t<ViewTraits<View>::setlike, size_t>
    {
        using Node = typename View::Node;

        // convenience function for throwing not-found error
        auto not_found = [](Item& item) {
            std::ostringstream msg;
            msg << util::repr(item) << " is not in set";
            return std::invalid_argument(msg.str());
        };

        // trivial case: empty set
        if (view.size() == 0) throw not_found(item);

        // normalize start/stop indices
        SliceIndices<View> indices = normalize_slice(view, start, stop);
        size_t norm_start = indices.start;
        size_t norm_stop = indices.stop;
        if (norm_start > norm_stop) {
            throw std::invalid_argument(
                "start index cannot be greater than stop index"
            );
        }

        // search for item in hash table
        Node* node = view.search(item);
        if (node == nullptr) throw not_found(item);

        // if list is doubly-linked and stop is closer to tail than start is to head,
        // then we iterate backward from the tail
        if constexpr (NodeTraits<Node>::has_prev) {
            if (indices.backward) {
                // get backwards iterator to stop index
                size_t idx = view.size() - 1;
                auto it = view.rbegin();
                for (; idx > norm_start; --idx) ++it;

                // search until we hit start index
                for (; idx >= norm_stop; --idx, ++it) {
                    if (it.curr() == node) return idx;
                }
                throw not_found(item);  // item comes before start
            }
        }

        // otherwise, we iterate forward from the head
        size_t idx = 0;
        auto it = view.begin();
        for (; idx < norm_start; ++idx, ++it) {
            if (it.curr() == node) throw not_found(item);  // comes before start
        }

        // search until we hit item or stop index
        for (; idx < norm_stop; ++idx, ++it) {
            if (it.curr() == node) return idx;
        }
        throw not_found(item);  // item comes after stop
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_INDEX_H include guard
