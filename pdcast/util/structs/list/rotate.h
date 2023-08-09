
// include guard prevents multiple inclusion
#ifndef ROTATE_H
#define ROTATE_H

#include <cstddef>  // for size_t
#include <cmath>  // for abs()
#include <Python.h>  // for CPython API
#include <utility>  // for std::pair
#include <node.h>  // for nodes
#include <view.h>  // for views


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Rotate a singly-linked list to the right by the specified number of steps. */
template <template <typename> class ViewType, typename NodeType>
void rotate(ViewType<NodeType>* view, ssize_t steps) {
    using Node = typename ViewType<NodeType>::Node;

    // normalize steps
    size_t norm_steps = abs(steps) % view->size;
    if (norm_steps == 0) {
        return;  // rotated list is identical to original
    }

    // get index at which to split the list
    size_t index;
    if (steps < 0) {  // split early and join head to tail
        index = norm_steps;
    } else {  // split late and join tail to head
        index = view->size - norm_steps;
    }

    Node* head;
    Node* tail;

    // identify new head and tail of rotated list
    if constexpr (is_doubly_linked<Node>::value) {
        // NOTE: if the list is doubly-linked, then we can iterate in either
        // direction to find the junction point.
        if (index <= view->size / 2) {  // forward traversal
            tail = view->head;
            for (size_t i = 0; i < index; i++) {
                tail = (Node*)tail->next;
            }
            head = (Node*)tail->next;
        } else {  // backward traversal
            head = view->tail;
            for (size_t i = view->size - 1; i > index; i--) {
                head = (Node*)head->prev;
            }
            tail = (Node*)head->prev;
        }
    } else {
        tail = view->head;
        for (size_t i = 0; i < index; i++) {
            tail = (Node*)tail->next;
        }
        head = (Node*)tail->next;
    }

    // split list at junction and join head/tail based on direction of rotation
    Node::split(tail, head);
    if (steps < 0) {  // rotate left
        Node::join(view->tail, head);
    } else {  // rotate right
        Node::join(tail, view->head);
    }

    // update head/tail pointers
    view->head = head;
    view->tail = tail;
}


////////////////////////
////    WRAPPERS    ////
////////////////////////


// NOTE: Cython doesn't play well with nested templates, so we need to
// explicitly instantiate specializations for each combination of node/view
// type.  This is a bit of a pain, put it's the only way to get Cython to
// properly recognize the functions.

// Maybe in a future release we won't have to do this:


template void rotate(ListView<SingleNode>* view, ssize_t steps);
template void rotate(SetView<SingleNode>* view, ssize_t steps);
template void rotate(DictView<SingleNode>* view, ssize_t steps);
template void rotate(ListView<DoubleNode>* view, ssize_t steps);
template void rotate(SetView<DoubleNode>* view, ssize_t steps);
template void rotate(DictView<DoubleNode>* view, ssize_t steps);


#endif // ROTATE_H include guard
