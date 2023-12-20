#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_APPEND_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_APPEND_H

#include <type_traits>  // std::enable_if_t<>
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace linked {


    template <typename View, typename Item>
    inline auto append(View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::linked, void>
    {
        view.link(view.tail(), view.node(item), nullptr);
    }


    template <typename View, typename Item>
    inline auto append_left(View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::linked, void>
    {
        view.link(view.tail(), view.node(item), nullptr);
    }


    template <typename View, typename Key, typename Value>
    inline auto append(View& view, const Key& key, const Value& value)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        view.template node<View::Allocator::INSERT_TAIL>(key, value);
    }


    template <typename View, typename Key, typename Value>
    inline auto append_left(View& view, const Key& key, const Value& value)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        view.template node<View::Allocator::INSERT_HEAD>(key, value);
    }


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_APPEND_H
