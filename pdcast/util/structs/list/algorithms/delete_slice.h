// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_DELETE_SLICE_H
#define BERTRAND_STRUCTS_ALGORITHMS_DELETE_SLICE_H

#include <cstddef>  // size_t
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/bounds.h"  // normalize_slice()
#include "../core/node.h"  // is_doubly_linked<>
#include "../core/view.h"  // views


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    /* Delete the value at a particular index of a linked list, set, or dictionary. */
    template <typename View, typename T>
    void delete_index(View* view, T index) {
        using Node = typename View::Node;

        // allow python-style negative indexing + boundschecking
        size_t idx = normalize_index(index, view->size, false);
        if (idx == MAX_SIZE_T && PyErr_Occurred()) {
            return;  // propagate error
        }

        // get neighbors at index
        std::tuple<Node*, Node*, Node*> bounds = neighbors(view, view->head, idx);
        Node* prev = std::get<0>(bounds);
        Node* curr = std::get<1>(bounds);
        Node* next = std::get<2>(bounds);

        // unlink and deallocate node
        view->unlink(prev, curr, next);
        view->recycle(curr);
    }

    /* Delete a slice from a linked list, set, or dictionary. */
    template <typename View>
    void delete_slice(
        View* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step
    ) {
        using Node = typename View::Node;

        // get direction in which to traverse slice that minimizes iterations
        std::pair<size_t, size_t> bounds = normalize_slice(view, start, stop, step);
        size_t begin = bounds.first;
        size_t end = bounds.second;
        if (begin == MAX_SIZE_T && end == MAX_SIZE_T && PyErr_Occurred()) {
            PyErr_Clear();  // swallow error
            return;  // Python does nothing in this case
        }

        // get number of nodes in slice
        size_t abs_step = static_cast<size_t>(llabs(step));
        size_t length = slice_length(begin, end, abs_step);

        // NOTE: if the slice is closer to the end of a doubly-linked list, we can
        // iterate from the tail to save time.
        if constexpr (is_doubly_linked<Node>::value) {
            if (begin > view->size / 2) {  // backward traversal
                _drop_slice_backward(view, begin, length, abs_step);
                return;
            }
        }

        // forward traversal
        _drop_slice_forward(view, begin, length, abs_step);
    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Remove a slice from left to right. */
template <typename View>
void _drop_slice_forward(
    View* view,
    size_t begin,
    size_t slice_length,
    size_t abs_step
) {
    using Node = typename View::Node;

    // get first node in slice (+ its predecessor) by iterating from head
    Node* prev = nullptr;
    Node* curr = view->head;
    for (size_t i = 0; i < begin; i++) {
        prev = curr;
        curr = static_cast<Node*>(curr->next);
    }

    // delete nodes from view
    Node* next;
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    for (size_t i = 0; i < slice_length; i++) {
        // unlink and deallocate node
        next = static_cast<Node*>(curr->next);
        view->unlink(prev, curr, next);
        view->recycle(curr);
        curr = next;

        // advance according to step size
        if (i < slice_length - 1) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = static_cast<Node*>(curr->next);
            }
        }
    }
}


/* Remove a slice from right to left. */
template <typename View>
void _drop_slice_backward(
    View* view,
    size_t begin,
    size_t slice_length,
    size_t abs_step
) {
    using Node = typename View::Node;

    // get first node in slice (+ its successor) by iterating from tail
    Node* next = nullptr;
    Node* curr = view->tail;
    for (size_t i = view->size - 1; i > begin; i--) {
        next = curr;
        curr = static_cast<Node*>(curr->prev);
    }

    // delete nodes from view
    Node* prev;
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    for (size_t i = 0; i < slice_length; i++) {
        // unlink and deallocate node
        prev = static_cast<Node*>(curr->prev);
        view->unlink(prev, curr, next);
        view->recycle(curr);
        curr = prev;

        // advance according to step size
        if (i < slice_length - 1) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                next = curr;
                curr = static_cast<Node*>(curr->prev);
            }
        }
    }
}


#endif // BERTRAND_STRUCTS_ALGORITHMS_DELETE_SLICE_H include guard
