// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_EXTEND_H
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
    auto extend(View& view, Container& items)
        -> std::enable_if_t<ViewTraits<View>::listlike, void>
    {
        using MemGuard = typename View::MemGuard;

        // preallocate if possible (unless size is unknown)
        MemGuard guard = view.try_reserve(items);

        // append each item
        size_t size = view.size();
        try {
            for (auto item : iter(items)) {
                view.link(view.tail(), view.node(item), nullptr);
            }

        // clean up nodes on error
        } catch (...) {
            size_t idx = view.size() - size;  // number of nodes to remove
            if (idx == 0) throw;  // nothing to clean up
            MemGuard hold = view.reserve();  // hold allocator at current size

            // remove last idx nodes
            if constexpr (NodeTraits<typename View::Node>::has_prev) {
                auto it = view.rbegin();
                for (size_t i = 0; i < idx; ++i) view.recycle(it.drop());
            } else {
                auto it = view.begin();  // forward traversal
                for (size_t i = 0; i < view.size() - idx; ++i) ++it;
                for (size_t i = 0; i < idx; ++i) view.recycle(it.drop());
            }
            throw;  // propagate
        }
    }


    /* Add multiple items to the end of a list, set, or dictionary. */
    template <typename View, typename Container>
    auto extend_left(View& view, Container& items)
        -> std::enable_if_t<ViewTraits<View>::listlike, void>
    {
        using MemGuard = typename View::MemGuard;

        // NOTE: handling memory is a bit tricky here.  First, we want to optimize to
        // preallocate memory ahead of time if possible.  However, the container may
        // not have a definite size, so we also need to allow dynamic growth as
        // elements are added.  Secondly, if an error is encountered, we need to delay
        // any resize until after the exception is handled.  This allows us to safely
        // iterate over the list and remove any nodes that were previously added.

        // preallocate if possible (unless size is unknown)
        MemGuard guard = view.try_reserve(items);

        // append each item
        size_t size = view.size();
        try {
            for (auto item : iter(items)) {
                view.link(nullptr, view.node(item), view.head());
            }

        // clean up nodes on error
        } catch (...) {
            size_t idx = view.size() - size;  // number of nodes to remove
            if (idx == 0) throw;  // nothing to clean up
            MemGuard hold = view.reserve();  // hold allocator at current size

            // remove first idx nodes
            auto it = view.begin();
            for (size_t i = 0; i < idx; ++i) view.recycle(it.drop());
            throw;  // propagate
        }
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_EXTEND_H
