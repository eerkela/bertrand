#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONTAINS_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONTAINS_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/base.h"  // is_pairlike<>
#include "../../util/ops.h"  // eq()
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace linked {


    template <Yield yield = Yield::KEY, typename View, typename Item>
    auto contains(const View& view, const Item& item)
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


    template <Yield yield = Yield::KEY, typename View, typename Item>
    inline auto contains(const View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed && yield != Yield::VALUE, bool>
    {
        if constexpr (yield == Yield::KEY) {
            return view.search(item) != nullptr;
        } else {
            static_assert(
                is_pairlike<Item>,
                "item must be pair-like (e.g. std::pair or std::tuple of size 2)"
            );
            const typename View::Node* node = view.search(std::get<0>(item));
            return node != nullptr && eq(node->mapped(), std::get<1>(item));
        }
    }


    template <typename View, typename Item>
    inline auto lru_contains(View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed, bool>
    {
        return view.template search<View::Allocator::MOVE_HEAD>(item) != nullptr;
    }


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONTAINS_H
