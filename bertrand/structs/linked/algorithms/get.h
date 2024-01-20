#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_GET_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_GET_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/ops.h"  // repr()
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace linked {


    template <typename View, typename Key>
    inline auto get(View& view, const Key& key)
        -> std::enable_if_t<ViewTraits<View>::dictlike, typename View::MappedValue&>
    {
        using Node = typename View::Node;
        Node* node = view.search(key);
        if (node == nullptr) {
            throw KeyError(repr(key));
        }
        return node->mapped();
    }


    template <typename View, typename Key, typename Value>
    inline auto get(View& view, const Key& key, Value& default_value)
        -> std::enable_if_t<ViewTraits<View>::dictlike, Value&>
    {
        using Node = typename View::Node;
        Node* node = view.search(key);
        if (node == nullptr) {
            return default_value;
        }
        return node->mapped();
    }


    template <typename View, typename Key>
    inline auto lru_get(View& view, const Key& key)
        -> std::enable_if_t<ViewTraits<View>::dictlike, typename View::MappedValue&>
    {
        using Node = typename View::Node;
        Node* node = view.template search<View::Allocator::MOVE_HEAD>(key);
        if (node == nullptr) {
            throw KeyError(repr(key));
        }
        return node->mapped();
    }


    template <typename View, typename Key, typename Value>
    inline auto lru_get(View& view, const Key& key, Value& default_value)
        -> std::enable_if_t<ViewTraits<View>::dictlike, Value&>
    {
        using Node = typename View::Node;
        Node* node = view.template search<View::Allocator::MOVE_HEAD>(key);
        if (node == nullptr) {
            return default_value;
        }
        return node->mapped();
    }


}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_GET_H
