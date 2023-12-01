// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_INSERT_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_INSERT_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_INSERT_H

#include <cstddef>  // size_t
#include <type_traits>  // std::enable_if_t<>
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/view.h"  // ViewTraits
#include "position.h"  // position()


namespace bertrand {
namespace structs {
namespace linked {

    // TODO:
    // >>> l
    // LinkedList(["a", "b", "c", "d", "e", "f"])
    // >>> l.insert(6, "g")
    // >>> l
    // LinkedList(["a", "b", "c", "d", "e", "g", "f"])

    // >>> p
    // ["a", "b", "c", "d", "e", "f"]
    // >>> p.insert(6, "g")
    // >>> p
    // ["a", "b", "c", "d", "e", "f", "g"]


    /* Insert an item into a linked list, set, or dictionary at the given index. */
    template <typename View, typename Item>
    inline auto insert(View& view, long long index, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::linked, void>
    {
        // normalize index
        size_t norm_index = normalize_index(index, view.size(), true);

        // reserve space for new node and freeze allocator
        typename View::MemGuard guard = view.reserve(view.size() + 1);

        // get iterator to index
        if constexpr (NodeTraits<typename View::Node>::has_prev) {
            if (view.closer_to_tail(norm_index)) {
                auto it = view.rbegin();
                for (size_t i = view.size(); i > norm_index; --i) ++it;
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


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_INSERT_H
