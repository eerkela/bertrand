// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISTANCE_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISTANCE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISTANCE_H

#include <cstddef>  // ssize_t
#include <optional>  // std::optional<>
#include <sstream>  // std::ostringstream
#include "../../util/repr.h"  // repr()
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Get the linear distance between two values in a linked set or dictionary. */
    template <typename View, typename Item = typename View::Value>
    auto distance(View& view, Item& item1, Item& item2)
        -> std::enable_if_t<ViewTraits<View>::setlike, long long>
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
        if (i1 < i2) {
            return -1 * static_cast<long long>(i2 - i1);
        } else {
            return static_cast<long long>(i1 - i2);
        }
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISTANCE_H
