
// include guard prevents multiple inclusion
#ifndef INSERT_H
#define INSERT_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <node.h>  // for node definitions
#include <view.h>  // for view definitions


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Insert an item into a singly-linked list, set, or dictionary at the given index. */
template <template <typename> class ViewType, typename NodeType>
void insert_single(ViewType<NodeType>* view, size_t index, PyObject* item) {
    _insert_forward(view, view->head, index, item);
}


/* Insert an item into a doubly-linked list, set, or dictionary at the given index. */
template <template <typename> class ViewType, typename NodeType>
void insert_double(ViewType<NodeType>* view, size_t index, PyObject* item) {
    // if index is closer to head, use singly-linked version
    if (index <= view->size / 2) {
        _insert_forward(view, view->head, index, item);
    } else {
        _insert_backward(view, view->tail, index, item);
    }
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Iterate forwards from head and insert a node at the given index. */
template <template <typename> class ViewType, typename T, typename U>
void _insert_forward(ViewType<T>* view, U* head, size_t index, PyObject* item) {
    // allocate a new node
    U* node = view->allocate(item);
    if (node == NULL) {  // TypeError() during hash() / tuple unpacking
        return;
    }

    // iterate from head to find junction
    U* curr = view->head;
    U* prev = NULL;  // shadows curr
    for (size_t i = 0; i < index; i++) {
        prev = curr;
        curr = (U*)curr->next;
    }

    // insert node
    try {
        view->link(prev, node, curr);
    } catch (const std::bad_alloc&) {  // error during resize()
        view->deallocate(node);
        throw;
    }
    if (PyErr_Occurred()) {  // ValueError() item is already contained in set
        view->deallocate(node);
        throw;
    }
}


/* Iterate backwards from tail and insert a node at the given index. */
template <template <typename> class ViewType, typename T, typename U>
void _insert_backward(ViewType<T>* view, U* tail, size_t index, PyObject* item) {
    // allocate a new node
    U* node = view->allocate(item);
    if (node == NULL) {  // TypeError() during hash() / tuple unpacking
        return;
    }

    // iterate from tail to find junction
    U* curr = view->tail;
    U* next = NULL;  // shadows curr
    for (size_t i = view->size - 1; i > index; i--) {
        next = curr;
        curr = (U*)curr->prev;
    }

    // insert node
    try {
        view->link(curr, node, next);
    } catch (const std::bad_alloc&) {  // error during resize()
        view->deallocate(node);
        throw;
    }
    if (PyErr_Occurred()) {  // ValueError() item is already contained in set
        view->deallocate(node);
        throw;
    }
}


#endif // INSERT_H include guard
