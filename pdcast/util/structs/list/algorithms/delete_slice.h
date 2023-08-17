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

    /* Delete a single item from a linked list, set, or dictionary. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    void delete_index(ViewType<NodeType, Allocator>* view, size_t index) {
        using Node = typename ViewType<NodeType, Allocator>::Node;
        Node* prev;
        Node* curr;
        Node* next;

        // NOTE: if the index is closer to tail and the list is doubly-linked, we
        // can iterate from the tail to save time.
        if constexpr (is_doubly_linked<Node>::value) {
            if (index > view->size / 2) {
                // backward traversal
                next = nullptr;
                curr = view->tail;
                for (size_t i = view->size - 1; i > index; i--) {
                    next = curr;
                    curr = static_cast<Node*>(curr->prev);
                }

                // unlink and deallocate node
                view->unlink(static_cast<Node*>(curr->prev), curr, next);
                view->recycle(curr);
                return;
            }
        }

        // forward traversal
        prev = nullptr;
        curr = view->head;
        for (size_t i = 0; i < index; i++) {
            prev = curr;
            curr = static_cast<Node*>(curr->next);
        }

        // unlink and deallocate node
        view->unlink(prev, curr, static_cast<Node*>(curr->next));
        view->recycle(curr);
    }

    /* Delete a slice from a linked list, set, or dictionary. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    void delete_slice(
        ViewType<NodeType, Allocator>* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step
    ) {
        using Node = typename ViewType<NodeType, Allocator>::Node;
        size_t abs_step = static_cast<size_t>(llabs(step));

        // get direction in which to traverse slice that minimizes iterations
        std::pair<size_t, size_t> bounds = normalize_slice(view, start, stop, step);
        if (PyErr_Occurred()) {
            PyErr_Clear();  // swallow error
            return;  // Python does nothing in this case
        }

        // get number of nodes in slice
        size_t slice_length = llabs((ssize_t)bounds.second - (ssize_t)bounds.first);
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
template <
    template <typename, template <typename> class> class ViewType,
    typename NodeType,
    template <typename> class Allocator
>
void _drop_slice_forward(
    ViewType<NodeType, Allocator>* view,
    size_t begin,
    size_t slice_length,
    size_t abs_step
) {
    using Node = typename ViewType<NodeType, Allocator>::Node;

    // skip to start index
    Node* prev = nullptr;
    Node* curr = view->head;
    for (size_t i = 0; i < begin; i++) {
        prev = curr;
        curr = static_cast<Node*>(curr->next);
    }

    // delete all nodes in slice
    Node* next;
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    size_t last_iter = slice_length - 1;
    for (size_t i = 0; i < slice_length; i++) {
        // unlink and deallocate node
        next = static_cast<Node*>(curr->next);
        view->unlink(prev, curr, next);
        view->recycle(curr);
        curr = next;

        // advance node according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = static_cast<Node*>(curr->next);
            }
        }
    }
}


/* Remove a slice from right to left. */
template <
    template <typename, template <typename> class> class ViewType,
    typename NodeType,
    template <typename> class Allocator
>
void _drop_slice_backward(
    ViewType<NodeType, Allocator>* view,
    size_t begin,
    size_t slice_length,
    size_t abs_step
) {
    using Node = typename ViewType<NodeType, Allocator>::Node;

    // skip to start index
    Node* next = nullptr;
    Node* curr = view->tail;
    for (size_t i = view->size - 1; i > begin; i--) {
        next = curr;
        curr = static_cast<Node*>(curr->prev);
    }

    // delete all nodes in slice
    Node* prev;
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    size_t last_iter = slice_length - 1;
    for (size_t i = 0; i < slice_length; i++) {
        // unlink and deallocate node
        prev = static_cast<Node*>(curr->prev);
        view->unlink(prev, curr, next);
        view->recycle(curr);
        curr = prev;

        // advance node according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                next = curr;
                curr = static_cast<Node*>(curr->prev);
            }
        }
    }
}


#endif // BERTRAND_STRUCTS_ALGORITHMS_DELETE_SLICE_H include guard
