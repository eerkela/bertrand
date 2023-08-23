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
        size_t abs_step = static_cast<size_t>(llabs(step));

        // get direction in which to traverse slice that minimizes iterations
        std::pair<size_t, size_t> bounds = normalize_slice(view, start, stop, step);
        if (
            bounds.first == MAX_SIZE_T &&
            bounds.second == MAX_SIZE_T &&
            PyErr_Occurred()
        ) {
            PyErr_Clear();  // Python does nothing in this case
            return;
        }

        // get number of nodes in slice
        size_t slice_length = llabs((long long)bounds.second - (long long)bounds.first);
        slice_length = (slice_length / abs_step) + 1;

        // NOTE: if the list is doubly-linked, then we can traverse from either
        // end.  As such, we choose whichever is closest to a slice boundary in
        // order to minimize total iterations.
        if constexpr (is_doubly_linked<Node>::value) {
            if (bounds.first > view->size / 2) {  // backward traversal
                _drop_slice_backward(view, bounds.first, slice_length, abs_step);
                return;
            }
        }

        // forward traversal
        _drop_slice_forward(view, bounds.first, slice_length, abs_step);
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
    size_t last_iter = slice_length - 1;
    for (size_t i = 0; i < slice_length; i++) {
        // unlink and deallocate node
        next = static_cast<Node*>(curr->next);
        view->unlink(prev, curr, next);
        view->recycle(curr);
        curr = next;

        // advance according to step size
        if (i < last_iter) {  // don't jump on final iteration
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
    size_t last_iter = slice_length - 1;
    for (size_t i = 0; i < slice_length; i++) {
        // unlink and deallocate node
        prev = static_cast<Node*>(curr->prev);
        view->unlink(prev, curr, next);
        view->recycle(curr);
        curr = prev;

        // advance according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                next = curr;
                curr = static_cast<Node*>(curr->prev);
            }
        }
    }
}


#endif // BERTRAND_STRUCTS_ALGORITHMS_DELETE_SLICE_H include guard
