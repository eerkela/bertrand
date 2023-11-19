// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_APPEND_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_APPEND_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_APPEND_H

#include <type_traits>  // std::enable_if_t<>
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Add an item to the end of a linked list, set, or dictionary. */
    template <typename View, typename Item = typename View::Value>
    inline auto append(View& view, Item& item, bool left)
        -> std::enable_if_t<ViewTraits<View>::linked, void>
    {
        using Node = typename View::Node;
        Node* node = view.node(item);
        if (left) {
            view.link(nullptr, node, view.head());
        } else {
            view.link(view.tail(), node, nullptr);
        }
    }


    /* Add a key-value pair to the end of a linked dictionary. */
    template <
        typename View,
        typename Key = typename View::Value,
        typename Value = typename View::MappedValue
    >
    inline auto append(View& view, Key& key, Value& value, bool left)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        using Node = typename View::Node;
        Node* node = view.node(key, value);  // use 2-argument init
        if (left) {
            view.link(nullptr, node, view.head);
        } else {
            view.link(view.tail, node, nullptr);
        }
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_APPEND_H
