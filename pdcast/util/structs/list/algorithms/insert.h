// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_INSERT_H
#define BERTRAND_STRUCTS_ALGORITHMS_INSERT_H

#include <cstddef>  // size_t
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/bounds.h"  // walk()
#include "../core/node.h"  // is_doubly_linked<>
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
        std::pair<Node*, Node*> neighbors = junction(
            view, view->head, index, true
        );

        // insert node between neighbors
        _insert_between(view, neighbors.first, neighbors.second, item, false);
    }

    /////////////////////////////////
    ////    INSERT_RELATIVE()    ////
    /////////////////////////////////

    /* Insert an item into a linked set or dictionary relative to a given sentinel
    value. */
    template <typename View>
    inline void insert_relative(
        View* view,
        PyObject* item,
        PyObject* sentinel,
        Py_ssize_t offset
    ) {
        _insert_relative(view, item, sentinel, offset, true);  // propagate errors
    }

    //////////////////////////////
    ////    ADD_RELATIVE()    ////
    //////////////////////////////

    /* Add an item to a linked set or dictionary relative to a given sentinel
    value if it is not already present. */
    template <typename View>
    inline void add_relative(
        View* view,
        PyObject* item,
        PyObject* sentinel,
        Py_ssize_t offset
    ) {
        _insert_relative(view, item, sentinel, offset, false);  // suppress errors
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


/* Implement both insert_relative() and add_relative() depending on error handling
flag. */
template <typename View>
void _insert_relative(
    View* view,
    PyObject* item,
    PyObject* sentinel,
    Py_ssize_t offset,
    bool update
) {
    using Node = typename View::Node;

    // ensure offset is nonzero
    if (offset == 0) {
        PyErr_Format(PyExc_ValueError, "offset must be non-zero");
        return;
    } else if (offset < 0) {
        offset += 1;
    }

    // search for sentinel
    Node* node = view->search(sentinel);
    if (node == nullptr) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R is not contained in the set", sentinel);
        return;
    }

    // walk according to offset
    std::pair<Node*,Node*> neighbors = relative_junction(
        view, node, offset, true
    );

    // insert node between neighbors
    _insert_between(view, neighbors.first, neighbors.second, item, update);
}


#endif // BERTRAND_STRUCTS_ALGORITHMS_INSERT_H include guard
