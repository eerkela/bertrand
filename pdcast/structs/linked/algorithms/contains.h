#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONTAINS_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONTAINS_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/ops.h"  // eq()
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace linked {


    /* Check if an item is contained within a linked list. */
    template <Yield yield = Yield::KEY, typename View, typename Item>
    inline auto contains(const View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::listlike || yield == Yield::VALUE, bool>
    {
        auto it = view.template begin<yield>();
        auto end = view.template end<yield>();
        for (; it != end; ++it) {
            if (eq(*it, item)) {
                return true;
            }
        }
        return false;
    }


    /* Check if an item is contained within a linked set or dictionary. */
    template <Yield yield = Yield::KEY, typename View, typename Item>
    inline auto contains(const View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed && yield != Yield::VALUE, bool>
    {
        if constexpr (yield == Yield::KEY) {
            return view.search(item) != nullptr;
        } else {
            using Node = typename View::Node;
            const Node* node = view.search(item.first);
            return node != nullptr && eq(node->mapped(), item.second);
        }
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
