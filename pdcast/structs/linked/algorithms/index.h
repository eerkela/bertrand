// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_INDEX_H
#define BERTRAND_STRUCTS_ALGORITHMS_INDEX_H

#include <cstddef>  // size_t
#include <sstream>  // std::ostringstream
#include <stdexcept>  // std::invalid_argument
#include <type_traits>  // std::enable_if_t<>
#include "position.h"  // normalize_index()
#include "../../util/repr.h"  // repr()
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Get the index of an item within a linked list. */
    template <
        typename View,
        typename Index,
        typename Item = typename View::Value
    >
    auto index(const View& view, Item& item, Index start, Index stop)
        -> std::enable_if_t<ViewTraits<View>::listlike, size_t>
    {
        using Node = typename View::Node;

        // convenience function for throwing not-found error
        auto not_found = [](Item& item) {
            std::ostringstream msg;
            msg << util::repr(item) << " is not in list";
            return std::invalid_argument(msg.str());
        };

        // trivial case: empty list
        if (view.size() == 0) throw not_found(item);

        // normalize start/stop indices
        size_t norm_start = normalize_index(start, view.size(), true);
        size_t norm_stop = normalize_index(stop, view.size(), true);
        if (norm_start > norm_stop) {
            throw std::invalid_argument(
                "start index cannot be greater than stop index"
            );
        }

        // if list is doubly-linked and stop is closer to tail than start is to head,
        // then we iterate backward from the tail
        if constexpr (NodeTraits<Node>::has_prev) {
            if ((view.size() - 1 - norm_stop) < norm_start) {
                // get backwards iterator to stop index
                auto it = view.rbegin();
                size_t idx = view.size() - 1;
                while (idx >= norm_stop) ++it, --idx;

                // search until we hit start index
                bool found = false;
                size_t last_observed;
                while (idx >= norm_start) {
                    const Node* node = it.curr();
                    if (node->eq(item)) {
                        found = true;
                        last_observed = idx;
                    }
                    ++it;
                    --idx;
                }
                if (found) {
                    return last_observed;
                }

                // item not found
                throw not_found(item);
            }
        }

        // otherwise, we iterate forward from the head
        auto it = view.begin();
        size_t idx = 0;
        while (idx < norm_start) {
            ++it;
            ++idx;
        }

        // search until we hit item or stop index
        while (idx < norm_stop) {
            const Node* node = it.curr();
            if (node->eq(item)) {
                return idx;
            }
            ++it;
            ++idx;
        }

        // item not found
        throw not_found(item);
    }


    /* Get the index of an item within a linked set or dictionary. */
    template <
        typename View,
        typename Index,
        typename Item = typename View::Value
    >
    auto index(View& view, Item& item, Index start, Index stop)
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
        size_t norm_start = normalize_index(start, view.size(), true);
        size_t norm_stop = normalize_index(stop, view.size(), true);
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
            if ((view.size() - 1 - norm_stop) < norm_start) {
                // get backwards iterator to stop index
                auto it = view.rbegin();
                size_t idx = view.size() - 1;
                while (idx >= norm_stop) {
                    if (it.curr() == node) throw not_found(item);  // comes after stop
                    ++it;
                    --idx;
                }

                // search until we hit start index
                while (idx >= norm_start) {
                    if (it.curr() == node) return idx;
                    ++it;
                    --idx;
                }

                throw not_found(item);  // item comes before start
            }
        }

        // otherwise, we iterate forward from the head
        auto it = view.begin();
        size_t idx = 0;
        while (idx < norm_start) {
            if (it.curr() == node) throw not_found(item);  // comes before start
            ++it;
            ++idx;
        }

        // search until we hit item or stop index
        while (idx < norm_stop) {
            if (it.curr() == node) return idx;
            ++it;
            ++idx;
        }

        throw not_found(item);  // item comes after stop
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_INDEX_H include guard
