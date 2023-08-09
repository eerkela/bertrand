
// include guard prevents multiple inclusion
#ifndef INDEX_H
#define INDEX_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <node.h>  // for nodes
#include <view.h>  // for views


//////////////////////
////    SHARED    ////
//////////////////////


/* MAX_SIZE_T is used to signal errors in indexing operations where NULL would
not be a valid return value, and 0 is likely to be valid output. */
const size_t MAX_SIZE_T = std::numeric_limits<size_t>::max();


/* Allow Python-style negative indexing with wraparound and boundschecking. */
inline size_t normalize_index(
    PyObject* index,
    size_t size,
    bool truncate
) {
    // check that index is a Python integer
    if (!PyLong_Check(index)) {
        PyErr_SetString(PyExc_TypeError, "Index must be a Python integer");
        return MAX_SIZE_T;
    }

    PyObject* pylong_zero = PyLong_FromSize_t(0);
    PyObject* pylong_size = PyLong_FromSize_t(size);
    int index_lt_zero = PyObject_RichCompareBool(index, pylong_zero, Py_LT);

    // wraparound negative indices
    // if index < 0:
    //     index += size
    bool release_index = false;
    if (index_lt_zero) {
        index = PyNumber_Add(index, pylong_size);
        index_lt_zero = PyObject_RichCompareBool(index, pylong_zero, Py_LT);
        release_index = true;
    }

    // boundscheck
    // if index < 0 or index >= size:
    //     if truncate:
    //         if index < 0:
    //             return 0
    //         return size - 1
    //    raise IndexError("list index out of range")
    if (index_lt_zero || PyObject_RichCompareBool(index, pylong_size, Py_GE)) {
        Py_DECREF(pylong_zero);
        Py_DECREF(pylong_size);
        if (release_index) {
            Py_DECREF(index);  // index reference was created by PyNumber_Add()
        }
        if (truncate) {
            if (index_lt_zero) {
                return 0;
            }
            return size - 1;
        }
        PyErr_SetString(PyExc_IndexError, "list index out of range");
        return MAX_SIZE_T;
    }

    // release references
    Py_DECREF(pylong_zero);
    Py_DECREF(pylong_size);
    if (release_index) {
        Py_DECREF(index);  // index reference was created by PyNumber_Add()
    }

    // return as size_t
    return PyLong_AsSize_t(index);
}


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Get the index of an item within a singly-linked set or dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline size_t index(
    ViewType<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    using Node = typename ViewType<NodeType>::Node;

    // search for item in hash table
    Node* node = view->search(item);
    if (node == NULL) {
        PyErr_Format(PyExc_ValueError, "%R is not in the set", item);
        return MAX_SIZE_T;
    }

    // skip to start index
    Node* curr = view->head;
    size_t idx;
    for (idx = 0; idx < start; idx++) {
        if (curr == node) {  // item exists, but comes before range
            PyErr_Format(PyExc_ValueError, "%R is not in the set", item);
            return MAX_SIZE_T;
        }
        curr = (Node*)curr->next;
    }

    // iterate until we hit match or stop index
    while (curr != node && idx < stop) {
        curr = (Node*)curr->next;
        idx++;
    }
    if (curr == node) {
        return idx;
    }

    // item exists, but comes after range
    PyErr_Format(PyExc_ValueError, "%R is not in the set", item);
    return MAX_SIZE_T;
}


/* Get the index of an item within a singly-linked list. */
template <typename NodeType>
inline size_t index(
    ListView<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    using Node = typename ListView<NodeType>::Node;
    Node* curr;
    size_t idx;

    // NOTE: if start index is closer to tail and the list is doubly-linked,
    // we can iterate from the tail to save time.
    if constexpr (is_doubly_linked<Node>::value) {
        if (start > view->size / 2) {
            curr = view->tail;
            for (idx = view->size - 1; idx > stop; idx--) {  // skip to stop index
                curr = (Node*)curr->prev;
            }

            // search until we hit start index
            bool found = false;
            size_t last_observed;
            while (idx >= start) {
                // C API equivalent of the == operator
                int comp = PyObject_RichCompareBool(curr->value, item, Py_EQ);
                if (comp == -1) {  // comparison raised an exception
                    return MAX_SIZE_T;
                } else if (comp == 1) {  // found a match
                    last_observed = idx;
                    found = true;
                }

                // advance to next node
                curr = (Node*)curr->prev;
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
    }

    // NOTE: otherwise, we iterate forward from the head
    curr = view->head;
    for (idx = 0; idx < start; idx++) {  // skip to start index
        curr = (Node*)curr->next;
    }

    // search until we hit stop index
    while (idx < stop) {
        // C API equivalent of the == operator
        int comp = PyObject_RichCompareBool(curr->value, item, Py_EQ);
        if (comp == -1) {  // `==` raised an exception
            return MAX_SIZE_T;
        } else if (comp == 1) {  // found a match
            return idx;
        }

        // advance to next node
        curr = (Node*)curr->next;
        idx++;
    }

    // item not found
    PyErr_Format(PyExc_ValueError, "%R is not in list", item);
    return MAX_SIZE_T;
}


////////////////////////
////    WRAPPERS    ////
////////////////////////


// NOTE: Cython doesn't play well with nested templates, so we need to
// explicitly instantiate specializations for each combination of node/view
// type.  This is a bit of a pain, put it's the only way to get Cython to
// properly recognize the functions.

// Maybe in a future release we won't have to do this:


template size_t index(
    ListView<SingleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t index(
    SetView<SingleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t index(
    DictView<SingleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t index(
    ListView<DoubleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t index(
    SetView<DoubleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t index(
    DictView<DoubleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);


#endif // INDEX_H include guard
