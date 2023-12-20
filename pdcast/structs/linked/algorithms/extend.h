#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_EXTEND_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_EXTEND_H

#include <Python.h>  // CPython API
#include "../../util/iter.h" // iter()
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace linked {


    template <typename View, typename Container>
    auto extend(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::listlike, void>
    {
        using MemGuard = typename View::MemGuard;
        MemGuard guard = view.try_reserve(items);

        size_t size = view.size();
        try {
            for (const auto& item : iter(items)) {
                view.link(view.tail(), view.node(item), nullptr);
            }

        } catch (...) {
            size_t idx = view.size() - size;
            if (idx == 0) {
                throw;
            }

            // hold allocator at current size while we remove nodes
            MemGuard hold = view.reserve();
            if constexpr (NodeTraits<typename View::Node>::has_prev) {
                auto it = view.rbegin();
                for (size_t i = 0; i < idx; ++i) {
                    view.recycle(it.drop());
                }
            } else {
                auto it = view.begin();
                for (size_t i = 0; i < view.size() - idx; ++i, ++it);
                for (size_t i = 0; i < idx; ++i) {
                    view.recycle(it.drop());
                }
            }
            throw;
        }
    }


    template <typename View, typename Container>
    auto extend_left(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::listlike, void>
    {
        using MemGuard = typename View::MemGuard;
        MemGuard guard = view.try_reserve(items);

        size_t size = view.size();
        try {
            for (const auto& item : iter(items)) {
                view.link(nullptr, view.node(item), view.head());
            }

        } catch (...) {
            size_t idx = view.size() - size;
            if (idx == 0) {
                throw;
            }

            // hold allocator at current size while we remove nodes
            MemGuard hold = view.reserve();
            auto it = view.begin();
            for (size_t i = 0; i < idx; ++i) {
                view.recycle(it.drop());
            }
            throw;
        }
    }


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_EXTEND_H
