#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONTAINS_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONTAINS_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/ops.h"  // eq()
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace linked {


    /* Check if an item is contained within a linked list. */
    template <typename View, typename Item>
    inline auto contains(const View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::listlike, bool>
    {
        for (const auto& val : view) {
            if (eq(val, item)) return true;
        }
        return false;
    }


    /* Check if an item is contained within a linked set or dictionary. */
    template <typename View, typename Item>
    inline auto contains(const View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed, bool>
    {
        return view.search(item) != nullptr;
    }

    /* Check if an item is contained within a linked set or dictionary and move it to
    the front if so. */
    template <typename View, typename Item>
    inline auto lru_contains(View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed, bool>
    {
        return view.template search<View::Allocator::MOVE_HEAD>(item) != nullptr;
    }


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONTAINS_H
