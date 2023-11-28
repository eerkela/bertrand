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
    inline auto add(View& view, Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::INSERT_TAIL
        );
        view.template node<flags>(item);
    }


    /* Add an item to the beginning of a linked set or dictionary. */
    template <typename View, typename Item = typename View::Value>
    inline auto add_left(View& view, Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::INSERT_HEAD
        );
        view.template node<flags>(item);
    }


    /* Add an item to the front of a set or dictionary, evicting the last item if
    necessary and moving items that are already present. */
    template <typename View, typename Item = typename View::Value>
    inline auto lru_add(View& view, Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::INSERT_HEAD |
            Allocator::MOVE_HEAD | Allocator::EVICT_TAIL
        );
        view.template node<flags>(item);
    }


    /* Add a key-value pair to the end of a linked dictionary. */
    template <
        typename View,
        typename Key = typename View::Value,
        typename Value = typename View::MappedValue
    >
    inline auto add(View& view, Key& key, Value& value)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::INSERT_TAIL
        );
        view.template node<flags>(key, value);
    }


    /* Add a key-value pair to the end of a linked dictionary. */
    template <
        typename View,
        typename Key = typename View::Value,
        typename Value = typename View::MappedValue
    >
    inline auto add_left(View& view, Key& key, Value& value)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::INSERT_HEAD
        );
        view.template node<flags>(key, value);
    }


    /* Add a key-value pair to the front of a dictionary, evicting the last item if
    necessary and moving pairs that are already present. */
    template <
        typename View,
        typename Key = typename View::Value,
        typename Value = typename View::MappedValue
    >
    inline auto lru_add(View& view, Key& key, Value& value, bool left)
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
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_ADD_H
