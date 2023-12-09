#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_GET_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_GET_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/ops.h"  // repr()
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace linked {


    /* Get the value associated with a key if it is present in the dictionary, or throw
    an error if it is not found. */
    template <typename View, typename Key, typename Value = typename View::MappedValue>
    inline auto get(View& view, const Key& key)
        -> std::enable_if_t<ViewTraits<View>::dictlike, Value&>
    {
        using Node = typename View::Node;
        Node* node = view.search(key);
        if (node == nullptr) {
            throw KeyError(repr(key));
        }
        return node->mapped();
    }


    /* Get the value associated with a key if it is present in the dictionary, or
    return the default value. */
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


    /* Get the value associated with a key and move it to the front of the dictionary
    if it is present.  Otherwise, throw an error. */
    template <typename View, typename Key, typename Value = typename View::MappedValue>
    inline auto lru_get(View& view, const Key& key)
        -> std::enable_if_t<ViewTraits<View>::dictlike, Value&>
    {
        using Node = typename View::Node;
        Node* node = view.template search<View::Allocator::MOVE_HEAD>(key);
        if (node == nullptr) {
            throw KeyError(repr(key));
        }
        return node->mapped();
    }


    /* Get the value associated with a key and move it to the front of the dictionary
    if it is present.  Otherwise, return the default value. */
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
