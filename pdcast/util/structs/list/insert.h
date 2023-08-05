
// include guard prevents multiple inclusion
#ifndef INSERT_H
#define INSERT_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <node.h>  // for node definitions
#include <view.h>  // for view definitions


/* Insert an item into a singly-linked list at the given index. */
template <typename NodeType>
void insert_single(ListView<NodeType>* view, size_t index, PyObject* item) {
    // allocate a new node
    NodeType* node = view->allocate(item);
    if (node == NULL) {  // MemoryError()
        return;
    }

    // iterate from head to find junction
    NodeType* curr = view->head;
    NodeType* prev = NULL;  // shadows curr
    for (size_t i = 0; i < index; i++) {
        prev = curr;
        curr = curr->next;
    }

    // insert node
    view->link(prev, node, curr);
}


/* Insert an item into a singly-linked set at the given index. */
template <typename NodeType>
void insert_single(SetView<NodeType>* view, size_t index, PyObject* item) {
    // allocate a new node
    Hashed<NodeType>* node = view->allocate(item);
    if (node == NULL) {  // MemoryError() or TypeError() during hash()
        return;
    }

    // iterate from head to find junction
    Hashed<NodeType>* curr = view->head;
    Hashed<NodeType>* prev = NULL;  // shadows curr
    for (size_t i = 0; i < index; i++) {
        prev = curr;
        curr = (Hashed<NodeType>*)curr->next;
    }

    // insert node
    view->link(prev, node, curr);
}


/* Insert an item into a singly-linked dictionary at the given index. */
template <typename NodeType>
void insert_single(DictView<NodeType>* view, size_t index, PyObject* item) {
    // allocate a new node
    Mapped<NodeType>* node = view->allocate(item);
    if (node == NULL) {  // MemoryError() or TypeError() during hash()
        return;
    }

    // iterate from head to find junction
    Mapped<NodeType>* curr = view->head;
    Mapped<NodeType>* prev = NULL;  // shadows curr
    for (size_t i = 0; i < index; i++) {
        prev = curr;
        curr = (Mapped<NodeType>*)curr->next;
    }

    // insert node
    view->link(prev, node, curr);
}


/* Insert an item into a doubly-linked list at the given index. */
template <typename NodeType>
void insert_double(ListView<NodeType>* view, size_t index, PyObject* item) {
    // if index is closer to head, use singly-linked version
    if (norm_index <= view->size / 2) {
        return insert_single(view, index, item);
    }

    // else, start from tail
    NodeType* node = view->allocate(item);
    if (node == NULL) {  // MemoryError()
        return;
    }

    // find node at index
    NodeType* curr = view->tail;
    NodeType* next = NULL;  // shadows curr
    for (size_t i = view->size - 1; i > index; i--) {
        next = curr;
        curr = curr->prev;
    }

    // insert node
    view->link(curr, node, next);
}


/* Insert an item into a doubly-linked set at the given index. */
template <typename NodeType>
void insert_double(SetView<NodeType>* view, size_t index, PyObject* item) {
    // if index is closer to head, use singly-linked version
    if (norm_index <= view->size / 2) {
        return insert_single(view, index, item);
    }

    // else, start from tail
    Hashed<NodeType>* node = view->allocate(item);
    if (node == NULL) {  // MemoryError() or TypeError() during hash()
        return;
    }

    // find node at index
    Hashed<NodeType>* curr = view->tail;
    Hashed<NodeType>* next = NULL;  // shadows curr
    for (size_t i = view->size - 1; i > index; i--) {
        next = curr;
        curr = (Hashed<NodeType>*)curr->prev;
    }

    // insert node
    view->link(curr, node, next);
}


/* Insert an item into a doubly-linked dictionary at the given index. */
template <typename NodeType>
void insert_double(DictView<NodeType>* view, size_t index, PyObject* item) {
    // if index is closer to head, use singly-linked version
    if (index <= view->size / 2) {
        return insert_single(view, index, item);
    }

    // else, start from tail
    Mapped<NodeType>* node = view->allocate(item);
    if (node == NULL) {  // MemoryError() or TypeError() during hash()
        return;
    }

    // find node at index
    Mapped<NodeType>* curr = view->tail;
    Mapped<NodeType>* next = NULL;  // shadows curr
    for (size_t i = view->size - 1; i > index; i--) {
        next = curr;
        curr = (Mapped<NodeType>*)curr->prev;
    }

    // insert node
    view->link(curr, node, next);
}


#endif // INSERT_H include guard
