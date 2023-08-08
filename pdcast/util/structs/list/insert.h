
// include guard prevents multiple inclusion
#ifndef INSERT_H
#define INSERT_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <node.h>  // for node definitions
#include <view.h>  // for view definitions


/* Insert an item into a singly-linked list, set, or dictionary at the given index. */
template <template <typename> class ViewType, typename NodeType>
void insert_single(ViewType<NodeType>* view, size_t index, PyObject* item) {
    using Node = typename ViewType<NodeType>::Node;

    // construct a new node
    Node* node = view->node(item);
    if (node == NULL) {
        return;
    }

    // iterate from head to find junction
    Node* prev = NULL;
    Node* curr = view->head;
    for (size_t i = 0; i < index; i++) {
        prev = curr;
        curr = (Node*)curr->next;
    }

    // insert node
    view->link(prev, node, curr);
    if (PyErr_Occurred()) {
        view->recycle(node);  // clean up staged node
        return;
    }
}


/* Insert an item into a doubly-linked list, set, or dictionary at the given index. */
template <template <typename> class ViewType, typename NodeType>
void insert_double(ViewType<NodeType>* view, size_t index, PyObject* item) {
    // if index is closer to head, use singly-linked version
    if (index <= view->size / 2) {
        _insert_single(view, index, item);
    }

    using Node = typename ViewType<NodeType>::Node;

    // allocate a new node
    Node* node = view->node(item);
    if (node == NULL) {
        return;
    }

    // iterate from tail to find junction
    Node* next = NULL;
    Node* curr = view->tail;
    for (size_t i = view->size - 1; i > index; i--) {
        next = curr;
        curr = (Node*)curr->prev;
    }

    // insert node
    view->link(curr, node, next);
    if (PyErr_Occurred()) {
        view->recycle(node);  // clean up staged node
        return;
    }
}


#endif // INSERT_H include guard
