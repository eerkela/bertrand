// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_BOUNDS_H
#define BERTRAND_STRUCTS_CORE_BOUNDS_H

#include <cstddef>  // size_t
#include <limits>  // std::numeric_limits
#include <tuple>  // std::tuple
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "node.h"  // has_prev<>



// TODO: what if the slice methods are implemented in a SliceProxy class similar to
// RelativeProxy?  This would automatically compute the slice bounds, make them
// inclusive, and get the overall length of the slice.  This removes some boilerplate
// from the slice methods and makes them a bit easier to understand.  The proxy could
// have an invalid() flag that indicates whether the slice is malformed.

// view->slice(start, stop, step) -> SliceProxy

// view.slice(1, 6, 2).get()
// view.slice(1, 6, 2).set(items)
// view.slice(1, 6, 2).del()

// view->slice(1, 6, 2).execute(Slice::get)
// view->slice(1, 6, 2).execute(Slice::set, [1, 2, 3])
// view->slice(1, 6, 2).execute(Slice::del)


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
    // slice.indices(), which handles missing and negative indices as well as 0
    // step size.  Its behavior is undefined if these conditions are not met.
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
    if constexpr (has_prev<Node>::value) {
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


/* Get the total number of nodes that are included in a closed slice. */
inline size_t slice_length(size_t begin, size_t end, size_t abs_step) {
    size_t length = llabs((long long)end - (long long)begin);
    return (length / abs_step) + 1;
}


// NOTE: similarly, we have to be careful about how we handle insertions and
// removals within the list.  Since these need access to the neighboring nodes,
// we actually have to find the previous node and then return its successor(s).
// These helpers greatly simplify the process of finding the correct nodes in
// these cases.


/* Get a node at a specified position within a linked list. */
template <typename View, typename Node>
Node* node_at_index(View* view, Node* head, size_t index) {
    // If the list is doubly-linked, we can traverse from either end
    if constexpr (has_prev<Node>::value) {
        if (index > view->size / 2) {  // backward traversal
            Node* curr = view->tail;
            for (size_t i = view->size - 1; i > index; i--) {
                curr = static_cast<Node*>(curr->prev);
            }
            return curr;
        }
    }

    // Otherwise, we have to iterate from the head
    Node* curr = view->head;
    for (size_t i = 0; i < index; i++) {
        curr = static_cast<Node*>(curr->next);
    }
    return curr;
}


/* Traverse a list to a given index in order to find the left and right
bounds for an insertion. */
template <typename View, typename Node>
std::pair<Node*, Node*> junction(View* view, Node* head, size_t index) {
    Node* prev;
    Node* curr;

    // get previous node and next successor
    if (index == 0) {  // special case for head of list
        prev = nullptr;
        curr = view->head;
    } else {
        prev = node_at_index(view, head, index - 1);
        curr = static_cast<Node*>(prev->next);
    }

    return std::make_pair(prev, curr);
}


/* Traverse a list to a given index in order to find the left and right
bounds for a removal. */
template <typename View, typename Node>
inline std::tuple<Node*, Node*, Node*> neighbors(View* view, Node* head, size_t index) {
    // start with junction() to get the previous and current nodes
    std::pair<Node*, Node*> bounds = junction(view, head, index);

    // second bound corresponds to current node, so we just get its successor
    Node* next = static_cast<Node*>(bounds.second->next);
    return std::make_tuple(bounds.first, bounds.second, next);
}


// NOTE: Sometimes we need to traverse a list from an arbitrary node, which
// might not be the head or tail of the list.  This is common when dealing with
// linked sets and dictionaries, where we maintain a hash table that gives
// constant-time access to any node in the list.  In these cases, we can save
// time by walking along the list relative to a sentinel node rather than
// blindly starting at the head or tail.


/* Get a node relative to another node within a linked list, set, or dictionary. */
template <typename View, typename Node>
Node* walk(View* view, Node* node, Py_ssize_t offset, bool truncate) {
    // check for no-op
    if (offset == 0) {
        return node;
    }

    // if we're traversing forward, then the process is the same for both
    // singly- and doubly-linked lists
    if (offset > 0) {
        Node* curr = node;
        for (Py_ssize_t i = 0; i < offset; i++) {
            if (curr == nullptr) {
                if (truncate) {
                    return view->tail;  // truncate to end of list
                } else {
                    return nullptr;  // index out of range
                }
            }
            curr = static_cast<Node*>(curr->next);
        }
        return curr;
    }

    // if the list is doubly-linked, then we can traverse backward just as easily
    if constexpr (has_prev<Node>::value) {
        Node* curr = node;
        for (Py_ssize_t i = 0; i > offset; i--) {
            if (curr == nullptr) {
                if (truncate) {
                    return view->head;  // truncate to beginning of list
                } else {
                    return nullptr;  // index out of range
                }
            }
            curr = static_cast<Node*>(curr->prev);
        }
        return curr;
    }

    // Otherwise, we have to iterate from the head of the list.  We do this using
    // a two-pointer approach where the `lookahead` pointer is offset from the
    // `curr` pointer by the specified number of steps.  When it reaches the
    // sentinel, then `curr` will be at the correct position.
    Node* lookahead = view->head;
    for (Py_ssize_t i = 0; i > offset; i--) {  // advance lookahead to offset
        if (lookahead == node) {
            if (truncate) {
                return view->head;  // truncate to beginning of list
            } else {
                return nullptr;  // index out of range
            }
        }
        lookahead = static_cast<Node*>(lookahead->next);
    }

    // advance both pointers until lookahead reaches sentinel
    Node* curr = view->head;
    while (lookahead != node) {
        curr = static_cast<Node*>(curr->next);
        lookahead = static_cast<Node*>(lookahead->next);
    }
    return curr;
}


/* Traverse a list relative to a given sentinel to find the left and right
bounds for an insertion. */
template <typename View, typename Node>
std::pair<Node*, Node*> relative_junction(
    View* view,
    Node* sentinel,
    Py_ssize_t offset,
    bool truncate
) {
    // get the previous node for the insertion point
    Node* prev = walk(view, sentinel, offset - 1, false);

    // apply truncate rule
    if (prev == nullptr) {  // walked off end of list
        if (!truncate) {
            return std::make_pair(nullptr, nullptr);  // error code
        }
        if (offset < 0) {
            return std::make_pair(nullptr, view->head);  // beginning of list
        }
        return std::make_pair(view->tail, nullptr);  // end of list
    }

    // return the previous node and its successor
    Node* next = static_cast<Node*>(prev->next);
    return std::make_pair(prev, next);
}


/* Traverse a list relative to a given sentinel to find the left and right
bounds for a removal */
template <typename View, typename Node>
std::tuple<Node*, Node*, Node*> relative_neighbors(
    View* view,
    Node* sentinel,
    Py_ssize_t offset,
    bool truncate
) {
    // NOTE: we can't reuse relative_junction() here because we need access to
    // the node preceding the tail in the event that we walk off the end of the
    // list and truncate=true.
    Node* prev;
    Node* curr = sentinel;
    Node* next;

    // NOTE: this is trivial for doubly-linked lists
    if constexpr (has_prev<Node>::value) {
        if (offset > 0) {  // forward traversal
            next = static_cast<Node*>(curr->next);
            for (Py_ssize_t i = 0; i < offset; i++) {
                if (next == nullptr) {
                    if (truncate) {
                        break;  // truncate to end of list
                    } else {
                        return std::make_tuple(nullptr, nullptr, nullptr);
                    }
                }
                curr = next;
                next = static_cast<Node*>(curr->next);
            }
            prev = static_cast<Node*>(curr->prev);
        } else {  // backward traversal
            prev = static_cast<Node*>(curr->prev);
            for (Py_ssize_t i = 0; i > offset; i--) {
                if (prev == nullptr) {
                    if (truncate) {
                        break;  // truncate to beginning of list
                    } else {
                        return std::make_tuple(nullptr, nullptr, nullptr);
                    }
                }
                curr = prev;
                prev = static_cast<Node*>(curr->prev);
            }
            next = static_cast<Node*>(curr->next);
        }
        return std::make_tuple(prev, curr, next);
    }

    // NOTE: It gets significantly more complicated if the list is singly-linked.
    // In this case, we can only optimize the forward traversal branch if we're
    // advancing at least one node and the current node is not the tail of the
    // list.
    if (truncate && offset > 0 && curr == view->tail) {
        offset = 0;  // skip forward iteration branch
    }

    // forward iteration (efficient)
    if (offset > 0) {
        prev = nullptr;
        next = static_cast<Node*>(curr->next);
        for (Py_ssize_t i = 0; i < offset; i++) {
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
    for (size_t i = 0; i > offset; i--) {  // advance lookahead to offset
        if (lookahead == curr) {
            if (truncate) {  // truncate to beginning of list
                next = static_cast<Node*>(view->head->next);
                return std::make_tuple(nullptr, view->head, next);
            } else {  // index out of range
                return std::make_tuple(nullptr, nullptr, nullptr);
            }
        }
        lookahead = static_cast<Node*>(lookahead->next);
    }

    // advance both pointers until lookahead reaches sentinel
    prev = nullptr;
    Node* temp = view->head;
    while (lookahead != curr) {
        prev = temp;
        temp = static_cast<Node*>(temp->next);
        lookahead = static_cast<Node*>(lookahead->next);
    }
    next = static_cast<Node*>(temp->next);
    return std::make_tuple(prev, temp, next);
}


#endif  // BERTRAND_STRUCTS_CORE_BOUNDS_H include guard
