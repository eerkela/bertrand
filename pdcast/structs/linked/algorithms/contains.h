// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONTAINS_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONTAINS_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONTAINS_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/python.h"  // eq()
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Check if an item is contained within a linked list. */
    template <typename View, typename Item = typename View::Value>
    inline auto contains(View& view, Item& item)
        -> std::enable_if_t<ViewTraits<View>::listlike, bool>
    {
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            if (util::eq(it.curr()->value(), item)) return true;
        }
        return false;
    }


    /* Check if an item is contained within a linked set or dictionary. */
    template <typename View, typename Item = typename View::Value>
    inline auto contains(View& view, Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed, bool>
    {
        return view.search(item) != nullptr;
    }

    /* Check if an item is contained within a linked set or dictionary and move it to
    the front if so. */
    template <typename View, typename Item = typename View::Value>
    inline auto lru_contains(View& view, Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed, bool>
    {
        return view.lru_search(item) != nullptr;
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONTAINS_H
