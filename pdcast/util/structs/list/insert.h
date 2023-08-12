
// include guard prevents multiple inclusion
#ifndef INSERT_H
#define INSERT_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include "node.h"  // for nodes
#include "view.h"  // for views


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Insert an item into a singly-linked list, set, or dictionary at the given index. */
template <template <typename> class ViewType, typename NodeType>
void insert(ViewType<NodeType>* view, size_t index, PyObject* item) {
    using Node = typename ViewType<NodeType>::Node;

    // allocate a new node
    Node* node = view->node(item);
    if (node == nullptr) {
        return;
    }

    // NOTE: if the index is closer to the tail and the list is doubly-linked,
    // we can iterate from the tail to save time.
    if constexpr (is_doubly_linked<Node>::value) {
        if (index > view->size / 2) {
            // iterate from tail to find junction
            Node* next = nullptr;
            Node* curr = view->tail;
            for (size_t i = view->size - 1; i > index; i--) {
                next = curr;
                curr = static_cast<Node*>(curr->prev);
            }

            // insert node
            view->link(curr, node, next);
            if (PyErr_Occurred()) {
                view->recycle(node);  // clean up staged node
                return;
            }
        }
    }

    // NOTE: otherwise, we iterate from the head
    Node* prev = nullptr;
    Node* curr = view->head;
    for (size_t i = 0; i < index; i++) {
        prev = curr;
        curr = static_cast<Node*>(curr->next);
    }

    // insert node
    view->link(prev, node, curr);
    if (PyErr_Occurred()) {
        view->recycle(node);  // clean up staged node
        return;
    }
}


////////////////////////
////    WRAPPERS    ////
////////////////////////


// NOTE: Cython doesn't play well with heavily templated functions, so we need
// to explicitly instantiate the specializations we need.  Maybe in a future
// release we won't have to do this:


template void insert(ListView<SingleNode>* view, size_t index, PyObject* item);
template void insert(SetView<SingleNode>* view, size_t index, PyObject* item);
template void insert(DictView<SingleNode>* view, size_t index, PyObject* item);
template void insert(ListView<DoubleNode>* view, size_t index, PyObject* item);
template void insert(SetView<DoubleNode>* view, size_t index, PyObject* item);
template void insert(DictView<DoubleNode>* view, size_t index, PyObject* item);


#endif // INSERT_H include guard
