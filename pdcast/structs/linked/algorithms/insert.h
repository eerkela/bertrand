// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_INSERT_H
#define BERTRAND_STRUCTS_ALGORITHMS_INSERT_H

#include <cstddef>  // size_t
#include <type_traits>  // std::enable_if_t<>
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/view.h"  // ViewTraits
#include "position.h"  // position()


namespace bertrand {
namespace structs {
namespace linked {

    /* Insert an item into a linked list, set, or dictionary at the given index. */
    template <
        typename View,
        typename Index,
        typename Item = typename View::Value
    >
    inline auto insert(View& view, Index index, Item& item)
        -> std::enable_if_t<ViewTraits<View>::listlike, void>
    {
        algorithms::list::position(view, index).insert(item);
    }


// namespace Relative {

//     /* Insert an item into a linked set or dictionary relative to a given sentinel
//     value. */
//     template <typename RelativeProxy>
//     inline void insert(RelativeProxy* proxy, PyObject* item) {
//         _insert_relative(proxy, item, false);  // propagate errors
//     }

// }


// ///////////////////////
// ////    PRIVATE    ////
// ///////////////////////


// // NOTE: these are reused for add_relative() as well


// /* Attempt to insert a node between the left and right neighbors. */
// template <typename View, typename Node>
// void _insert_between(
//     View& view,
//     Node* left,
//     Node* right,
//     PyObject* item,
//     bool update
// ) {
//     // allocate a new node
//     Node* curr = view.node(item);
//     if (curr == nullptr) {
//         return;  // propagate error
//     }

//     // check if we should update an existing node
//     if constexpr (is_setlike<View>::value) {
//         if (update) {
//             Node* existing = view.search(curr);
//             if (existing != nullptr) {  // item already exists
//                 if constexpr (has_mapped<Node>::value) {
//                     // update mapped value
//                     Py_DECREF(existing->mapped);
//                     Py_INCREF(curr->mapped);
//                     existing->mapped = curr->mapped;
//                 }
//                 view.recycle(curr);
//                 return;
//             }
//         }
//     }

//     // insert node between neighbors
//     view.link(left, curr, right);
//     if (PyErr_Occurred()) {
//         view.recycle(curr);  // clean up staged node before propagating
//     }
// }


// /* Implement both insert_relative() and add_relative() depending on error handling
// flag. */
// template <typename RelativeProxy>
// void _insert_relative(RelativeProxy* proxy, PyObject* item, bool update) {
//     using Node = typename RelativeProxy::Node;

//     // ensure offset is nonzero
//     if (proxy->offset == 0) {
//         PyErr_Format(PyExc_ValueError, "offset must be non-zero");
//         return;
//     } else if (proxy->offset < 0) {
//         proxy->offset += 1;
//     }

//     // walk according to offset
//     // std::pair<Node*,Node*> neighbors = relative_junction(
//     //     proxy->view, proxy->sentinel, proxy->offset, true
//     // );
//     std::pair<Node*, Node*> neighbors = proxy->junction(proxy->offset, true);

//     // insert node between neighbors
//     _insert_between(proxy->view, neighbors.first, neighbors.second, item, update);
// }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_INSERT_H include guard
