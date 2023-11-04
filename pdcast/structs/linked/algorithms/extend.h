// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_EXTEND_H
#define BERTRAND_STRUCTS_ALGORITHMS_EXTEND_H

#include <Python.h>  // CPython API
#include "../../util/iter.h" // iter()
#include "../../util/python.h"  // is_pyobject<>
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
        view.reserve(items);  // attempt to reserve memory ahead of time

        // TODO: make this safe against resizing.

        size_t idx = 0;
        try {
            for (auto item : util::iter(items)) {
                linked::append(view, item, left);
                ++idx;
            }
        } catch (...) {  // error recovery - remove all appended nodes
            if (left) {
                for (size_t i = 0; i < idx; ++i) {
                    Node* node = view.head();
                    view.unlink(nullptr, node, node->next());
                    view.recycle(node);
                }
            } else {
                if constexpr (NodeTraits<Node>::has_prev) {
                    for (size_t i = 0; i < idx; ++i) {
                        Node* node = view.tail();
                        view.unlink(node->prev(), node, nullptr);
                        view.recycle(node);
                    }
                } else {
                    // NOTE: this branch is not memory safe.  If a resize is triggered
                    // during iteration, the iterator will be invalidated.
                    size_t i = 0;
                    Node* prev = nullptr;
                    Node* curr = view.head();
                    while (i < view.size() - idx) {
                        prev = curr;
                        curr = curr->next();
                        ++i;
                    }
                    while (curr != nullptr) {
                        Node* next = curr->next();
                        view.unlink(prev, curr, next);
                        view.recycle(curr);
                        curr = next;
                    }
                }
            }
            throw;  // propagate
        }
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_EXTEND_H include guard
