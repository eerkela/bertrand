// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_DISCARD_H
#define BERTRAND_STRUCTS_ALGORITHMS_DISCARD_H

#include <type_traits>  // std::enable_if_t<>
#include "../core/node.h"  // NodeTrats
#include "../core/view.h"  // ViewTrats


namespace bertrand {
namespace structs {
namespace linked {


    /* Remove an item from a linked set or dictionary if it is present. */
    template <
        typename View,
        typename Item = typename View::Value
    >
    auto discard(View& view, Item& item)
        -> std::enable_if_t<ViewTraits<View>::setlike, void>
    {
        using Node = typename View::Node;

        // search for node
        Node* curr = view.search(item);
        if (curr == nullptr) {
            return;  // item not found
        }

        // get neighboring nodes
        Node* prev;
        if constexpr (NodeTraits<Node>::has_prev) {  // O(1) if doubly-linked
            prev = curr->prev();
        } else {
            auto it = view.begin();
            while (it.next() != curr) ++it;
            prev = it.curr();
        }

        // unlink and free node
        view.unlink(prev, curr, curr->next());
        view.recycle(curr);
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_ALGORITHMS_DISCARD_H include guard
