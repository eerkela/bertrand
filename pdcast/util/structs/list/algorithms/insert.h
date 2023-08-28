// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_INSERT_H
#define BERTRAND_STRUCTS_ALGORITHMS_INSERT_H

#include <cstddef>  // size_t
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/bounds.h"  // walk()
#include "../core/view.h"  // views


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    ////////////////////////
    ////    INSERT()    ////
    ////////////////////////

    /* Insert an item into a linked list, set, or dictionary at the given index. */
    template <typename View, typename T>
    inline void insert(View* view, T index, PyObject* item) {
        using Node = typename View::Node;

        // get neighboring nodes at index
        size_t idx = normalize_index(index, view->size, true);
        std::pair<Node*, Node*> neighbors = junction(view, view->head, idx);

        // insert node between neighbors
        _insert_between(view, neighbors.first, neighbors.second, item, false);
    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Attempt to insert a node between the left and right neighbors. */
template <typename View, typename Node>
void _insert_between(
    View* view,
    Node* left,
    Node* right,
    PyObject* item,
    bool update
) {
    // allocate a new node
    Node* curr = view->node(item);
    if (curr == nullptr) {
        return;  // propagate error
    }

    // check if we should update an existing node
    if constexpr (is_setlike<View>::value) {
        if (update) {
            Node* existing = view->search(curr);
            if (existing != nullptr) {  // item already exists
                if constexpr (has_mapped<Node>::value) {
                    // update mapped value
                    Py_DECREF(existing->mapped);
                    Py_INCREF(curr->mapped);
                    existing->mapped = curr->mapped;
                }
                view->recycle(curr);
                return;
            }
        }
    }

    // insert node between neighbors
    view->link(left, curr, right);
    if (PyErr_Occurred()) {
        view->recycle(curr);  // clean up staged node before propagating
    }
}


#endif // BERTRAND_STRUCTS_ALGORITHMS_INSERT_H include guard
