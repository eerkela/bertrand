
// include guard prevents multiple inclusion
#ifndef COUNT_H
#define COUNT_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <node.h>  // for node definitions
#include <view.h>  // for view definitions


// NOTE: we don't need to check for NULLs due to view->normalize_index()


/* Count the number of occurrences of an item within a singly-linked list. */
template <typename NodeType>
inline size_t count_single(
    ListView<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    // skip to start index
    NodeType* curr = view->head;
    size_t idx = 0;
    for (idx; idx < start; idx++) {
        curr = curr->next;
    }

    // search until we hit stop index
    int comp;
    size_t observed = 0;
    while (idx < stop) {
        // C API equivalent of the == operator
        comp = PyObject_RichCompareBool(curr->value, item, Py_EQ);
        if (comp == -1) {  // comparison raised an exception
            return MAX_SIZE_T;
        } else if (comp == 1) {  // found a match
            observed++;
        }

        // advance to next node
        curr = curr->next;
        idx++;
    }

    return observed;
}


/* Count the number of occurrences of an item within a singly-linked set. */
template <typename NodeType>
inline size_t count_single(
    SetView<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    return _count_set(view, view->head, item, start, stop);
}


/* Count the number of occurrences of an item within a singly-linked set. */
template <typename NodeType>
inline size_t count_single(
    DictView<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    return _count_set(view, view->head, item, start, stop);
}


/* Count the number of occurrences of an item within a doubly-linked list. */
template <typename NodeType>
inline size_t count_double(
    ListView<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    // if starting index is closer to head, use singly-linked version
    if (start <= view->size / 2) {
        return count_single(view, item, start, stop);
    }

    // else, start from tail
    NodeType* curr = view->tail;
    size_t i = view->size - 1;
    for (i; i >= stop; i--) {  // skip to stop index
        curr = curr->prev;
    }

    // search until we hit start index
    int comp;
    size_t observed = 0;
    while (i >= start) {
        // C API equivalent of the == operator
        comp = PyObject_RichCompareBool(curr->value, item, Py_EQ);
        if (comp == -1) {  // comparison raised an exception
            return MAX_SIZE_T;
        } else if (comp == 1) {  // found a match
            observed++;
        }

        // advance to next node
        curr = curr->prev;
        i--;
    }

    // return final count
    return observed;
}


/* Count the number of occurrences of an item within a doubly-linked set. */
template <typename NodeType>
inline size_t count_double(
    SetView<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    return _count_set(view, view->head, item, start, stop);
}


/* Count the number of occurrences of a key within a doubly-linked dictionary. */
template <typename NodeType>
inline size_t count_double(
    DictView<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    return _count_set(view, view->head, item, start, stop);
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Count the number of occurrences of an item within a set-like list. */
template <template <typename> class ViewType, typename T, typename U>
inline size_t _count_set(
    ViewType<T>* view,
    U* head, PyObject* item,
    size_t start,
    size_t stop
) {
    // check if item is contained in view
    U* node = view->search(item);
    if (node == NULL) {
        return 0;
    }

    // if range includes all items, return 1
    if (start == 0 && stop == view->size - 1) {
        return 1;
    }

    // else, find index of item
    U* curr = view->head;
    size_t idx = 0;
    while (curr != node && idx < stop) {
        curr = (U*)curr->next;
        idx++;
    }

    // check if index is in range
    if (idx >= start && idx < stop) {
        return 1;
    }
    return 0;
}


#endif // COUNT_H include guard
