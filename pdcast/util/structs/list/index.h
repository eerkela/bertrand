
// include guard prevents multiple inclusion
#ifndef INDEX_H
#define INDEX_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <node.h>  // for node definitions
#include <view.h>  // for view definitions


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Get the index of an item within a singly-linked list. */
template <typename NodeType>
inline size_t index_single(
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
    while (idx < stop) {
        // C API equivalent of the == operator
        comp = PyObject_RichCompareBool(curr->value, item, Py_EQ);
        if (comp == -1) {  // comparison raised an exception
            return MAX_SIZE_T;
        } else if (comp == 1) {  // found a match
            return idx;
        }

        // advance to next node
        curr = curr->next;
        idx++;
    }

    // item not found
    PyErr_Format(PyExc_ValueError, "%R is not in list", item);
    return MAX_SIZE_T;
}


/* Get the index of an item within a singly-linked set. */
template <typename NodeType>
inline size_t index_single(
    SetView<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    return _index_set(view, view->head, item, start, stop);
}


/* Get the index of a key within a singly-linked dictionary. */
template <typename NodeType>
inline size_t index_single(
    DictView<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    return _index_set(view, view->head, item, start, stop);
}


/* Get the index of an item within a doubly-linked list. */
template <typename NodeType>
inline size_t index_double(
    ListView<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    // if starting index is closer to head, use singly-linked version
    if (start <= view->size / 2) {
        return index_single(view, item, start, stop);
    }

    // else, start from tail
    NodeType* curr = view->tail;
    size_t idx = view->size - 1;
    for (idx; idx > stop; idx--) {  // skip to stop index
        curr = curr->prev;
    }

    // search until we hit start index
    int comp;
    size_t last_observed;
    bool found = false;
    while (idx >= start) {
        // C API equivalent of the == operator
        comp = PyObject_RichCompareBool(curr->value, item, Py_EQ);
        if (comp == -1) {  // comparison raised an exception
            return MAX_SIZE_T;
        } else if (comp == 1) {  // found a match
            last_observed = idx;
            found = true;
        }

        // advance to next node
        curr = curr->prev;
        idx--;
    }

    // return first occurrence in range
    if (found) {
        return last_observed;
    }

    // item not found
    PyErr_Format(PyExc_ValueError, "%R is not in list", item);
    return MAX_SIZE_T;
}


/* Get the index of an item within a doubly-linked set. */
template <typename NodeType>
inline size_t index_double(
    SetView<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    return _index_set(view, view->head, item, start, stop);
}


/* Get the index of an item within a doubly-linked dictionary. */
template <typename NodeType>
inline size_t index_double(
    DictView<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    return _index_set(view, view->head, item, start, stop);
}


///////////////////////
////    PRIVATE    ////
///////////////////////


// NOTE: we don't need to check for NULL due to view->normalize_index()


/* Get the index of an item within a set-like list. */
template <template <typename> class ViewType, typename T, typename U>
inline size_t _index_set(
    ViewType<T>* view,
    U* head,
    PyObject* item,
    size_t start,
    size_t stop
) {
    // check if item is in set
    U* node = view->search(item);
    if (node == NULL) {
        PyErr_Format(PyExc_ValueError, "%R is not in the set", item);
        return MAX_SIZE_T;
    }

    // skip to start index
    U* curr = view->head;
    size_t idx = 0;
    for (idx; idx < start; idx++) {
        if (curr == node) {  // item exists, but comes before range
            PyErr_Format(PyExc_ValueError, "%R is not in the set", item);
            return MAX_SIZE_T;
        }
        curr = (U*)curr->next;
    }

    // search until we hit stop index
    while (curr != node && idx < stop) {
        curr = (U*)curr->next;
        idx++;
    }

    // check for match
    if (curr == node) {
        return idx;
    }

    // item exists, but comes after range
    PyErr_Format(PyExc_ValueError, "%R is not in the set", item);
    return MAX_SIZE_T;
}


#endif // INDEX_H include guard
