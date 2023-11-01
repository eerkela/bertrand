// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISTANCE_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISTANCE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISTANCE_H

#include <cstddef>  // ssize_t
#include <sstream>  // std::ostringstream
#include "../../util/repr.h"  // repr()
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Get the linear distance between two values in a linked set or dictionary. */
    template <
        typename View,
        typename Item = typename View::Value
    >
    auto distance(View& view, Item& item1, Item& item2)
        -> std::enable_if_t<ViewTraits<View>::setlike, long long>
    {
        using Node = typename View::Node;

        // convenience function for throwing not-found error
        auto not_found = [] (Item& item) {
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
        if (node1 == node2) return 0;

        // get indices of both nodes
        long long idx = 0;
        long long index1 = -1;
        long long index2 = -1;
        for (auto it = view.begin(), end = view.end(); it != end, ++it) {
            if (it.curr() == node1) {
                index1 = idx;
                if (index2 != -1) break;
            } else if (it.curr() == node2) {
                index2 = idx;
                if (index1 != -1) break;
            }
            ++idx;
        }

        // return distance from node1 to node2
        return index2 - index1;
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISTANCE_H
