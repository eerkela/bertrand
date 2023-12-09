#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_COUNT_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_COUNT_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <type_traits>  // std::enable_if_t<>
#include "../../util/ops.h"  // eq()
#include "../core/view.h"  // ViewTraits
#include "slice.h"  // normalize_slice()


namespace bertrand {
namespace structs {
namespace linked {


    /* Count the number of occurrences of an item within a linked list. */
    template <typename View, typename Item>
    inline auto count(
        const View& view,
        const Item& item,
        std::optional<long long> start,
        std::optional<long long> stop
    ) -> std::enable_if_t<ViewTraits<View>::listlike, size_t>
    {
        return _listlike_count(view, item, start, stop);
    }


    /* Count the number of occurrences of an item within a linked set or dictionary. */
    template <typename View, typename Item>
    auto count(
        const View& view,
        const Item& item,
        std::optional<long long> start,
        std::optional<long long> stop
    ) -> std::enable_if_t<ViewTraits<View>::hashed, size_t>
    {
        using Node = typename View::Node;
        if (view.size() == 0) {
            return 0;
        }

        SliceIndices<View> indices = normalize_slice(view, start, stop);
        size_t norm_start = indices.start;
        size_t norm_stop = indices.stop;
        if (norm_start == norm_stop) {
            return 0;
        } else if (norm_start > norm_stop) {
            throw IndexError("start index cannot be greater than stop index");
        }
        

        Node* node = view.search(item);
        if (node == nullptr) {
            return 0;
        } else if (norm_start == 0 && norm_stop == view.size() - 1) {
            return 1;
        }

        size_t idx = 0;
        for (auto it = view.begin(); idx < norm_stop; ++it, ++idx) {
            if (it.curr() == node) break;
        }
        return idx >= norm_start && idx < norm_stop;
    }


    /* Count the number of occurrences of a key-value pair within a linked
    dictionary. */
    template <typename View, typename Key, typename Value>
    auto count(
        const View& view,
        const Key& key,
        const Value& value,
        std::optional<long long> start,
        std::optional<long long> stop
    ) -> std::enable_if_t<ViewTraits<View>::dictlike, size_t>
    {
        using Node = typename View::Node;
        if (view.size() == 0) {
            return 0;
        }

        SliceIndices<View> indices = normalize_slice(view, start, stop);
        size_t norm_start = indices.start;
        size_t norm_stop = indices.stop;
        if (norm_start == norm_stop) {
            return 0;
        } else if (norm_start > norm_stop) {
            throw IndexError("start index cannot be greater than stop index");
        } 

        Node* node = view.search(key);
        if (node == nullptr) {
            return 0;
        } else if (!eq(node->mapped(), value)) {
            std::ostringstream msg;
            msg << "value mismatch for key " << repr(key) << ": ";
            msg << repr(node->mapped()) << " != " << repr(value);
            throw KeyError(msg.str());
        }

        if (norm_start == 0 && norm_stop == view.size() - 1) {
            return 1;
        }
        size_t idx = 0;
        for (auto it = view.begin(); idx < norm_stop; ++it, ++idx) {
            if (it.curr() == node) {
                break;
            }
        }
        return idx >= norm_start && idx < norm_stop;
    }


    ////////////////////////
    ////    INTERNAL    ////
    ////////////////////////

    // NOTE: this used for both the listlike count() method as well as
    // LinkedDict.values().count().

    /* Count the number of occurrences of an item within a linked list. */
    template <typename View, typename Item>
    size_t _listlike_count(
        const View& view,
        const Item& item,
        std::optional<long long> start,
        std::optional<long long> stop
    ) {
        using Node = typename View::Node;
        if (view.size() == 0) {
            return 0;
        }

        SliceIndices<View> indices = normalize_slice(view, start, stop);
        size_t norm_start = indices.start;
        size_t norm_stop = indices.stop;
        if (norm_start == norm_stop) {
            return 0;
        } else if (norm_start > norm_stop) {
            throw IndexError("start index cannot be greater than stop index");
        }

        // NOTE: if list is doubly-linked and stop is closer to tail than start is to
        // head, then we iterate backward from the tail
        if constexpr (NodeTraits<Node>::has_prev) {
            if (indices.backward) {
                size_t idx = view.size() - 1;
                auto it = view.rbegin();
                for (; idx >= norm_stop; --idx, ++it);

                size_t count = 0;
                for (; idx >= norm_start; --idx, ++it) {
                    count += eq(*it, item);
                }
                return count;
            }
        }

        // otherwise, we have to iterate from the head
        size_t idx = 0;
        auto it = view.begin();
        for (; idx < norm_start; ++idx, ++it);

        size_t count = 0;
        for (; idx < norm_stop; ++idx, ++it) {
            count += eq(*it, item);
        }
        return count;
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_COUNT_H
