
// include guard prevents multiple inclusion
#ifndef REVERSE_H
#define REVERSE_H

#include <view.h>  // for views


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace SinglyLinked {

    /* Reverse a list in-place. */
    template <typename NodeType>
    inline void reverse(ListView<NodeType>* view) {
        NodeType* curr = view->head;
        NodeType* next;
        NodeType* prev = NULL;

        // swap all next/prev pointers
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

    /* Reverse a set in-place. */
    template <typename NodeType>
    inline void reverse(SetView<NodeType>* view) {
        Hashed<NodeType>* curr = view->head;
        Hashed<NodeType>* next;
        Hashed<NodeType>* prev = NULL;

        // swap all next/prev pointers
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

    /* Reverse a dictionary in-place. */
    template <typename NodeType>
    inline void reverse(DictView<NodeType>* view) {
        Mapped<NodeType>* curr = view->head;
        Mapped<NodeType>* next;
        Mapped<NodeType>* prev = NULL;

        // swap all next/prev pointers
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

}


namespace DoublyLinked {

    /* Reverse a list in-place. */
    template <typename NodeType>
    inline void reverse(ListView<NodeType>* view) {
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

    /* Reverse a set in-place. */
    template <typename NodeType>
    inline void reverse(SetView<NodeType>* view) {
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

    /* Reverse a dictionary in-place. */
    template <typename NodeType>
    inline void reverse(DictView<NodeType>* view) {
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

}


#endif // REVERSE_H include guard
