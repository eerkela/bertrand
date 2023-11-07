// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_EXTEND_H
#define BERTRAND_STRUCTS_ALGORITHMS_EXTEND_H

#include <Python.h>  // CPython API
#include "../../util/iter.h" // iter()
#include "../../util/python.h"  // is_pyobject<>, len()
#include "../core/view.h"  // ViewTraits
#include "append.h"  // append()


namespace bertrand {
namespace structs {
namespace linked {


    /* Add multiple items to the end of a list, set, or dictionary. */
    template <
        typename View,
        typename Container
    >
    auto extend(View& view, Container& items, bool left)
        -> std::enable_if_t<ViewTraits<View>::listlike, void>
    {
        using Node = typename View::Node;
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
        size_t idx = 0;
        try {
            for (auto item : util::iter(items)) {
                linked::append(view, item, left);
                ++idx;
            }

        // clean up nodes on error
        } catch (...) {
            if (idx == 0) throw;  // nothing to clean up
            MemGuard hold = view.reserve();  // hold allocator at current size

            // if appending to head, remove first idx nodes
            if (left) {
                auto it = view.begin();
                for (size_t i = 0; i < idx; ++i) view.recycle(it.drop());

            // otherwise, remove last idx nodes
            } else {
                if constexpr (NodeTraits<Node>::has_prev) {
                    auto it = view.rbegin();
                    for (size_t i = 0; i < idx; ++i) view.recycle(it.drop());
                } else {
                    auto it = view.begin();  // forward traversal
                    for (size_t i = 0; i < view.size() - idx; ++i) ++it;
                    for (size_t i = 0; i < idx; ++i) view.recycle(it.drop());
                }
            }
            throw;  // propagate
        }
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_EXTEND_H include guard
