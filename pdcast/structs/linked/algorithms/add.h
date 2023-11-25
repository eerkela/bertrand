// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_ADD_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_ADD_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_ADD_H

#include <type_traits>  // std::enable_if_t<>
#include "../core/view.h"  // views


namespace bertrand {
namespace structs {
namespace linked {


    /* Add an item to the end of a linked set or dictionary. */
    template <typename View, typename Item = typename View::Value>
    inline auto add(View& view, Item& item, bool left)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        typename View::Node* node = view.template node<true>(item);
        if (node->next() == nullptr && node != view.tail()) {
            if (left) {
                view.link(nullptr, node, view.head());
            } else {
                view.link(view.tail(), node, nullptr);
            }
        }
    }


    /* Add an item to the front of a set or dictionary, or move it there if it is
    already present. */
    template <typename View, typename Item = typename View::Value>
    inline auto lru_add(View& view, Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Node = typename View::Node;

        // exist_ok=true: get existing node if present or create node if not
        // evict=true: if view is of fixed size, evict the tail node if necessary
        Node* node = view.template node<true, true>(item);

        // append to head if not already present
        if (node->next() == nullptr && node != view.tail()) {
            view.link(nullptr, node, view.head());

        // otherwise, move to head
        } else if (node != view.head()) {
            // if doubly-linked, moving to the front is O(1)
            if constexpr (NodeTraits<Node>::has_prev) {
                view.unlink(node->prev(), node, node->next());
                view.link(nullptr, node, view.head());

            // otherwise, we have to traverse the list to find the previous node
            } else {
                auto it = view.begin();
                ++it;  // skip head
                for (auto end = view.end(); it != end; ++it) {
                    if (it.curr() == node) {
                        view.unlink(it.prev(), node, it.next());
                        view.link(nullptr, node, view.head());
                        break;
                    }
                }
            }
        }
    }


    /* Add a key-value pair to the end of a linked dictionary. */
    template <
        typename View,
        typename Key = typename View::Value,
        typename Value = typename View::MappedValue
    >
    inline auto add(View& view, Key& key, Value& value, bool left)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        typename View::Node* node = view.template node<true>(key, value);
        if (node->next() == nullptr && node != view.tail()) {
            if (left) {
                view.link(nullptr, node, view.head());
            } else {
                view.link(view.tail(), node, nullptr);
            }
        }
    }


    /* Add an item to the front of a set or dictionary, or move it there if it is
    already present. */
    template <
        typename View,
        typename Key = typename View::Value,
        typename Value = typename View::MappedValue
    >
    inline auto lru_add(View& view, Key& key, Value& value, bool left)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        using Node = typename View::Node;

        // exist_ok=true: get existing node if present or create node if not
        // evict=true: if view is of fixed size, evict the tail node if necessary
        Node* node = view.template node<true, true>(key, value);

        // append to head if not already present
        if (node->next() == nullptr && node != view.tail()) {
            view.link(nullptr, node, view.head());

        // otherwise, move to head
        } else if (node != view.head()) {
            // if doubly-linked, moving to the front is O(1)
            if constexpr (NodeTraits<Node>::has_prev) {
                view.unlink(node->prev(), node, node->next());
                view.link(nullptr, node, view.head());

            // otherwise, we have to traverse the list to find the previous node
            } else {
                auto it = view.begin();
                ++it;  // skip head
                for (auto end = view.end(); it != end; ++it) {
                    if (it.curr() == node) {
                        view.unlink(it.prev(), node, it.next());
                        view.link(nullptr, node, view.head());
                        break;
                    }
                }
            }
        }
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_ADD_H
