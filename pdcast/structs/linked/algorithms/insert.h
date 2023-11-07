// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_INSERT_H
#define BERTRAND_STRUCTS_ALGORITHMS_INSERT_H

#include <cstddef>  // size_t
#include <type_traits>  // std::enable_if_t<>
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/view.h"  // ViewTraits
#include "position.h"  // position()


namespace bertrand {
namespace structs {
namespace linked {


    /* Insert an item into a linked list, set, or dictionary at the given index. */
    template <
        typename View,
        typename Index,
        typename Item = typename View::Value
    >
    inline auto insert(View& view, Index index, Item& item)
        -> std::enable_if_t<ViewTraits<View>::listlike, void>
    {
        // normalize index
        size_t norm_index = normalize_index(index, view.size(), true);

        // reserve space for new node and freeze allocator
        typename View::MemGuard guard = view.reserve(view.size() + 1);

        // get iterator to index
        if constexpr (NodeTraits<typename View::Node>::has_prev) {
            if (view.closer_to_tail(norm_index)) {
                auto it = view.rbegin();
                for (size_t i = view.size() - 1; i > norm_index; --i) ++it;
                it.insert(view.node(item));
                return;
            }
        }

        // forward traversal
        auto it = view.begin();
        for (size_t i = 0; i < norm_index; ++i) ++it;
        it.insert(view.node(item));
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_INSERT_H include guard
