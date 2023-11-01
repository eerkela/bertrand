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

        // note original head/tail in case of error
        Node* original = left ? view.head() : view.tail();
        try {
            for (auto item : util::iter(items)) {
                linked::append(view, item, left);
            }
        } catch (...) {  // error recovery - remove all appended nodes
            if (left) {
                // if we linked to head, remove until we reach the original head
                Node* curr = view.head();
                while (curr != original) {
                    Node* next = curr->next();
                    view.unlink(nullptr, curr, next);
                    view.recycle(curr);
                    curr = next;
                }
            } else {
                // otherwise, start from original tail and remove until end of list
                Node* curr = original->next();
                while (curr != nullptr) {
                    Node* next = curr->next();
                    view.unlink(original, curr, next);
                    view.recycle(curr);
                    curr = next;
                }
            }
            throw;  // propagate original error
        }
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_EXTEND_H include guard
