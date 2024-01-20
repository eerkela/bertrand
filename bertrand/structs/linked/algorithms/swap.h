#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_SWAP_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_SWAP_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/ops.h"  // repr()
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace linked {


    template <typename View, typename Item>
    auto swap(View& view, const Item& item1, const Item& item2)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Node = typename View::Node;

        // helper for throwing item not found error
        auto not_found = [](auto item) {
            return KeyError(repr(item));
        };

        Node* node1 = view.search(item1);
        if (node1 == nullptr) {
            throw not_found(item1);
        }
        Node* node2 = view.search(item2);
        if (node2 == nullptr) {
            throw not_found(item2);
        } else if (node1 == node2) {
            return;
        }

        Node* prev1 = nullptr;
        Node* prev2 = nullptr;
        if constexpr (NodeTraits<Node>::has_prev) {
            prev1 = node1->prev();
            prev2 = node2->prev();
        } else {
            for (auto it = view.begin(), end = view.end(); it != end; ++it) {
                if (it.curr() == node1) {
                    prev1 = it.prev();
                    if (prev2 != nullptr) {
                        break;
                    }
                } else if (it.curr() == node2) {
                    prev2 = it.prev();
                    if (prev1 != nullptr) {
                        break;
                    }
                }
            }
        }

        Node* next1 = node1->next();
        Node* next2 = node2->next();
        if (next1 == node2) {
            view.unlink(prev1, node1, node2);
            view.link(node2, node1, next2);
        } else if (prev1 == node2) {
            view.unlink(node2, node1, next1);
            view.link(prev2, node1, node2);
        } else {
            view.unlink(prev1, node1, next1);
            view.link(prev2, node1, next2);
            view.unlink(prev2, node2, next2);
            view.link(prev1, node2, next1);
        }
    }


}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_SWAP_H
