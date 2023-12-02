#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_EXTEND_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_EXTEND_H

#include <Python.h>  // CPython API
#include "../../util/iter.h" // iter()
#include "../core/view.h"  // ViewTraits
#include "append.h"  // append()


namespace bertrand {
namespace structs {
namespace linked {


    /* NOTE: handling memory is a bit tricky here.  First, we want to optimize to
     * preallocate memory ahead of time if possible.  However, the container may not
     * have a definite size, so we also need to allow dynamic growth as elements are
     * added.  Secondly, if an error is encountered, we need to delay any resize until
     * after the exception is handled.  This allows us to safely iterate over the list
     * and remove any nodes that were previously added.
     */


    /* Add multiple items to the end of a list, set, or dictionary. */
    template <typename View, typename Container>
    auto extend(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::listlike, void>
    {
        using MemGuard = typename View::MemGuard;
        MemGuard guard = view.try_reserve(items);  // preallocate if size is known
        size_t size = view.size();
        try {
            for (auto item : iter(items)) {
                view.link(view.tail(), view.node(item), nullptr);
            }

        // restore list on error
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


    /* Add multiple items to the end of a list, set, or dictionary. */
    template <typename View, typename Container>
    auto extend_left(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::listlike, void>
    {
        using MemGuard = typename View::MemGuard;
        MemGuard guard = view.try_reserve(items);  // preallocate if size is known
        size_t size = view.size();
        try {
            for (auto item : iter(items)) {
                view.link(nullptr, view.node(item), view.head());
            }

        // restore list on error
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
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_EXTEND_H
