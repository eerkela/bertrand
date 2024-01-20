#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_SETDEFAULT_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_SETDEFAULT_H

#include <type_traits>  // std::enable_if_t<>
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace linked {


    template <typename View, typename Key, typename Value>
    inline auto setdefault(
        View& view,
        const Key& key,
        Value& default_value
    ) -> std::enable_if_t<ViewTraits<View>::dictlike, Value&>
    {
        using Node = typename View::Node;
        static constexpr unsigned int flags = (
            View::Allocator::EXIST_OK | View::Allocator::INSERT_TAIL
        );
        Node* node = view.template node<flags>(key, default_value);
        return node->mapped();
    }


    template <typename View, typename Key, typename Value>
    inline auto setdefault_left(
        View& view,
        const Key& key,
        Value& default_value
    ) -> std::enable_if_t<ViewTraits<View>::dictlike, Value&>
    {
        using Node = typename View::Node;
        static constexpr unsigned int flags = (
            View::Allocator::EXIST_OK | View::Allocator::INSERT_HEAD
        );
        Node* node = view.template node<flags>(key, default_value);
        return node->mapped();
    }


    template <typename View, typename Key, typename Value>
    inline auto lru_setdefault(
        View& view,
        const Key& key,
        Value& default_value
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
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_SETDEFAULT_H
