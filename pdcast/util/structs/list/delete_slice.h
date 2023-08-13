
// include guard prevents multiple inclusion
#ifndef DELETE_SLICE_H
#define DELETE_SLICE_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <utility>  // for std::pair
#include "node.h"  // for nodes
#include "view.h"  // for views


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Delete a node at a particular index of a singly-linked list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline void delete_index(ViewType<NodeType>* view, size_t index) {
    using Node = typename ViewType<NodeType>::Node;
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


/* Delete a slice within a linked list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline void delete_slice(
    ViewType<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
) {
    size_t abs_step = (size_t)abs(step);

    // get direction in which to traverse slice that minimizes iterations
    std::pair<size_t, size_t> bounds = normalize_slice(view, start, stop, step);
    if (PyErr_Occurred()) {
            return;  // Python does nothing here
        }

    if constexpr (is_doubly_linked<NodeType>::value) {
        // NOTE: if the list is doubly-linked, then we can traverse from either
        // end.  As such, we choose whichever end is closest to a slice
        // boundary, which minimizes iterations.
        if (bounds.first > bounds.second) {
            _drop_slice_backward(view, bounds.first, bounds.second, abs_step);
            return;
        }
    }

    // forward traversal
    _drop_slice_forward(view, bounds.first, bounds.second, abs_step);
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Remove a slice from left to right. */
template <template <typename> class ViewType, typename NodeType>
void _drop_slice_forward(
    ViewType<NodeType>* view,
    size_t begin,
    size_t end,
    size_t abs_step
) {
    using Node = typename ViewType<NodeType>::Node;

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
    for (size_t i = begin; i <= end; i += abs_step) {
        // unlink and deallocate node
        next = static_cast<Node*>(curr->next);
        view->unlink(prev, curr, next);
        view->recycle(curr);
        curr = next;

        // advance node according to step size
        if (i != end) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = static_cast<Node*>(curr->next);
            }
        }
    }
}


/* Remove a slice from right to left. */
template <template <typename> class ViewType, typename NodeType>
void _drop_slice_backward(
    ViewType<NodeType>* view,
    size_t begin,
    size_t end,
    size_t abs_step
) {
    using Node = typename ViewType<NodeType>::Node;

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
    for (size_t i = begin; i >= end; i -= abs_step) {
        // unlink and deallocate node
        prev = static_cast<Node*>(curr->prev);
        view->unlink(prev, curr, next);
        view->recycle(curr);
        curr = prev;

        // advance node according to step size
        if (i != end) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                next = curr;
                curr = static_cast<Node*>(curr->prev);
            }
        }
    }
}


///////////////////////
////    ALIASES    ////
///////////////////////


// NOTE: Cython doesn't play well with heavily templated functions, so we need
// to explicitly instantiate the specializations we need.  Maybe in a future
// release we won't have to do this:


template void delete_index(ListView<SingleNode>* view, size_t index);
template void delete_index(SetView<SingleNode>* view, size_t index);
template void delete_index(DictView<SingleNode>* view, size_t index);
template void delete_index(ListView<DoubleNode>* view, size_t index);
template void delete_index(SetView<DoubleNode>* view, size_t index);
template void delete_index(DictView<DoubleNode>* view, size_t index);
template void delete_slice(
    ListView<SingleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);
template void delete_slice(
    SetView<SingleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);
template void delete_slice(
    DictView<SingleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);
template void delete_slice(
    ListView<DoubleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);
template void delete_slice(
    SetView<DoubleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);
template void delete_slice(
    DictView<DoubleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);


#endif // DELETE_SLICE_H include guard
