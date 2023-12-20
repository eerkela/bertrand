#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_ADD_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_ADD_H

#include <type_traits>  // std::enable_if_t<>
#include "../core/view.h"  // views


namespace bertrand {
namespace linked {


    template <typename View, typename Item>
    inline auto add(View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::INSERT_TAIL
        );
        view.template node<flags>(item);
    }


    template <typename View, typename Item>
    inline auto add_left(View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::INSERT_HEAD
        );
        view.template node<flags>(item);
    }


    template <typename View, typename Item>
    inline auto lru_add(View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::INSERT_HEAD |
            Allocator::MOVE_HEAD | Allocator::EVICT_TAIL
        );
        view.template node<flags>(item);
    }


    template <typename View, typename Key, typename Value>
    inline auto add(View& view, const Key& key, const Value& value)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::INSERT_TAIL
        );
        view.template node<flags>(key, value);
    }


    template <typename View, typename Key, typename Value>
    inline auto add_left(View& view, const Key& key, const Value& value)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::INSERT_HEAD
        );
        view.template node<flags>(key, value);
    }


    template <typename View, typename Key, typename Value>
    inline auto lru_add(View& view, const Key& key, const Value& value)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::MOVE_HEAD |
            Allocator::EVICT_TAIL | Allocator::INSERT_HEAD
        );
        view.template node<flags>(key, value);
    }


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_ADD_H
