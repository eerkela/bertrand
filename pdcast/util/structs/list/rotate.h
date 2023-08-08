
// include guard prevents multiple inclusion
#ifndef ROTATE_H
#define ROTATE_H

#include <cstddef>  // for size_t
#include <cmath>  // for abs()
#include <Python.h>  // for CPython API
#include <utility>  // for std::pair
#include <node.h>  // for nodes
#include <view.h>  // for views


/* Rotate a singly-linked list to the right by the specified number of steps. */
template <template <typename> class ViewType, typename NodeType>
void rotate_single(ViewType<NodeType>* view, ssize_t steps) {
    // NOTE: due to the singly-linked nature of the list, rotating to the left
    // is O(1) while rotating to the right is O(n).  This is because we need to
    // iterate through the entire list in order to find the new tail.
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

    // identify new head and tail of rotated list
    Node* tail = view->head;
    for (size_t i = 0; i < index; i++) {
        tail = (Node*)tail->next;
    }
    Node* head = (Node*)tail->next;

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


/* Rotate a doubly-linked list to the right by the specified number of steps. */
template <template <typename> class ViewType, typename NodeType>
void rotate_double(ViewType<NodeType>* view, ssize_t steps) {
    // NOTE: doubly-linked lists use the same algorithm as the singly-linked
    // variants, but are slightly faster because we can iterate from either end
    // of the list to find the new head and tail of the list.
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

    // identify new head and tail of rotated list
    Node* head;
    Node* tail;
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


#endif // ROTATE_H include guard
