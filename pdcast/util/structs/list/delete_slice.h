
// include guard prevents multiple inclusion
#ifndef DELETE_SLICE_H
#define DELETE_SLICE_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <utility>  // for std::pair
#include <view.h>  // for views
#include <get_slice.h>  // for _get_slice_direction()


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Delete a slice within a linked list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline void delete_slice_single(
    ViewType<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
) {
    std::pair<size_t, size_t> bounds;
    
    // determine direction of traversal to avoid backtracking
    try {
        bounds = _get_slice_direction_single(start, stop, step, view->size);
    } catch (const std::invalid_argument&) {  // invalid slice
        return;  // Python does nothing here
    }

    // forward traversal
    _drop_slice_forward(
        view,
        view->head,
        bounds.first,
        bounds.second,
        (size_t)abs(step)
    );
}


/* Delete a slice within a linked list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline void delete_slice_double(
    ViewType<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
) {
    std::pair<size_t, size_t> bounds;
    
    // determine direction of traversal to avoid backtracking
    try {
        bounds = _get_slice_direction_double(start, stop, step, view->size);
    } catch (const std::invalid_argument&) {  // invalid slice
        return;  // Python does nothing here
    }

    // forward traversal
    if (bounds.first <= bounds.second) {
        _drop_slice_forward(
            view,
            view->head,
            bounds.first,
            bounds.second,
            (size_t)abs(step)
        );
    } else {  // backward traversal
        _drop_slice_backward(
            view,
            view->tail,
            bounds.first,
            bounds.second,
            (size_t)abs(step)
        );
    }
}


/* Delete a node at a particular index of a singly-linked list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline void delete_index_single(ViewType<NodeType>* view, size_t index) {
    _drop_index_forward(view, view->head, index);
}


/* Delete a node at a particular index of a doubly-linked list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline void delete_index_double(ViewType<NodeType>* view, size_t index) {
    if (index < view->size / 2) {  // forward traversal
        _drop_index_forward(view, view->head, index);
    } else {  // backward traversal
        _drop_index_backward(view, view->tail, index);
    }
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Remove a slice from left to right. */
template <template <typename> class ViewType, typename T, typename U>
inline void _drop_slice_forward(
    ViewType<T>* view,
    U* head,
    size_t begin,
    size_t end,
    size_t abs_step
) {
    // skip to start index
    U* prev = NULL;
    U* curr = head;
    for (size_t i = 0; i < begin; i++) {
        prev = curr;
        curr = (U*)curr->next;
    }

    // delete all nodes in slice
    U* next;
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    for (size_t i = begin; i <= end; i += abs_step) {
        // unlink and deallocate node
        next = (U*)curr->next;
        view->unlink(prev, curr, next);
        view->deallocate(curr);
        curr = next;

        // advance node according to step size
        if (i != end) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = (U*)curr->next;
            }
        }
    }
}


/* Remove a slice from right to left. */
template <template <typename> class ViewType, typename T, typename U>
inline void _drop_slice_backward(
    ViewType<T>* view,
    U* tail,
    size_t begin,
    size_t end,
    size_t abs_step
) {
    // skip to start index
    U* next = NULL;
    U* curr = tail;
    for (size_t i = view->size - 1; i > begin; i--) {
        next = curr;
        curr = (U*)curr->prev;
    }

    // delete all nodes in slice
    U* prev;
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    for (size_t i = begin; i >= end; i -= abs_step) {
        // unlink and deallocate node
        prev = (U*)curr->prev;
        view->unlink(prev, curr, next);
        view->deallocate(curr);
        curr = prev;

        // advance node according to step size
        if (i != end) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                next = curr;
                curr = (U*)curr->prev;
            }
        }
    }
}


/* Remove the node at the given index by iterating forwards from the head. */
template <template <typename> class ViewType, typename T, typename U>
inline void _drop_index_forward(ViewType<T>* view, U* head, size_t index) {
    // skip to start index
    U* prev = NULL;
    U* curr = head;
    for (size_t i = 0; i < index; i++) {
        prev = curr;
        curr = (U*)curr->next;
    }

    // unlink and deallocate node
    view->unlink(prev, curr, (U*)curr->next);
    view->deallocate(curr);
}


/* Remove the node at the given index by iterating backwards from the tail. */
template <template <typename> class ViewType, typename T, typename U>
inline void _drop_index_backward(ViewType<T>* view, U* tail, size_t index) {
    // skip to start index
    U* next = NULL;
    U* curr = tail;
    for (size_t i = view->size - 1; i > index; i--) {
        next = curr;
        curr = (U*)curr->prev;
    }

    // unlink and deallocate node
    view->unlink((U*)curr->prev, curr, next);
    view->deallocate(curr);
}


#endif // DELETE_SLICE_H include guard
