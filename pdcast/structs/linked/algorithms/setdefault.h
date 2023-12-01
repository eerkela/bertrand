// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_SETDEFAULT_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_SETDEFAULT_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_SETDEFAULT_H

#include <type_traits>  // std::enable_if_t<>
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Get the value associated with a key if it is present in the dictionary,
    otherwise insert it at the end of the dictionary. */
    template <typename View, typename Key, typename Value>
    inline auto setdefault(
        View& view,
        const Key& key,
        const Value& default_value
    ) -> std::enable_if_t<ViewTraits<View>::dictlike, Value&>
    {
        using Node = typename View::Node;
        static constexpr unsigned int flags = (
            View::Allocator::EXIST_OK | View::Allocator::INSERT_TAIL
        );
        Node* node = view.template node<flags>(key, default_value);
        return node->mapped();
    }


    /* Get the value associated with a key if it is present in the dictionary,
    otherwise insert it at the front of the dictionary. */
    template <typename View, typename Key, typename Value>
    inline auto setdefault_left(
        View& view,
        const Key& key,
        const Value& default_value
    ) -> std::enable_if_t<ViewTraits<View>::dictlike, Value&>
    {
        using Node = typename View::Node;
        static constexpr unsigned int flags = (
            View::Allocator::EXIST_OK | View::Allocator::INSERT_HEAD
        );
        Node* node = view.template node<flags>(key, default_value);
        return node->mapped();
    }


    /* Get the value associated with a key if it is present in the dictionary,
    moving it to the front of the dictionary if so.  Otherwise, insert it at
    the front of the dictionary and evict the tail node to make. */
    template <typename View, typename Key, typename Value>
    inline auto lru_setdefault(
        View& view,
        const Key& key,
        const Value& default_value
    ) -> std::enable_if_t<ViewTraits<View>::dictlike, Value&>
    {
        using Node = typename View::Node;
        static constexpr unsigned int flags = (
            View::Allocator::EXIST_OK | View::Allocator::MOVE_HEAD |
            View::Allocator::INSERT_HEAD | View::Allocator::EVICT_TAIL
        );
        Node* node = view.template node<flags>(key, default_value);
        return node->mapped();
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_SETDEFAULT_H
