// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_GET_SLICE_H
#define BERTRAND_STRUCTS_ALGORITHMS_GET_SLICE_H

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

    /* Get the value at a particular index of a linked list, set, or dictionary. */
    template <typename View, typename T>
    PyObject* get_index(View* view, T index) {
        using Node = typename View::Node;

        // allow python-style negative indexing + boundschecking
        size_t norm_index = normalize_index(index, view->size, false);
        if (norm_index == MAX_SIZE_T && PyErr_Occurred()) {
            return nullptr;  // propagate error
        }

        // get node at index
        Node* curr = node_at_index(view, view->head, norm_index);

        // return a new reference to the node's value
        Py_INCREF(curr->value);
        return curr->value;
    }

    /* Extract a slice from a linked list, set, or dictionary. */
    template <typename View>
    inline View* get_slice(
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
            return new View();  // Python returns an empty list in this case
        }

        // get number of nodes in slice
        size_t slice_length = llabs((long long)bounds.second - (long long)bounds.first);
        slice_length = (slice_length / abs_step) + 1;

        // create a new view to hold the slice
        View* slice;
        try {
            slice = new View();
        } catch (const std::bad_alloc&) {
            PyErr_NoMemory();
            return nullptr;
        }

        // NOTE: if the slice is closer to the end of a doubly-linked list, we can
        // iterate from the tail to save time.
        if constexpr (is_doubly_linked<Node>::value) {
            if (bounds.first > bounds.second) {
                // backward traversal
                return _extract_slice_backward(
                    view,
                    slice,
                    bounds.first,
                    slice_length,
                    abs_step,
                    (step > 0)
                );
            }
        }

        // forward traversal
        return _extract_slice_forward(
            view,
            slice,
            bounds.first,
            slice_length,
            abs_step,
            (step < 0)
        );
    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Extract a slice from left to right. */
template <typename View>
View* _extract_slice_forward(
    View* view,
    View* slice,
    size_t begin,
    size_t slice_length,
    size_t abs_step,
    bool reverse
) {
    using Node = typename View::Node;

    // get first node in slice by iterating from head
    Node* curr = view->head;
    for (size_t i = 0; i < begin; i++) {
        curr = static_cast<Node*>(curr->next);
    }

    // copy nodes from original view
    size_t last_iter = slice_length - 1;
    for (size_t i = 0; i < slice_length; i++) {
        Node* copy = slice->copy(curr);
        if (copy == nullptr) {  // error during copy()
            delete slice;  // clean up staged slice
            return nullptr;  // propagate error
        }

        // link to slice
        if (reverse) {  // reverse slice as we add nodes
            slice->link(nullptr, copy, slice->head);
        } else {
            slice->link(slice->tail, copy, nullptr);
        }
        if (PyErr_Occurred()) {
            slice->recycle(copy);  // clean up staged node
            delete slice;
            return nullptr;  // propagate error
        }

        // advance according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                curr = static_cast<Node*>(curr->next);
            }
        }
    }

    return slice;
}


/* Extract a slice from right to left. */
template <typename View>
View* _extract_slice_backward(
    View* view,
    View* slice,
    size_t begin,
    size_t slice_length,
    size_t abs_step,
    bool reverse
) {
    using Node = typename View::Node;

    // get first node in slice by iterating from tail
    Node* curr = view->tail;
    for (size_t i = view->size - 1; i > begin; i--) {
        curr = static_cast<Node*>(curr->prev);
    }

    // copy nodes from original view
    size_t last_iter = slice_length - 1;
    for (size_t i = 0; i < slice_length; i++) {
        Node* copy = slice->copy(curr);
        if (copy == nullptr) {  // error during copy()
            delete slice;  // clean up staged slice
            return nullptr;  // propagate error
        }

        // link to slice
        if (reverse) {  // reverse slice as we add nodes
            slice->link(nullptr, copy, slice->head);
        } else {
            slice->link(slice->tail, copy, nullptr);
        }
        if (PyErr_Occurred()) {
            slice->recycle(copy);  // clean up staged node
            delete slice;
            return nullptr;  // propagate error
        }

        // advance according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                curr = static_cast<Node*>(curr->prev);
            }
        }
    }

    return slice;
}


#endif // BERTRAND_STRUCTS_ALGORITHMS_GET_SLICE_H include guard
