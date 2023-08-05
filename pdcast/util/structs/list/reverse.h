
// include guard prevents multiple inclusion
#ifndef REVERSE_H
#define REVERSE_H

#include <node.h>  // for node definitions
#include <view.h>  // for view definitions


/* Reverse a singly-linked list in-place. */
template <typename NodeType>
inline void reverse_single(ListView<NodeType>* view) {
    NodeType* curr = view->head;
    NodeType* next;
    NodeType* prev = NULL;

    // swap all next pointers
    while (curr != NULL) {
        next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }

    // swap head/tail pointers
    curr = view->head;
    view->head = view->tail;
    view->tail = curr;
}


/* Reverse a singly-linked set in-place. */
template <typename NodeType>
inline void reverse_single(SetView<NodeType>* view) {
    Hashed<NodeType>* curr = view->head;
    Hashed<NodeType>* next;
    Hashed<NodeType>* prev = NULL;

    // swap all next pointers
    while (curr != NULL) {
        next = (Hashed<NodeType>*)curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }

    // swap head/tail pointers
    curr = view->head;
    view->head = view->tail;
    view->tail = curr;
}


/* Reverse a singly-linked dictionary in-place. */
template <typename NodeType>
inline void reverse_single(DictView<NodeType>* view) {
    Mapped<NodeType>* curr = view->head;
    Mapped<NodeType>* next;
    Mapped<NodeType>* prev = NULL;

    // swap all next pointers
    while (curr != NULL) {
        next = (Mapped<NodeType>*)curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }

    // swap head/tail pointers
    curr = view->head;
    view->head = view->tail;
    view->tail = curr;
}


/* Reverse a doubly-linked list in-place. */
template <typename NodeType>
inline void reverse_double(ListView<NodeType>* view) {
    NodeType* curr = view->head;
    NodeType* next;

    // swap all next/prev pointers
    while (curr != NULL) {
        next = curr->next;
        curr->next = curr->prev;
        curr->prev = next;
        curr = next;
    }

    // swap head/tail pointers
    curr = view->head;
    view->head = view->tail;
    view->tail = curr;
}


/* Reverse a doubly-linked set in-place. */
template <typename NodeType>
inline void reverse_double(SetView<NodeType>* view) {
    Hashed<NodeType>* curr = view->head;
    Hashed<NodeType>* next;

    // swap all next/prev pointers
    while (curr != NULL) {
        next = (Hashed<NodeType>*)curr->next;
        curr->next = (Hashed<NodeType>*)curr->prev;
        curr->prev = next;
        curr = next;
    }

    // swap head/tail pointers
    curr = view->head;
    view->head = view->tail;
    view->tail = curr;
}


/* Reverse a doubly-linked dictionary in-place. */
template <typename NodeType>
inline void reverse_double(DictView<NodeType>* view) {
    Mapped<NodeType>* curr = view->head;
    Mapped<NodeType>* next;

    // swap all next/prev pointers
    while (curr != NULL) {
        next = (Mapped<NodeType>*)curr->next;
        curr->next = (Mapped<NodeType>*)curr->prev;
        curr->prev = next;
        curr = next;
    }

    // swap head/tail pointers
    curr = view->head;
    view->head = view->tail;
    view->tail = curr;
}


#endif // REVERSE_H include guard
