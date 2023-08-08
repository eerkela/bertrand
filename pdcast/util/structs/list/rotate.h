
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


// NOTE: due to the singly-linked nature of the list, rotating to the left is
// O(1) while rotating to the right is O(n).  This is because we need to
// iterate through the entire list in order to find the new tail.


/* Rotate a singly-linked list to the right by the specified number of steps. */
template <typename NodeType>
inline void rotate_single(ListView<NodeType>* view, ssize_t steps) {
    // normalize steps
    size_t norm_steps = abs(steps) % view->size;
    if (norm_steps == 0) {  // rotated list is identical to original
        return;
    }

    // rotate the list
    std::pair<NodeType*, NodeType*> bounds = _rotate_single(
        view->head,
        view->tail,
        view->size,
        steps,
        norm_steps
    );

    // update head/tail pointers
    view->head = bounds.first;
    view->tail = bounds.second;
}


/* Rotate a singly-linked set to the right by the specified number of steps. */
template <typename NodeType>
void rotate_single(SetView<NodeType>* view, ssize_t steps) {
    // normalize steps
    size_t norm_steps = abs(steps) % view->size;
    if (norm_steps == 0) {  // rotated list is identical to original
        return;
    }

    // rotate the list
    std::pair<Hashed<NodeType>*, Hashed<NodeType>*> bounds = _rotate_single(
        view->head,
        view->tail,
        view->size,
        steps,
        norm_steps
    );

    // update head/tail pointers
    view->head = bounds.first;
    view->tail = bounds.second;
}


/* Rotate a singly-linked dictionary to the right by the specified number of steps. */
template <typename NodeType>
void rotate_single(DictView<NodeType>* view, ssize_t steps) {
    // normalize steps
    size_t norm_steps = abs(steps) % view->size;
    if (norm_steps == 0) {  // rotated list is identical to original
        return;
    }

    // rotate the list
    std::pair<Mapped<NodeType>*, Mapped<NodeType>*> bounds = _rotate_single(
        view->head,
        view->tail,
        view->size,
        steps,
        norm_steps
    );

    // update head/tail pointers
    view->head = bounds.first;
    view->tail = bounds.second;
}


// NOTE: doubly-linked lists use the same algorithm as the singly-linked
// variants, but are slightly faster because we can iterate from either end of
// the list.


/* Rotate a doubly-linked list to the right by the specified number of steps. */
template <typename NodeType>
void rotate_double(ListView<NodeType>* view, ssize_t steps) {
    // normalize steps
    size_t norm_steps = abs(steps) % view->size;
    if (norm_steps == 0) {  // rotated list is identical to original
        return;
    }

    // rotate the list
    std::pair<NodeType*, NodeType*> bounds = _rotate_double(
        view->head,
        view->tail,
        view->size,
        steps,
        norm_steps
    );

    // update head/tail pointers
    view->head = bounds.first;
    view->tail = bounds.second;
}


/* Rotate a doubly-linked set to the right by the specified number of steps. */
template <typename NodeType>
void rotate_double(SetView<NodeType>* view, ssize_t steps) {
    // normalize steps
    size_t norm_steps = abs(steps) % view->size;
    if (norm_steps == 0) {  // rotated list is identical to original
        return;
    }

    // rotate the list
    std::pair<Hashed<NodeType>*, Hashed<NodeType>*> bounds = _rotate_double(
        view->head,
        view->tail,
        view->size,
        steps,
        norm_steps
    );

    // update head/tail pointers
    view->head = bounds.first;
    view->tail = bounds.second;
}


/* Rotate a doubly-linked dictionary to the right by the specified number of steps. */
template <typename NodeType>
void rotate_double(DictView<NodeType>* view, ssize_t steps) {
    // normalize steps
    size_t norm_steps = abs(steps) % view->size;
    if (norm_steps == 0) {  // rotated list is identical to original
        return;
    }

    // rotate the list
    std::pair<Mapped<NodeType>*, Mapped<NodeType>*> bounds = _rotate_double(
        view->head,
        view->tail,
        view->size,
        steps,
        norm_steps
    );

    // update head/tail pointers
    view->head = bounds.first;
    view->tail = bounds.second;
}


///////////////////////
////    PRIVATE    ////
///////////////////////


template <typename NodeType>
std::pair<NodeType*, NodeType*> _rotate_single(
    NodeType* head,
    NodeType* tail,
    size_t size,
    ssize_t steps,
    size_t norm_steps
) {
    // identify index at which to split the list
    size_t split_index;
    if (steps < 0) {
        split_index = norm_steps;
    } else {
        split_index = size - norm_steps;
    }

    // identify new head and tail of rotated list
    NodeType* staged_tail = head;
    for (size_t i = 0; i < split_index; i++) {
        staged_tail = (NodeType*)staged_tail->next;
    }
    NodeType* staged_head = (NodeType*)staged_tail->next;

    // break list at junction
    NodeType::split(staged_tail, staged_head);

    // link sublists based on direction of rotation
    if (steps < 0) {  // rotate left
        NodeType::join(tail, staged_head);
    } else {  // rotate right
        NodeType::join(staged_tail, head);
    }

    // return references to new head and tail
    return std::make_pair(staged_head, staged_tail);
}


template <typename NodeType>
std::pair<NodeType*, NodeType*> _rotate_double(
    NodeType* head,
    NodeType* tail,
    size_t size,
    ssize_t steps,
    size_t norm_steps
) {
    // identify index at which to split the list
    size_t split_index;
    if (steps < 0) {
        split_index = norm_steps;
    } else {
        split_index = size - norm_steps;
    }

    // identify new head and tail of rotated list
    NodeType* staged_head;
    NodeType* staged_tail;
    if (split_index <= size / 2) {  // forward traversal
        staged_tail = head;
        for (size_t i = 0; i < split_index; i++) {
            staged_tail = (NodeType*)staged_tail->next;
        }
        staged_head = (NodeType*)staged_tail->next;
    } else {  // backward traversal
        staged_head = tail;
        for (size_t i = size - 1; i > split_index; i--) {
            staged_head = (NodeType*)staged_head->prev;
        }
        staged_tail = (NodeType*)staged_head->prev;
    }

    // break list at junction
    NodeType::split(staged_tail, staged_head);

    // link staged nodes to head/tail based on direction of rotation
    if (steps < 0) {  // rotate left
        NodeType::join(tail, staged_head);
    } else {  // rotate right
        NodeType::join(staged_tail, head);
    }

    // return references to new head and tail
    return std::make_pair(staged_head, staged_tail);
}


#endif // ROTATE_H include guard
