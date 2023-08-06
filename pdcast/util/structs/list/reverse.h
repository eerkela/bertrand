
// include guard prevents multiple inclusion
#ifndef REVERSE_H
#define REVERSE_H

#include <node.h>  // for node definitions
#include <view.h>  // for view definitions


/* Reverse a singly-linked list in-place. */
template <typename NodeType>
inline void reverse_single(ListView<NodeType>* view) {
    _reverse_next(view, view->head);
}


/* Reverse a singly-linked set in-place. */
template <typename NodeType>
inline void reverse_single(SetView<NodeType>* view) {
    _reverse_next(view, view->head);
}


/* Reverse a singly-linked dictionary in-place. */
template <typename NodeType>
inline void reverse_single(DictView<NodeType>* view) {
    _reverse_next(view, view->head);
}


/* Reverse a doubly-linked list in-place. */
template <typename NodeType>
inline void reverse_double(ListView<NodeType>* view) {
    _reverse_next_prev(view, view->head);
}


/* Reverse a doubly-linked set in-place. */
template <typename NodeType>
inline void reverse_double(SetView<NodeType>* view) {
    _reverse_next_prev(view, view->head);
}


/* Reverse a doubly-linked dictionary in-place. */
template <typename NodeType>
inline void reverse_double(DictView<NodeType>* view) {
    _reverse_next_prev(view, view->head);
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Reverse the `next` pointer of every node in a list. */
template <template <typename> class ViewType, typename T, typename U>
inline void _reverse_next(ViewType<T>* view, U* head) {
    U* curr = head;
    U* next;
    U* prev = NULL;

    // swap all next pointers
    while (curr != NULL) {
        next = (U*)curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }

    // swap head/tail pointers
    view->head = view->tail;
    view->tail = head;
}


/* Reverse the `next` and `prev` pointers of every node in a list. */
template <template <typename> class ViewType, typename T, typename U>
inline void _reverse_next_prev(ViewType<T>* view, U* head) {
    U* curr = head;
    U* next;

    // swap all next/prev pointers
    while (curr != NULL) {
        next = (U*)curr->next;
        curr->next = (U*)curr->prev;
        curr->prev = next;
        curr = next;
    }

    // swap head/tail pointers
    view->head = view->tail;
    view->tail = head;
}


#endif // REVERSE_H include guard
