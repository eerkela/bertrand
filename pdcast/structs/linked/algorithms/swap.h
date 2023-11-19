// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_SWAP_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_SWAP_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_SWAP_H

#include <sstream>  // std::ostringstream
#include <stdexcept>  // std::invalid_argument
#include <type_traits>  // std::enable_if_t<>
#include "../../util/repr.h"  // repr()
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Swap the positions of two values in a linked set or dictionary. */
    template <typename View, typename Item = typename View::Value>
    auto swap(View& view, Item& item1, Item& item2)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Node = typename View::Node;

        // convenience function for throwing item not found error
        auto not_found = [](Item& item) {
            std::ostringstream msg;
            msg << util::repr(item) << " is not in set";
            return std::invalid_argument(msg.str());
        };

        // search for nodes in hash table
        Node* node1 = view.search(item1);
        if (node1 == nullptr) throw not_found(item1);
        Node* node2 = view.search(item2);
        if (node2 == nullptr) throw not_found(item2);

        // trivial case: nodes are identical
        if (node1 == node2) return;

        // get predecessors of both nodes
        Node* prev1 = nullptr;
        Node* prev2 = nullptr;
        if constexpr (NodeTraits<Node>::has_prev) {  // O(1) if doubly-linked
            prev1 = node1->prev();
            prev2 = node2->prev();
        } else {
            // Otherwise, we have to iterate from the head of the list
            for (auto it = view.begin(), end = view.end(); it != end; ++it) {
                if (it.next() == node1) {
                    prev1 = it.curr();
                    if (prev2 != nullptr) break;
                } else if (it.next() == node2) {
                    prev2 = it.curr();
                    if (prev1 != nullptr) break;
                }
            }
        }

        // swap nodes
        Node* next1 = node1->next();
        Node* next2 = node2->next();
        view.unlink(prev1, node1, next1);
        view.unlink(prev2, node2, next2);
        view.link(prev1, node2, next1);
        view.link(prev2, node1, next2);
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_SWAP_H
