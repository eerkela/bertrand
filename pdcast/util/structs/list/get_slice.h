
// include guard prevents multiple inclusion
#ifndef GET_SLICE_H
#define GET_SLICE_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <utility>  // for std::pair
#include <node.h>  // for nodes
#include <view.h>  // for views
#include <index.h>  // for MAX_SIZE_T


// NOTE: Due to the nature of linked lists, indexing in general and slicing in
// particular can be complicated and inefficient.  This implementation attempts
// to minimize these downsides as much as possible by taking advantage of
// doubly-linked lists where possible and avoiding backtracking.


//////////////////////
////    SHARED    ////
//////////////////////


// NOTE: We may not always be able to efficiently iterate through a linked list
// in reverse.  This means that we can't always guarantee that we iterate over
// a slice in the same direction as the step size would indicate.  For instance,
// if we have a singly-linked list and we want to iterate over a slice with a
// negative step size, we'll have to start from the head and iterate over it
// backwards.  We can compensate for this by reversing the slice again as we
// extract each node, which counteracts the previous effect and produces the
// intended result.

// In the case of doubly-linked lists, we can use this trick to minimize total
// iterations and avoid backtracking.  Since we're free to start from either
// end of the list, we always choose the one that is closer to a slice
// boundary, and then just reflect the results to match the intended output.

// NOTE: this changes the way we have to approach our slice indices.  Python
// slices are normally asymmetric and half-open at the stop index, but this is
// a problem for us.  Because we might be iterating from the stop index to the
// start index rather than the other way around, we need to be able to treat
// the slice symmetrically in both directions.  To do this, we convert it into
// a closed interval by rounding the stop index to the nearest included step.


/* A modulo operator (%) that matches Python with respect to negative numbers. */
template <typename T>
inline T py_modulo(T a, T b) {
    // NOTE: Python's `%` operator is defined such that the result has the same
    // sign as the divisor (b).  This differs from C, where the result has the
    // same sign as the dividend (a).  This function uses the Python version.
    return (a % b + b) % b;
}


/* Adjust the stop index in a slice to make it closed on both ends. */
template <typename T>
inline T make_closed(T start, T stop, T step) {
    T remainder = py_modulo((stop - start), step);  // sign matches step
    if (remainder == 0) {
        return stop - step; // decrement stop by 1 full step
    }
    return stop - remainder;  // decrement stop to nearest multiple of step
}


/* Check whether a slice represents a valid range consistent with its step size. */
template <typename T>
inline int is_valid(T start, T stop, T step) {
    return (step > 0 && start <= stop) || (step < 0 && start >= stop);
}


/* Get the direction in which to traverse a singly-linked slice that minimizes
total iterations and avoids backtracking. */
inline std::pair<size_t, size_t> _get_slice_direction_single(
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    size_t size
) {
    // convert to closed interval
    stop = make_closed(start, stop, step);

    // check if slice is not a valid range
    if (!is_valid(start, stop, step)) {
        PyErr_SetString(PyExc_ValueError, "invalid slice");
        return std::make_pair(MAX_SIZE_T, MAX_SIZE_T);
    }

    // determine direction of traversal for singly-linked list
    size_t norm_start = (size_t)start;
    size_t norm_stop = (size_t)stop;
    if (step > 0) {
        return std::make_pair(norm_start, norm_stop);  // iterate normally
    }
    return std::make_pair(norm_stop, norm_start);  // reverse slice
}


/* Get the direction in which to traverse a doubly-linked slice that minimizes
total iterations and avoids backtracking. */
inline std::pair<size_t, size_t> _get_slice_direction_double(
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    size_t size
) {
    // convert to closed interval
    stop = make_closed(start, stop, step);

    // check if slice is not a valid range
    if (!is_valid(start, stop, step)) {
        PyErr_SetString(PyExc_ValueError, "invalid slice");
        return std::make_pair(MAX_SIZE_T, MAX_SIZE_T);
    }

    // determine direction of traversal for doubly-linked list
    size_t norm_start = (size_t)start;
    size_t norm_stop = (size_t)stop;
    if (
        (step > 0 && norm_start <= size - norm_stop) ||  // start closer to head
        (step < 0 && size - norm_start <= norm_stop)     // start closer to tail
    ) {
        return std::make_pair(norm_start, norm_stop);  // iterate normally
    }
    return std::make_pair(norm_stop, norm_start);  // reverse slice
}


/////////////////////////
////    GET INDEX    ////
/////////////////////////


/* Get the value at a particular index of a singly-linked list. */
template <template <typename> class ViewType, typename NodeType>
inline PyObject* get_index_single(ViewType<NodeType>* view, size_t index) {
    using Node = typename ViewType<NodeType>::Node;

    // forward traversal
    Node* curr = view->head;
    for (size_t i = 0; i < index; i++) {
        curr = (Node*)curr->next;
    }
    Py_INCREF(curr->value);  // caller takes ownership of reference
    return curr->value;
}


/* Get the value at a particular index of a doubly-linked list. */
template <template <typename> class ViewType, typename NodeType>
inline PyObject* get_index_double(ViewType<NodeType>* view, size_t index) {
    // if index is closer to head, use singly-linked version
    if (index <= view->size / 2) {
        return get_index_single(view, index);
    }

    using Node = typename ViewType<NodeType>::Node;

    // backward traversal
    Node* curr = view->tail;
    for (size_t i = view->size - 1; i > index; i--) {
        curr = (Node*)curr->prev;
    }
    Py_INCREF(curr->value);  // caller takes ownership of reference
    return curr->value;
}


/////////////////////////
////    GET SLICE    ////
/////////////////////////


/* Extract a slice from a singly-linked list. */
template <template <typename> class ViewType, typename NodeType>
inline ViewType<NodeType>* get_slice_single(
    ViewType<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
) {
    size_t abs_step = (size_t)abs(step);
    std::pair<size_t, size_t> bounds;

    // determine direction of traversal to avoid backtracking
    bounds = _get_slice_direction_single(start, stop, step, view->size);
    if (PyErr_Occurred()) {
        return new ViewType<NodeType>();  // Python returns an empty list here
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


/* Extract a slice from a doubly-linked list. */
template <template <typename> class ViewType, typename NodeType>
inline ViewType<NodeType>* get_slice_double(
    ViewType<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
) {
    size_t abs_step = (size_t)abs(step);
    std::pair<size_t, size_t> bounds;

    // determine direction of traversal to avoid backtracking
    bounds = _get_slice_direction_double(start, stop, step, view->size);
    if (PyErr_Occurred()) {
        return new ViewType<NodeType>();  // Python returns an empty list here
    }

    // extract slice
    if (bounds.first <= bounds.second) {  // forward traversal
        return _extract_slice_forward(
            view,
            bounds.first,
            bounds.second,
            abs_step,
            (step < 0)
        );
    } else {  // backward traversal
        return _extract_slice_backward(
            view,
            bounds.first,
            bounds.second,
            abs_step,
            (step > 0)
        );
    }
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
        return NULL;
    }

    // get first node in slice by iterating from head
    Node* curr = view->head;
    for (size_t i = 0; i < begin; i++) {
        curr = (Node*)curr->next;
    }

    // copy nodes from original view
    for (size_t i = begin; i <= end; i += abs_step) {
        Node* copy = slice->copy(curr);
        if (copy == NULL) {  // error during copy()
            delete slice;
            return NULL;
        }

        // link to slice
        if (reverse) {  // reverse slice as we add nodes
            slice->link(slice->tail, copy, NULL);
        } else {
            slice->link(NULL, copy, slice->head);
        }
        if (PyErr_Occurred()) {  // error during resize()
            slice->recycle(copy);
            delete slice;
            return NULL;
        }

        // advance node according to step size
        if (i != end) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                curr = (Node*)curr->next;
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
        return NULL;
    }

    // get first node in slice by iterating from tail
    Node* curr = view->tail;
    for (size_t i = view->size - 1; i > begin; i--) {
        curr = (Node*)curr->prev;
    }

    // copy nodes from original view
    for (size_t i = begin; i >= end; i -= abs_step) {
        Node* copy = slice->copy(curr);
        if (copy == NULL) {  // error during copy()
            delete slice;
            return NULL;
        }

        // link to slice
        if (reverse) {  // reverse slice as we add nodes
            slice->link(slice->tail, copy, NULL);
        } else {
            slice->link(NULL, copy, slice->head);
        }
        if (PyErr_Occurred()) {  // error during resize()
            slice->recycle(copy);
            delete slice;
            return NULL;
        }

        // advance node according to step size
        if (i != end) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                curr = (Node*)curr->prev;
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


template PyObject* get_index_single(ListView<SingleNode>* view, size_t index);
template PyObject* get_index_single(SetView<SingleNode>* view, size_t index);
template PyObject* get_index_single(DictView<SingleNode>* view, size_t index);
template PyObject* get_index_double(ListView<DoubleNode>* view, size_t index);
template PyObject* get_index_double(SetView<DoubleNode>* view, size_t index);
template PyObject* get_index_double(DictView<DoubleNode>* view, size_t index);
template ListView<SingleNode>* get_slice_single(
    ListView<SingleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);
template SetView<SingleNode>* get_slice_single(
    SetView<SingleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);
template DictView<SingleNode>* get_slice_single(
    DictView<SingleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);
template ListView<DoubleNode>* get_slice_double(
    ListView<DoubleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);
template SetView<DoubleNode>* get_slice_double(
    SetView<DoubleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);
template DictView<DoubleNode>* get_slice_double(
    DictView<DoubleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
);


#endif // GET_SLICE_H include guard
