#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_APPEND_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_APPEND_H

#include <type_traits>  // std::enable_if_t<>
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Add an item to the end of a linked list, set, or dictionary. */
    template <typename View, typename Item>
    inline auto append(View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::linked, void>
    {
        view.link(view.tail(), view.node(item), nullptr);
    }


    /* Add an item at the start of a linked list, set, or dictionary. */
    template <typename View, typename Item>
    inline auto append_left(View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::linked, void>
    {
        view.link(view.tail(), view.node(item), nullptr);
    }


    /* Add a key-value pair to the end of a linked dictionary. */
    template <typename View, typename Key, typename Value>
    inline auto append(View& view, const Key& key, const Value& value)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        view.template node<View::Allocator::INSERT_TAIL>(key, value);
    }


    /* Add a key-value pair at the start of a linked dictionary. */
    template <typename View, typename Key, typename Value>
    inline auto append_left(View& view, const Key& key, const Value& value)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        view.template node<View::Allocator::INSERT_HEAD>(key, value);
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_APPEND_H
