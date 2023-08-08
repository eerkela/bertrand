
// include guard prevents multiple inclusion
#ifndef REVERSE_H
#define REVERSE_H

#include <node.h>  // for node definitions
#include <view.h>  // for view definitions


/* Reverse a singly-linked list in-place. */
template <template <typename> class ViewType, typename NodeType>
inline void reverse_single(ViewType<NodeType>* view) {
    using Node = typename ViewType<NodeType>::Node;

    // save original `head` pointer
    Node* head = view->head;

    // swap all `next` pointers
    Node* prev = NULL;
    Node* curr = head;
    while (curr != NULL) {
        Node* next = (Node*)curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }

    // swap `head`/`tail` pointers
    view->head = view->tail;
    view->tail = head;
}


/* Reverse a doubly-linked list in-place. */
template <template <typename> class ViewType, typename NodeType>
inline void reverse_double(ViewType<NodeType>* view) {
    using Node = typename ViewType<NodeType>::Node;

    // save original `head` pointer
    Node* head = view->head;

    // swap all `next`/`prev` pointers
    Node* curr = head;
    while (curr != NULL) {
        Node* next = (Node*)curr->next;
        curr->next = (Node*)curr->prev;
        curr->prev = next;
        curr = next;
    }

    // swap `head`/`tail` pointers
    view->head = view->tail;
    view->tail = head;
}


#endif // REVERSE_H include guard
