// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISTANCE_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISTANCE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISTANCE_H

#include <cstddef>  // ssize_t
#include <optional>  // std::optional<>
#include <sstream>  // std::ostringstream
#include "../../util/ops.h"  // repr()
#include "../core/view.h"  // ViewTraits


// TODO: distance() should be hidden behind the relative() proxy, such that we
// write set.relative("c").distance("f") to get the distance from "c" to "f", and
// set.relative("f").distance("c") to get the distance from "f" to "c".

// We can do the same with move() vs move_to_index().  The move_to_index() function
// would be available on the set itself via set.move("c", 3), which would move "c" to
// index 3.  The relative moves are available from the proxy via
// set.relative("c").move(3), which would move "c" three steps forward in the set.

// These would be accompanied by 


namespace bertrand {
namespace structs {
namespace linked {


    /* Get the linear distance between two values in a linked set or dictionary. */
    template <typename View, typename Item = typename View::Value>
    auto distance(View& view, Item& item1, Item& item2)
        -> std::enable_if_t<ViewTraits<View>::hashed, long long>
    {
        using Node = typename View::Node;

        // convenience function for throwing item not found error
        auto not_found = [](Item& item) {
            std::ostringstream msg;
            msg << repr(item) << " is not in set";
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
        size_t idx = 0;
        std::optional<size_t> index1, index2;
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            if (it.curr() == node1) {
                index1 = idx;
                if (index2.has_value()) break;
            } else if (it.curr() == node2) {
                index2 = idx;
                if (index1.has_value()) break;
            }
            ++idx;
        }

        // return distance from node1 to node2
        size_t i1 = index1.value();
        size_t i2 = index2.value();
        if (i2 > i1) {
            return static_cast<long long>(i2 - i1);
        } else {
            return -1 * static_cast<long long>(i1 - i2);
        }
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISTANCE_H
