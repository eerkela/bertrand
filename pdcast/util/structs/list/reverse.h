
// include guard prevents multiple inclusion
#ifndef REVERSE_H
#define REVERSE_H

#include <node.h>  // for nodes
#include <view.h>  // for views


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


////////////////////////
////    WRAPPERS    ////
////////////////////////


// NOTE: Cython doesn't play well with nested templates, so we need to
// explicitly instantiate specializations for each combination of node/view
// type.  This is a bit of a pain, put it's the only way to get Cython to
// properly recognize the functions.

// Maybe in a future release we won't have to do this:


template void reverse_single(ListView<SingleNode>* view);
template void reverse_single(SetView<SingleNode>* view);
template void reverse_single(DictView<SingleNode>* view);
template void reverse_single(ListView<DoubleNode>* view);
template void reverse_single(SetView<DoubleNode>* view);
template void reverse_single(DictView<DoubleNode>* view);
template void reverse_double(ListView<DoubleNode>* view);
template void reverse_double(SetView<DoubleNode>* view);
template void reverse_double(DictView<DoubleNode>* view);



#endif // REVERSE_H include guard
