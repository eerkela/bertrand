// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_BOUNDS_H
#define BERTRAND_STRUCTS_CORE_BOUNDS_H

#include <cstddef>  // size_t
#include <limits>  // std::numeric_limits
#include <tuple>  // std::tuple
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


/* Normalize the start and stop indices and ensure that they are properly ordered. */
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
// stop indices are inclusive, allowing us to iterate equally in either direction.


/* A modulo operator (%) that matches Python's behavior with respect to
negative numbers. */
template <typename T>
inline T py_modulo(T a, T b) {
    // NOTE: Python's `%` operator is defined such that the result has the same
    // sign as the divisor (b).  This differs from C/C++, where the result has
    // the same sign as the dividend (a).
    return (a % b + b) % b;
}


/* Adjust the stop index in a slice to make it closed on both ends. */
template <typename T>
inline T closed_interval(T start, T stop, T step) {
    T remainder = py_modulo((stop - start), step);
    if (remainder == 0) {
        return stop - step; // decrement by 1 full step
    }
    return stop - remainder;  // decrement to nearest multiple of step
}


/* Get the direction in which to traverse a slice that minimizes iterations and
avoids backtracking. */
template <typename View>
std::pair<size_t, size_t> normalize_slice(
    View* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
) {
    // NOTE: the input to this function is assumed to be the output of
    // slice.indices(), which handles negative indices and 0 step size.  Its
    // behavior is undefined if these conditions are not met.
    using Node = typename View::Node;

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
    // of the list.  If begin > end, we iterate from the tail.
    return std::make_pair(begin, end);
}


// NOTE: similarly, we have to be careful about how we handle insertions and
// removals within the list.  Since these need access to the neighboring nodes,
// we have to find the previous node.  This is trivial for doubly-linked lists,
// but requires a full linear traversal for singly-linked ones.  These helper
// methods allow us to abstract away the differences between the two.


/* Traverse a list to a given index in order to find the left and right
bounds for an insertion. */
template <typename View, typename Node, typename T>
std::pair<Node*, Node*> junction(View* view, Node* head, T index, bool truncate) {
    // allow Python-style negative indexing + boundschecking
    size_t norm_index = normalize_index(index, view->size, truncate);
    Node* curr;

    // NOTE: if the list is doubly-linked, then we can traverse from either end
    // and only need a single pointer.
    if constexpr (is_doubly_linked<Node>::value) {
        if (norm_index > view->size / 2) {  // backward traversal
            curr = view->tail;
            for (size_t i = view->size - 1; i > norm_index; i--) {
                curr = static_cast<Node*>(curr->prev);
            }
            return std::make_pair(curr, static_cast<Node*>(curr->next));

        } else {  // forward traversal
            curr = view->head;
            for (size_t i = 0; i < norm_index; i++) {
                curr = static_cast<Node*>(curr->next);
            }
            return std::make_pair(static_cast<Node*>(curr->prev), curr);
        }
    }

    // Otherwise, iterate from the head
    Node* prev = nullptr;
    curr = view->head;
    for (size_t i = 0; i < norm_index; i++) {
        prev = curr;
        curr = static_cast<Node*>(curr->next);
    }
    return std::make_pair(prev, curr);
}


/* Traverse a list to a given index in order to find the left and right
bounds for a removal. */
template <typename View, typename Node, typename T>
std::tuple<Node*, Node*, Node*> neighbors(
    View* view,
    Node* head,
    T index,
    bool truncate
) {
    // allow Python-style negative indexing + boundschecking
    size_t norm_index = normalize_index(index, view->size, truncate);

    Node* prev;
    Node* curr;
    Node* next;

    // NOTE: if the list is doubly-linked, then we can traverse from either end
    // and only need a single pointer.
    if constexpr (is_doubly_linked<Node>::value) {
        if (norm_index > view->size / 2) {  // backward traversal
            curr = view->tail;
            for (size_t i = view->size - 1; i > norm_index; i--) {
                curr = static_cast<Node*>(curr->prev);
            }
        } else {  // forward traversal
            curr = view->head;
            for (size_t i = 0; i < norm_index; i++) {
                curr = static_cast<Node*>(curr->next);
            }
        }
        prev = static_cast<Node*>(curr->prev);
        next = static_cast<Node*>(curr->next);
        return std::make_tuple(prev, curr, next);
    }

    // Otherwise, iterate from the head
    prev = nullptr;
    curr = view->head;
    for (size_t i = 0; i < norm_index; i++) {
        prev = curr;
        curr = static_cast<Node*>(curr->next);
    }
    next = static_cast<Node*>(curr->next);
    return std::make_tuple(prev, curr, next);
}


// NOTE: Sometimes we need to traverse a list from an arbitrary node, which
// might not be the head or tail of the list.  This is common when dealing with
// linked sets and dictionaries, where we maintain a hash table that gives
// constant-time access to any node in the list.  In these cases, we can save
// time by walking along the list relative to a sentinel node rather than
// blindly starting at the head or tail.


/* Traverse a list relative to a given sentinel to find the left and right
bounds for an insertion. */
template <typename View, typename Node>
std::pair<Node*, Node*> relative_junction(
    View* view,
    Node* sentinel,
    Py_ssize_t steps,
    bool truncate
) {
    Node* prev;
    Node* curr = sentinel;
    Node* next;

    // NOTE: this is trivial for doubly-linked lists since we can easily
    // iterate in both directions.
    if constexpr (is_doubly_linked<Node>::value) {
        if (steps < 0) {
            prev = static_cast<Node*>(curr->prev);
            for (Py_ssize_t i = 0; i > steps; i--) {
                if (prev == nullptr) {
                    if (truncate) {
                        break;
                    } else {
                        return std::make_pair(nullptr, nullptr);
                    }
                }
                curr = prev;
                prev = static_cast<Node*>(curr->prev);
            }
            return std::make_pair(prev, curr);
        } else {
            next = static_cast<Node*>(curr->next);
            for (Py_ssize_t i = 0; i < steps; i++) {
                if (next == nullptr) {
                    if (truncate) {
                        break;
                    } else {
                        return std::make_pair(nullptr, nullptr);
                    }
                }
                curr = next;
                next = static_cast<Node*>(curr->next);
            }
            return std::make_pair(curr, next);
        }
    }

    // NOTE: it is substantially more complicated if the list is singly-linked.
    // in this case, we can only iterate in one direction, which means our
    // traversal must be asymmetric.  we can still optimize for the case where
    // we are iterating forward from the sentinel by one or more nodes, but
    // in all other cases, we have to start from the head and iterate forward
    // until we find the preceding node.

    // normally we would always get the previous node while iterating forward,
    // but this is not the case if the sentinel is the tail of the list and
    // truncate=true. If truncate=False, we just raise an error like normal.
    if (truncate && steps > 0 && sentinel == view->tail) {
        steps = 0;  // skip forward iteration branch
    }

    // forward iteration (efficient)
    if (steps > 0) {
        next = static_cast<Node*>(curr->next);
        for (Py_ssize_t i = 0; i < steps; i++) {
            if (next == nullptr) {  // walked off end of list
                if (truncate) {
                    break;
                } else {
                    return std::make_pair(nullptr, nullptr);
                }
            }
            curr = next;
            next = static_cast<Node*>(curr->next);
        }
        return std::make_pair(curr, next);
    }

    // NOTE: iterating from the head requires a two-pointer approach where the
    // `lookahead` pointer is offset from the `curr` pointer by the specified
    // number of steps.  When it reaches the sentinel, `curr` will be at the
    // correct position.
    Node* lookahead = view->head;
    for (size_t i = 0; i > steps; i--) {  // advance lookahead to offset
        if (lookahead == sentinel) {
            if (truncate) {  // truncate to beginning of list
                return std::make_pair(nullptr, view->head);
            } else {  // index out of range
                return std::make_pair(nullptr, nullptr);
            }
        }
        lookahead = static_cast<Node*>(lookahead->next);
    }

    // advance both pointers until lookahead reaches sentinel
    prev = nullptr;
    curr = view->head;
    while (lookahead != sentinel) {
        prev = curr;
        curr = static_cast<Node*>(curr->next);
        lookahead = static_cast<Node*>(lookahead->next);
    }
    return std::make_tuple(prev, curr);
}


/* Traverse a list relative to a given sentinel to find the left and right
bounds for a removal */
template <typename View, typename Node>
std::tuple<Node*, Node*, Node*> relative_neighbors(
    View* view,
    Node* sentinel,
    Py_ssize_t steps,
    bool truncate
) {
    Node* prev;
    Node* curr = sentinel;
    Node* next;

    // doubly-linked case
    if constexpr (is_doubly_linked<Node>::value) {
        if (steps < 0) {  // backward traversal
            prev = static_cast<Node*>(curr->prev);
            for (Py_ssize_t i = 0; i > steps; i--) {
                if (prev == nullptr) {
                    if (truncate) {
                        break;
                    } else {
                        return std::make_tuple(nullptr, nullptr, nullptr);
                    }
                }
                curr = prev;
                prev = static_cast<Node*>(curr->prev);
            }
            next = static_cast<Node*>(curr->next);
        } else {  // forward traversal
            next = static_cast<Node*>(curr->next);
            for (Py_ssize_t i = 0; i < steps; i++) {
                if (next == nullptr) {
                    if (truncate) {
                        break;
                    } else {
                        return std::make_tuple(nullptr, nullptr, nullptr);
                    }
                }
                curr = next;
                next = static_cast<Node*>(curr->next);
            }
            prev = static_cast<Node*>(curr->prev);
        }
        return std::make_tuple(prev, curr, next);
    }

    // handle edge case where sentinel is tail and truncate=true
    if (truncate && steps > 0 && sentinel == view->tail) {
        steps = 0;  // skip forward iteration branch
    }

    // forward iteration (efficient)
    if (steps > 0) {
        prev = nullptr;
        next = static_cast<Node*>(curr->next);
        for (Py_ssize_t i = 0; i < steps; i++) {
            if (next == nullptr) {  // walked off end of list
                if (truncate) {
                    break;
                } else {
                    return std::make_tuple(nullptr, nullptr, nullptr);
                }
            }
            if (prev == nullptr) {
                prev = curr;
            }
            curr = next;
            next = static_cast<Node*>(curr->next);
        }
        return std::make_tuple(prev, curr, next);
    }

    // backward iteration (inefficient)
    Node* lookahead = view->head;
    for (size_t i = 0; i > steps; i--) {  // advance lookahead to offset
        if (lookahead == sentinel) {
            if (truncate) {  // truncate to beginning of list
                return std::make_tuple(nullptr, view->head, view->head->next);
            } else {  // index out of range
                return std::make_tuple(nullptr, nullptr, nullptr);
            }
        }
        lookahead = static_cast<Node*>(lookahead->next);
    }

    // advance both pointers until lookahead reaches sentinel
    prev = nullptr;
    curr = view->head;
    while (lookahead != sentinel) {
        prev = curr;
        curr = static_cast<Node*>(curr->next);
        lookahead = static_cast<Node*>(lookahead->next);
    }
    next = static_cast<Node*>(curr->next);
    return std::make_tuple(prev, curr, next);
}


#endif  // BERTRAND_STRUCTS_CORE_BOUNDS_H include guard
