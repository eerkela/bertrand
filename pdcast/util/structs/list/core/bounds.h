// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_BOUNDS_H
#define BERTRAND_STRUCTS_CORE_BOUNDS_H

#include <cstddef>  // size_t
#include <limits>  // std::numeric_limits
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "node.h"  // is_doubly_linked<>


/////////////////////////
////    CONSTANTS    ////
/////////////////////////


/* MAX_SIZE_T is used to signal errors in indexing operations where NULL would
not be a valid return value, and 0 is likely to be valid output. */
const size_t MAX_SIZE_T = std::numeric_limits<size_t>::max();
const std::pair<size_t, size_t> MAX_SIZE_T_PAIR = std::make_pair(
    MAX_SIZE_T, MAX_SIZE_T
);


////////////////////////////////
////    HELPER FUNCTIONS    ////
////////////////////////////////


/* Allow Python-style negative indexing with wraparound and boundschecking. */
template <typename T>
size_t normalize_index(T index, size_t size, bool truncate) {
    bool index_lt_zero = index < 0;

    // wraparound negative indices
    if (index_lt_zero) {
        index += size;
        index_lt_zero = index < 0;
    }

    // boundscheck
    if (index_lt_zero || index >= static_cast<T>(size)) {
        if (truncate) {
            if (index_lt_zero) {
                return 0;
            }
            return size - 1;
        }
        PyErr_SetString(PyExc_IndexError, "list index out of range");
        return MAX_SIZE_T;
    }

    // return as size_t
    return (size_t)index;
}


/* A specialized version of normalize_index() for use with Python integers. */
template <>
size_t normalize_index(PyObject* index, size_t size, bool truncate) {
    // NOTE: this is the same algorithm as _normalize_index() except that it
    // accepts Python integers and handles the associated reference counting.
    if (!PyLong_Check(index)) {
        PyErr_SetString(PyExc_TypeError, "Index must be a Python integer");
        return MAX_SIZE_T;
    }

    // comparisons are kept at the python level until we're ready to return
    PyObject* py_zero = PyLong_FromSize_t(0);  // new reference
    PyObject* py_size = PyLong_FromSize_t(size);  // new reference
    int index_lt_zero = PyObject_RichCompareBool(index, py_zero, Py_LT);

    // wraparound negative indices
    bool release_index = false;
    if (index_lt_zero) {
        index = PyNumber_Add(index, py_size);  // new reference
        index_lt_zero = PyObject_RichCompareBool(index, py_zero, Py_LT);
        release_index = true;  // remember to free index later
    }

    // boundscheck
    if (index_lt_zero || PyObject_RichCompareBool(index, py_size, Py_GE)) {
        // clean up references
        Py_DECREF(py_zero);
        Py_DECREF(py_size);
        if (release_index) {
            Py_DECREF(index);
        }

        // apply truncation if directed
        if (truncate) {
            if (index_lt_zero) {
                return 0;
            }
            return size - 1;
        }

        // raise IndexError
        PyErr_SetString(PyExc_IndexError, "list index out of range");
        return MAX_SIZE_T;
    }

    // value is good - convert to size_t
    size_t result = PyLong_AsSize_t(index);

    // clean up references
    Py_DECREF(py_zero);
    Py_DECREF(py_size);
    if (release_index) {
        Py_DECREF(index);
    }

    return result;
}


/* Normalize the start and stop index and ensure that they are properly ordered. */
template <typename T>
std::pair<size_t, size_t> normalize_bounds(
    T start,
    T stop,
    size_t size,
    bool truncate
) {
    // pass both start and stop through normalize_index()
    size_t norm_start = normalize_index(start, size, truncate);
    size_t norm_stop = normalize_index(stop, size, truncate);

    // check for errors
    if ((norm_start == MAX_SIZE_T || norm_stop == MAX_SIZE_T) && PyErr_Occurred()) {
        return std::make_pair(MAX_SIZE_T, MAX_SIZE_T);  // propagate error
    }

    // check bounds are in order
    if (norm_start > norm_stop) {
        PyErr_SetString(
            PyExc_ValueError,
            "start index must be less than or equal to stop index"
        );
        return std::make_pair(MAX_SIZE_T, MAX_SIZE_T);
    }

    // return normalized bounds
    return std::make_pair(norm_start, norm_stop);
}


// NOTE: We may not always be able to efficiently iterate through a linked list
// in reverse order.  As a result, we can't always guarantee that we iterate
// over a slice in the same direction as the step size would normally indicate.
// For instance, if we have a singly-linked list and we want to iterate over a
// slice with a negative step size, we'll have to start from the head and
// traverse over it backwards.  We can compensate for this by manually
// reversing the slice again as we extract each node, which counteracts the
// previous effect and produces the intended result.

// In the case of doubly-linked lists, we can use this trick to minimize total
// iterations and avoid backtracking.  Since we're free to start from either
// end of the list, we always choose whichever one that is closer to a slice
// boundary, and then reflect the results to match the intended output.

// This changes the way we have to approach our slice indices.  Python slices
// are normally asymmetric and half-open at the stop index, but this presents a
// problem for our optimization strategy.  Because we might be iterating from
// the stop index to the start index rather than the other way around, we need
// to be able to treat the slice symmetrically in both directions.  To
// facilitate this, we convert the slice into a closed interval by rounding the
// stop index to the nearest included step.  This means that both the start and
// stop indices are always included in the slice, allowing us to iterate
// equally in either direction.


/* A modulo operator (%) that matches Python's behavior with respect to
negative numbers. */
template <typename T>
inline T py_modulo(T a, T b) {
    // NOTE: Python's `%` operator is defined such that the result has the same
    // sign as the divisor (b).  This differs from C, where the result has the
    // same sign as the dividend (a).  This function uses the Python version.
    return (a % b + b) % b;
}


/* Adjust the stop index in a slice to make it closed on both ends. */
template <typename T>
inline T closed_interval(T start, T stop, T step) {
    T remainder = py_modulo((stop - start), step);
    if (remainder == 0) {
        return stop - step; // decrement stop by 1 full step
    }
    return stop - remainder;  // decrement stop to nearest multiple of step
}


/* Get the direction in which to traverse a slice that minimizes iterations and
avoids backtracking. */
template <
    template <typename, template <typename> class> class ViewType,
    typename NodeType,
    template <typename> class Allocator
>
std::pair<size_t, size_t> normalize_slice(
    ViewType<NodeType, Allocator>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
) {
    // NOTE: the input to this function is assumed to be the output of
    // slice.indices(), which handles negative indices and 0 step size step
    // size.  Its behavior is undefined if these conditions are not met.
    using Node = typename ViewType<NodeType, Allocator>::Node;

    // convert from half-open to closed interval
    stop = closed_interval(start, stop, step);

    // check if slice is not a valid interval
    if ((step > 0 && start > stop) || (step < 0 && start < stop)) {
        // NOTE: even though the inputs are never negative, the result of
        // closed_interval() may cause `stop` to run off the end of the list.
        // This branch catches that case, in addition to unbounded slices.
        PyErr_SetString(PyExc_ValueError, "invalid slice");
        return std::make_pair(MAX_SIZE_T, MAX_SIZE_T);
    }

    // convert to size_t
    size_t norm_start = (size_t)start;
    size_t norm_stop = (size_t)stop;
    size_t begin, end;

    // get begin and end indices
    if constexpr (is_doubly_linked<Node>::value) {
        // NOTE: if the list is doubly-linked, then we can iterate from either
        // direction.  We therefore choose the direction that's closest to its
        // respective slice boundary.
        if (
            (step > 0 && norm_start <= view->size - norm_stop) ||
            (step < 0 && view->size - norm_start <= norm_stop)
        ) {  // iterate normally
            begin = norm_start;
            end = norm_stop;
        } else {  // reverse
            begin = norm_stop;
            end = norm_start;
        }
    } else {
        // NOTE: if the list is singly-linked, then we can only iterate in one
        // direction, even when the step size is negative.
        if (step > 0) {  // iterate normally
            begin = norm_start;
            end = norm_stop;
        } else {  // reverse
            begin = norm_stop;
            end = norm_start;
        }
    }

    // NOTE: comparing the begin and end indices reveals the direction of
    // traversal for the slice.  If begin < end, then we iterate from the head
    // of the list.  If begin > end, then we iterate from the tail.
    return std::make_pair(begin, end);
}


// NOTE: similarly, we can't necessarily traverse a linked list in reverse
// order from an arbitrary node.  Doubly-linked lists can just iterate
// backwards from the starting node, but this doesn't apply for singly-linked
// equivalents.  Instead, we need to start from the head of the list and look
// ahead until we reach the desired node.


/* Traverse a list relative to a given sentinel to find the left and right
neighbors for an insertion or removal. */
template <
    template <typename, template <typename> class> class ViewType,
    typename NodeType,
    template <typename> class Allocator,
    typename Node
>
std::pair<Node*, Node*> walk(
    ViewType<NodeType, Allocator>* view,
    Node* sentinel,
    Py_ssize_t steps,
    bool truncate
) {
    // adjust steps to account for forward/backward traversal
    if (steps == 0) {
        PyErr_Format(PyExc_ValueError, "steps must be non-zero");
        return std::make_pair<Node*, Node*>(nullptr, nullptr);
    }

    Node* left;
    Node* right;

    // If we're iterating forwards from the sentinel, then the process is the
    // same for both singly- and doubly-linked lists.
    if (steps > 0) {
        left = sentinel;
        right = static_cast<Node*>(left->next);
        for (Py_ssize_t i = 0; i < steps; i++) {
            if (right == nullptr) {
                if (truncate) {
                    break;  // truncate to end of list
                } else {
                    PyErr_SetString(PyExc_IndexError, "list index out of range");
                    return std::make_pair<Node*, Node*>(nullptr, nullptr);
                }
            }
            left = right;
            right = static_cast<Node*>(right->next);
        }
        return std::make_pair(left, right);
    }

    // If the list is doubly-linked, then we can iterate backwards using the
    // `prev` pointer.
    if constexpr (is_doubly_linked<Node>::value) {
        right = sentinel;
        left = static_cast<Node*>(right->prev);
        for (Py_ssize_t i = -1; i > steps; i--) {
            if (left == nullptr) {
                if (truncate) {
                    break;  // truncate to start of list
                } else {
                    PyErr_SetString(PyExc_IndexError, "list index out of range");
                    return std::make_pair<Node*, Node*>(nullptr, nullptr);
                }
            }
            right = left;
            left = static_cast<Node*>(left->prev);
        }
        return std::make_pair(left, right);
    }

    // NOTE: otherwise, we have to iterate from the head of the list.  We do
    // this using a two-pointer approach, where the lookahead pointer is offset
    // from the left pointer by the specified number of steps.  When the
    // lookahead pointer reaches the sentinel, then the left pointer will be at
    // the correct position for the insertion.
    right = sentinel;
    Node* lookahead = view->head;
    for (size_t i = 0; i > steps; i--) {  // advance lookahead to offset
        if (lookahead == right) {  // insert at beginning of list
            return std::make_pair(nullptr, view->head);
        }
        lookahead = static_cast<Node*>(lookahead->next);
    }

    // advance both pointers until lookahead reaches sentinel
    left = view->head;
    while (lookahead != right) {
        left = static_cast<Node*>(left->next);
        lookahead = static_cast<Node*>(lookahead->next);
    }
    return std::make_pair(left, static_cast<Node*>(left->next));
}


#endif  // BERTRAND_STRUCTS_CORE_BOUNDS_H include guard
