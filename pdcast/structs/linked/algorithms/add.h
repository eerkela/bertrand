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
        -> std::enable_if_t<ViewTraits<View>::setlike, void>
    {
        using Node = typename View::Node;
        Node* node = view.node<true>(item);  // exist_ok = true
        if (node->next() == nullptr && node != view.tail()) {  // node not in view
            if (left) {
                view.link(nullptr, node, view.head());
            } else {
                view.link(view.tail(), node, nullptr);
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
        using Node = typename View::Node;
        Node* node = view.node<true>(key, value);  // 2-argument init, exist_ok = true
        if (node->next() == nullptr && node != view.tail()) {  // node not in view
            if (left) {
                view.link(nullptr, node, view.head());
            } else {
                view.link(view.tail(), node, nullptr);
            }
        }
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_ADD_H
