
// include guard prevents multiple inclusion
#ifndef GET_SLICE_H
#define GET_SLICE_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <utility>  // for std::pair
#include "node.h"  // for nodes
#include "view.h"  // for views, MAX_SIZE_T


// NOTE: Due to the nature of linked lists, indexing in general and slicing in
// particular can be complicated and inefficient.  This implementation attempts
// to minimize these downsides as much as possible by taking advantage of
// doubly-linked lists where possible and avoiding backtracking.


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Get the value at a particular index of a singly-linked list. */
template <template <typename> class ViewType, typename NodeType>
inline PyObject* get_index(ViewType<NodeType>* view, size_t index) {
    using Node = typename ViewType<NodeType>::Node;
    Node* curr;

    // NOTE: if the index is closer to tail and the list is doubly-linked, we
    // can iterate from the tail to save time.
    if constexpr (is_doubly_linked<Node>::value) {
        if (index > view->size / 2) {
            // backward traversal
            curr = view->tail;
            for (size_t i = view->size - 1; i > index; i--) {
                curr = static_cast<Node*>(curr->prev);
            }
            Py_INCREF(curr->value);  // caller takes ownership of reference
            return curr->value;
        }
    }

    // forward traversal
    curr = view->head;
    for (size_t i = 0; i < index; i++) {
        curr = static_cast<Node*>(curr->next);
    }
    Py_INCREF(curr->value);  // caller takes ownership of reference
    return curr->value;
}


/* Extract a slice from a singly-linked list. */
template <template <typename> class ViewType, typename NodeType>
inline ViewType<NodeType>* get_slice(
    ViewType<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
) {
    using Node = typename ViewType<NodeType>::Node;
    size_t abs_step = (size_t)abs(step);

    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds = normalize_slice(view, start, stop, step);
    if (PyErr_Occurred()) {
        return new ViewType<NodeType>();  // Python returns an empty list here
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


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Extract a slice from left to right. */
template <template <typename> class ViewType, typename NodeType>
ViewType<NodeType>* _extract_slice_forward(
    ViewType<NodeType>* view,
    size_t begin,
    size_t end,
    size_t abs_step,
    bool reverse
) {
    using Node = typename ViewType<NodeType>::Node;

    // create a new view to hold the slice
    ViewType<NodeType>* slice;
    try {
        slice = new ViewType<NodeType>();
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
template <template <typename> class ViewType, typename NodeType>
ViewType<NodeType>* _extract_slice_backward(
    ViewType<NodeType>* view,
    size_t begin,
    size_t end,
    size_t abs_step,
    bool reverse
) {
    using Node = typename ViewType<NodeType>::Node;

    // create a new view to hold the slice
    ViewType<NodeType>* slice;
    try {
        slice = new ViewType<NodeType>();
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


////////////////////////
////    WRAPPERS    ////
////////////////////////


// NOTE: Cython doesn't play well with nested templates, so we need to
// explicitly instantiate specializations for each combination of node/view
// type.  This is a bit of a pain, put it's the only way to get Cython to
// properly recognize the functions.

// Maybe in a future release we won't have to do this:


template PyObject* get_index(ListView<SingleNode>* view, size_t index);
template PyObject* get_index(SetView<SingleNode>* view, size_t index);
template PyObject* get_index(DictView<SingleNode>* view, size_t index);
template PyObject* get_index(ListView<DoubleNode>* view, size_t index);
template PyObject* get_index(SetView<DoubleNode>* view, size_t index);
template PyObject* get_index(DictView<DoubleNode>* view, size_t index);
template ListView<SingleNode>* get_slice(
    ListView<SingleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);
template SetView<SingleNode>* get_slice(
    SetView<SingleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);
template DictView<SingleNode>* get_slice(
    DictView<SingleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);
template ListView<DoubleNode>* get_slice(
    ListView<DoubleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);
template SetView<DoubleNode>* get_slice(
    SetView<DoubleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);
template DictView<DoubleNode>* get_slice(
    DictView<DoubleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);


#endif // GET_SLICE_H include guard
