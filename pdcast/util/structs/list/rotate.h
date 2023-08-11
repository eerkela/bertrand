
// include guard prevents multiple inclusion
#ifndef ROTATE_H
#define ROTATE_H

#include <cstddef>  // for size_t
#include <cmath>  // for abs()
#include <Python.h>  // for CPython API
#include <utility>  // for std::pair
#include "node.h"  // for nodes
#include "view.h"  // for views


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
    size_t rotate_left = (steps < 0);
    if (rotate_left) {  // count from head
        index = norm_steps;
    } else {  // count from tail
        index = view->size - norm_steps;
    }

    Node* new_head;
    Node* new_tail;

    // identify new head and tail of rotated list
    if constexpr (is_doubly_linked<Node>::value) {
        // NOTE: if the list is doubly-linked, then we can iterate in either
        // direction to find the junction point.
        if (index > view->size / 2) {  // backward traversal
            new_head = view->tail;
            for (size_t i = view->size - 1; i > index; i--) {
                new_head = static_cast<Node*>(new_head->prev);
            }
            new_tail = static_cast<Node*>(new_head->prev);

            // split list at junction and join previous head/tail
            Node::split(new_tail, new_head);
            Node::join(view->tail, view->head);

            // update head/tail pointers
            view->head = new_head;
            view->tail = new_tail;
            return;
        }
    }

    // forward traversal
    new_tail = view->head;
    for (size_t i = 1; i < index; i++) {
        new_tail = static_cast<Node*>(new_tail->next);
    }
    new_head = static_cast<Node*>(new_tail->next);

    // split at junction and join previous head/tail
    Node::split(new_tail, new_head);
    Node::join(view->tail, view->head);

    // update head/tail pointers
    view->head = new_head;
    view->tail = new_tail;
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
