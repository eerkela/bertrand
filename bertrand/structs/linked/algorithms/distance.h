#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISTANCE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISTANCE_H

#include <cstddef>  // ssize_t
#include <optional>  // std::optional<>
#include "../../util/ops.h"  // repr()
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace linked {


    template <typename View, typename Item>
    auto distance(const View& view, const Item& item1, const Item& item2)
        -> std::enable_if_t<ViewTraits<View>::hashed, long long>
    {
        using Node = typename View::Node;

        // helper for throwing item not found error
        auto not_found = [](auto& item) {
            return KeyError(repr(item));
        };

        const Node* node1 = view.search(item1);
        if (node1 == nullptr) {
            throw not_found(item1);
        }
        const Node* node2 = view.search(item2);
        if (node2 == nullptr) {
            throw not_found(item2);
        } else if (node1 == node2) {
            return 0;
        }

        size_t idx = 0;
        std::optional<size_t> index1, index2;
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            if (it.curr() == node1) {
                index1 = idx;
                if (index2.has_value()) {
                    break;
                }
            } else if (it.curr() == node2) {
                index2 = idx;
                if (index1.has_value()) {
                    break;
                }
            }
            ++idx;
        }

        size_t i1 = index1.value();
        size_t i2 = index2.value();
        return i2 < i1 ?
            static_cast<long long>(i2 - i1) :
            -1 * static_cast<long long>(i1 - i2);
    }


}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISTANCE_H
