// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_GET_SLICE_H
#define BERTRAND_STRUCTS_ALGORITHMS_GET_SLICE_H

#include <cstddef>  // size_t
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/bounds.h"  // normalize_slice()
#include "../core/node.h"  // is_doubly_linked<>
#include "../core/view.h"  // views


// NOTE: Due to the nature of linked lists, indexing in general and slicing in
// particular can be complicated and inefficient.  This implementation attempts
// to minimize these downsides as much as possible by taking advantage of
// doubly-linked lists where possible and avoiding backtracking.


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    /* Get the value at a particular index of a linked list, set, or dictionary. */
    template <typename View, typename T>
    PyObject* get_index(View* view, T index) {
        using Node = typename View::Node;
        Node* curr;

        // allow python-style negative indexing + boundschecking
        size_t norm_index = normalize_index(index, view->size, false);

        // NOTE: if the index is closer to tail and the list is doubly-linked, we
        // can iterate from the tail to save time.
        if constexpr (is_doubly_linked<Node>::value) {
            if (norm_index > view->size / 2) {
                // backward traversal
                curr = view->tail;
                for (size_t i = view->size - 1; i > norm_index; i--) {
                    curr = static_cast<Node*>(curr->prev);
                }
                Py_INCREF(curr->value);  // caller takes ownership of reference
                return curr->value;
            }
        }

        // forward traversal
        curr = view->head;
        for (size_t i = 0; i < norm_index; i++) {
            curr = static_cast<Node*>(curr->next);
        }
        Py_INCREF(curr->value);  // caller takes ownership of reference
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

        // determine direction of traversal to avoid backtracking
        std::pair<size_t, size_t> bounds = normalize_slice(view, start, stop, step);
        if (PyErr_Occurred()) {  // Python returns an empty list here
            return new View();
        }

        // NOTE: if the slice is closer to the end of a doubly-linked list, we can
        // iterate from the tail to save time.
        if constexpr (is_doubly_linked<Node>::value) {
            if (bounds.first > bounds.second) {
                // backward traversal
                return _extract_slice_backward(
                    view,
                    bounds.first,
                    bounds.second,
                    abs_step,
                    (step > 0)
                );
            }
        }

        // forward traversal
        return _extract_slice_forward(
            view,
            bounds.first,
            bounds.second,
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
    size_t begin,
    size_t end,
    size_t abs_step,
    bool reverse
) {
    using Node = typename View::Node;

    // create a new view to hold the slice
    View* slice;
    try {
        slice = new View();
    } catch (const std::bad_alloc&) {  // MemoryError()
        PyErr_NoMemory();
        return nullptr;
    }

    // get first node in slice by iterating from head
    Node* curr = view->head;
    for (size_t i = 0; i < begin; i++) {
        curr = static_cast<Node*>(curr->next);
    }

    // copy nodes from original view
    for (size_t i = begin; i <= end; i += abs_step) {
        Node* copy = slice->copy(curr);
        if (copy == nullptr) {  // error during copy()
            delete slice;
            return nullptr;
        }

        // link to slice
        if (reverse) {  // reverse slice as we add nodes
            slice->link(nullptr, copy, slice->head);
        } else {
            slice->link(slice->tail, copy, nullptr);
        }
        if (PyErr_Occurred()) {  // error during resize()
            slice->recycle(copy);
            delete slice;
            return nullptr;
        }

        // advance node according to step size
        if (i != end) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                curr = static_cast<Node*>(curr->next);
            }
        }
    }

    // caller takes ownership of slice
    return slice;
}


/* Extract a slice from right to left. */
template <typename View>
View* _extract_slice_backward(
    View* view,
    size_t begin,
    size_t end,
    size_t abs_step,
    bool reverse
) {
    using Node = typename View::Node;

    // create a new view to hold the slice
    View* slice;
    try {
        slice = new View();  // TODO: cannot default-construct
    } catch (const std::bad_alloc&) {  // MemoryError()
        PyErr_NoMemory();
        return nullptr;
    }

    // get first node in slice by iterating from tail
    Node* curr = view->tail;
    for (size_t i = view->size - 1; i > begin; i--) {
        curr = static_cast<Node*>(curr->prev);
    }

    // copy nodes from original view
    for (size_t i = begin; i >= end; i -= abs_step) {
        Node* copy = slice->copy(curr);
        if (copy == nullptr) {  // error during copy()
            delete slice;
            return nullptr;
        }

        // link to slice
        if (reverse) {  // reverse slice as we add nodes
            slice->link(nullptr, copy, slice->head);
        } else {
            slice->link(slice->tail, copy, nullptr);
        }
        if (PyErr_Occurred()) {  // error during resize()
            slice->recycle(copy);
            delete slice;
            return nullptr;
        }

        // advance node according to step size
        if (i != end) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                curr = static_cast<Node*>(curr->prev);
            }
        }
    }

    // caller takes ownership of slice
    return slice;
}


#endif // BERTRAND_STRUCTS_ALGORITHMS_GET_SLICE_H include guard
