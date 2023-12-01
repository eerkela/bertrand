// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_GET_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_GET_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_GET_H

#include <type_traits>  // std::enable_if_t<>
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Get the value associated with a key if it is present in the dictionary,
    otherwise return the default value. */
    template <typename View, typename Key, typename Value>
    inline auto get(
        const View& view,
        const Key& key,
        std::optional<Value> default_value
    ) -> std::enable_if_t<ViewTraits<View>::dictlike, std::optional<Value>>
    {
        using Node = typename View::Node;
        Node* node = view.search(key);
        if (node == nullptr) return default_value;
        return std::make_optional(node->mapped());
    }


    /* Get the value associated with a key and move it to the front of the dictionary
    if it is present.  Otherwise, return the default value. */
    template <typename View, typename Key, typename Value>
    inline auto lru_get(
        View& view,
        const Key& key,
        std::optional<Value> default_value
    ) -> std::enable_if_t<ViewTraits<View>::dictlike, std::optional<Value>>
    {
        using Node = typename View::Node;
        Node* node = view.template search<View::Allocator::MOVE_HEAD>(key);
        if (node == nullptr) return default_value;
        return std::make_optional(node->mapped());
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_GET_H
