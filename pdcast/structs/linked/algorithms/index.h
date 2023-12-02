#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_INDEX_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_INDEX_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <type_traits>  // std::enable_if_t<>
#include "../../util/ops.h"  // eq(), repr()
#include "../core/view.h"  // ViewTraits
#include "slice.h"  // normalize_slice()


namespace bertrand {
namespace structs {
namespace linked {


    /* Get the index of an item within a linked list. */
    template <typename View, typename Item>
    inline auto index(
        const View& view,
        const Item& item,
        std::optional<long long> start,
        std::optional<long long> stop
    ) -> std::enable_if_t<ViewTraits<View>::listlike, size_t>
    {
        return _listlike_index(view, item, start, stop);
    }


    /* Get the index of a key within a linked set or dictionary. */
    template <typename View, typename Item>
    auto index(
        const View& view,
        const Item& item,
        std::optional<long long> start,
        std::optional<long long> stop
    ) -> std::enable_if_t<ViewTraits<View>::hashed, size_t>
    {
        using Node = typename View::Node;

        // helper for throwing not-found error
        auto not_found = [](const Item& item) {
            std::ostringstream msg;
            msg << repr(item) << " is not in the set";
            return KeyError(msg.str());
        };

        if (view.size() == 0) {
            throw not_found(item);
        }

        // normalize start/stop indices
        SliceIndices<View> indices = normalize_slice(view, start, stop);
        size_t norm_start = indices.start;
        size_t norm_stop = indices.stop;
        if (norm_start == norm_stop) {
            throw not_found(item);
        }else if (norm_start > norm_stop) {
            throw IndexError("start index cannot be greater than stop index");
        }

        // search for item
        Node* node = view.search(item);
        if (node == nullptr) {
            throw not_found(item);
        }

        // NOTE: if list is doubly-linked and stop is closer to tail than start is to
        // head, then we iterate backward from the tail
        if constexpr (NodeTraits<Node>::has_prev) {
            if (indices.backward) {
                size_t idx = view.size() - 1;
                auto it = view.rbegin();
                for (; idx > norm_start; --idx, ++it);
                for (; idx >= norm_stop; --idx, ++it) {
                    if (it.curr() == node) {
                        return idx;
                    }
                }
                throw not_found(item);  // item comes before start
            }
        }

        // otherwise, we have to iterate forward from the head
        size_t idx = 0;
        auto it = view.begin();
        for (; idx < norm_start; ++idx, ++it) {
            if (it.curr() == node) {
                throw not_found(item);  // item comes before start
            }
        }
        for (; idx < norm_stop; ++idx, ++it) {
            if (it.curr() == node) {
                return idx;
            }
        }
        throw not_found(item);  // item comes after stop
    }


    /* Get the index of a key-value pair within a linked dictionary. */
    template <typename View, typename Key, typename Value>
    auto index(
        const View& view,
        const Key& key,
        const Value& value,
        std::optional<long long> start,
        std::optional<long long> stop
    ) -> std::enable_if_t<ViewTraits<View>::dictlike, size_t>
    {
        using Node = typename View::Node;

        // convenience function for throwing not-found error
        auto not_found = [](const Key& key) {
            std::ostringstream msg;
            msg << repr(key) << " is not in the set";
            return KeyError(msg.str());
        };

        if (view.size() == 0) {
            throw not_found(key);
        }

        // normalize start/stop indices
        SliceIndices<View> indices = normalize_slice(view, start, stop);
        size_t norm_start = indices.start;
        size_t norm_stop = indices.stop;
        if (norm_start == norm_stop) {
            throw not_found(key);
        } else if (norm_start > norm_stop) {
            throw IndexError("start index cannot be greater than stop index");
        }

        // search for key
        Node* node = view.search(key);
        if (node == nullptr) {
            throw not_found(key);
        } else if (!eq(node->mapped(), value)) {
            std::ostringstream msg;
            msg << "value mismatch for key " << repr(key) << ": ";
            msg << repr(node->mapped()) << " != " << repr(value);
            throw KeyError(msg.str());
        }

        // NOTE: if list is doubly-linked and stop is closer to tail than start is to
        // head, then we iterate backward from the tail
        if constexpr (NodeTraits<Node>::has_prev) {
            if (indices.backward) {
                size_t idx = view.size() - 1;
                auto it = view.rbegin();
                for (; idx > norm_start; --idx, ++it);
                for (; idx >= norm_stop; --idx, ++it) {
                    if (it.curr() == node) return idx;
                }
                throw not_found(key);  // key comes before start
            }
        }

        // otherwise, we iterate forward from the head
        size_t idx = 0;
        auto it = view.begin();
        for (; idx < norm_start; ++idx, ++it) {
            if (it.curr() == node) {
                throw not_found(key);  // comes before start
            }
        }
        for (; idx < norm_stop; ++idx, ++it) {
            if (it.curr() == node) {
                return idx;
            }
        }
        throw not_found(key);  // key comes after stop
    }


    ////////////////////////
    ////    INTERNAL    ////
    ////////////////////////

    // NOTE: this used for both the listlike index() method as well as
    // LinkedDict.values().index().

    /* Get the index of an item within a linked list. */
    template <typename View, typename Item>
    size_t _listlike_index(
        const View& view,
        const Item& item,
        std::optional<long long> start,
        std::optional<long long> stop
    ) {
        using Node = typename View::Node;

        // convenience function for throwing not-found error
        auto not_found = [](const Item& item) {
            std::ostringstream msg;
            msg << repr(item) << " is not in the list";
            return KeyError(msg.str());
        };

        if (view.size() == 0) {
            throw not_found(item);
        }

        // normalize start/stop indices
        SliceIndices<View> indices = normalize_slice(view, start, stop);
        size_t norm_start = indices.start;
        size_t norm_stop = indices.stop;
        if (norm_start == norm_stop) {
            throw not_found(item);
        } else if (norm_start > norm_stop) {
            throw IndexError("start index cannot be greater than stop index");
        }

        // NOTE: if list is doubly-linked and stop is closer to tail than start is to
        // head, then we iterate backward from the tail
        if constexpr (NodeTraits<Node>::has_prev) {
            if (indices.backward) {
                size_t idx = view.size() - 1;
                auto it = view.rbegin();
                for (; idx > norm_stop; --idx, ++it);

                // keep track of last matching index
                bool found = false;
                size_t last_observed;
                for (; idx >= norm_start; --idx, ++it) {
                    if (eq(*it, item)) {
                        found = true;
                        last_observed = idx;
                    }
                }
                if (found) {
                    return last_observed;
                }
                throw not_found(item);
            }
        }

        // otherwise, we have to iterate forward from head
        size_t idx = 0;
        auto it = view.begin();
        for (; idx < norm_start; ++idx, ++it);
        for (; idx < norm_stop; ++idx, ++it) {
            if (eq(*it, item)) {
                return idx;
            }
        }
        throw not_found(item);
    }



}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_INDEX_H
