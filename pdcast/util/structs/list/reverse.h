
// include guard prevents multiple inclusion
#ifndef REVERSE_H
#define REVERSE_H

#include "node.h"  // for nodes
#include "view.h"  // for views


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Reverse a singly-linked list in-place. */
template <template <typename> class ViewType, typename NodeType>
void reverse(ViewType<NodeType>* view) {
    using Node = typename ViewType<NodeType>::Node;

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


////////////////////////
////    WRAPPERS    ////
////////////////////////


// NOTE: Cython doesn't play well with nested templates, so we need to
// explicitly instantiate specializations for each combination of node/view
// type.  This is a bit of a pain, put it's the only way to get Cython to
// properly recognize the functions.

// Maybe in a future release we won't have to do this:


template void reverse(ListView<SingleNode>* view);
template void reverse(SetView<SingleNode>* view);
template void reverse(DictView<SingleNode>* view);
template void reverse(ListView<DoubleNode>* view);
template void reverse(SetView<DoubleNode>* view);
template void reverse(DictView<DoubleNode>* view);


#endif // REVERSE_H include guard
