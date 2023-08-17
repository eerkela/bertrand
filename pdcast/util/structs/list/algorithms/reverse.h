// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_REVERSE_H
#define BERTRAND_STRUCTS_ALGORITHMS_REVERSE_H

#include "../core/node.h"  // is_doubly_linked<>
#include "../core/view.h"  // views


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    /* Reverse a linked list in-place. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    void reverse(ViewType<NodeType, Allocator>* view) {
        using Node = typename ViewType<NodeType, Allocator>::Node;

        // save original `head` pointer
        Node* head = view->head;
        Node* curr = head;
        
        if constexpr (is_doubly_linked<Node>::value) {
            // swap all `next`/`prev` pointers
            while (curr != nullptr) {
                Node* next = static_cast<Node*>(curr->next);
                curr->next = static_cast<Node*>(curr->prev);
                curr->prev = next;
                curr = next;
            }
        } else {
            // swap all `next` pointers
            Node* prev = nullptr;
            while (curr != nullptr) {
                Node* next = static_cast<Node*>(curr->next);
                curr->next = prev;
                prev = curr;
                curr = next;
            }
        }

        // swap `head`/`tail` pointers
        view->head = view->tail;
        view->tail = head;
    }

}


#endif // BERTRAND_STRUCTS_ALGORITHMS_REVERSE_H include guard
