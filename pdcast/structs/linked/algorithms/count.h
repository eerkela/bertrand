#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_COUNT_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_COUNT_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <type_traits>  // std::enable_if_t<>
#include "../../util/ops.h"  // eq()
#include "../core/view.h"  // ViewTraits
#include "slice.h"  // normalize_slice()


namespace bertrand {
namespace linked {


    template <Yield yield = Yield::KEY, typename View, typename Item>
    auto count(
        const View& view,
        const Item& item,
        std::optional<long long> start,
        std::optional<long long> stop
    ) -> std::enable_if_t<ViewTraits<View>::listlike || yield == Yield::VALUE, size_t>
    {
        using Node = typename View::Node;

        if (view.size() == 0) {
            return 0;
        }

        SliceIndices<const View> indices = normalize_slice(view, start, stop);
        size_t norm_start = indices.start;
        size_t norm_stop = indices.stop;
        if (norm_start == norm_stop) {
            return 0;
        } else if (norm_start > norm_stop) {
            throw IndexError("start index cannot be greater than stop index");
        }

        // NOTE: if list is doubly-linked and stop is closer to tail than start is to
        // head, then we iterate backward from the tail
        if constexpr (NodeTraits<Node>::has_prev) {
            if (indices.backward) {
                size_t idx = view.size() - 1;
                auto it = view.template rbegin<yield>();
                for (; idx >= norm_stop; --idx, ++it);

                size_t count = 0;
                for (; idx >= norm_start; --idx, ++it) {
                    count += eq(*it, item);
                }
                return count;
            }
        }

        // otherwise, we have to iterate from the head
        size_t idx = 0;
        auto it = view.template begin<yield>();
        for (; idx < norm_start; ++idx, ++it);

        size_t count = 0;
        for (; idx < norm_stop; ++idx, ++it) {
            count += eq(*it, item);
        }
        return count;
    }


    template <Yield yield = Yield::KEY, typename View, typename Item>
    auto count(
        const View& view,
        const Item& item,
        std::optional<long long> start,
        std::optional<long long> stop
    ) -> std::enable_if_t<ViewTraits<View>::hashed && yield != Yield::VALUE, size_t>
    {
        using Node = typename View::Node;

        if (view.size() == 0) {
            return 0;
        }

        SliceIndices<const View> indices = normalize_slice(view, start, stop);
        size_t norm_start = indices.start;
        size_t norm_stop = indices.stop;
        if (norm_start == norm_stop) {
            return 0;
        } else if (norm_start > norm_stop) {
            throw IndexError("start index cannot be greater than stop index");
        }

        const Node* node;
        if constexpr (yield == Yield::ITEM) {
            static_assert(
                is_pairlike<Item>,
                "item must be pair-like (e.g. std::pair or std::tuple of size 2)"
            );

            node = view.search(std::get<0>(item));
            if (node == nullptr || !eq(node->mapped(), std::get<1>(item))) {
                return 0;
            }
        } else {
            node = view.search(item);
            if (node == nullptr) {
                return 0;
            }
        }

        if (norm_start == 0 && norm_stop == view.size() - 1) {
            return 1;
        }

        size_t idx = 0;
        for (auto it = view.template begin<yield>(); idx < norm_stop; ++it, ++idx) {
            if (it.curr() == node) break;
        }
        return idx >= norm_start && idx < norm_stop;
    }


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_COUNT_H
