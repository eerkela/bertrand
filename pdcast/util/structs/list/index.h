
// include guard prevents multiple inclusion
#ifndef INDEX_H
#define INDEX_H

#include <limits>  // for std::numeric_limits
#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <node.h>  // for node definitions, views, etc.


// TODO: node_at_index might be redundant.


//////////////////////
////    PUBLIC    ////
//////////////////////


const size_t MAX_SIZE_T = std::numeric_limits<size_t>::max();


/* Allow Python-style negative indexing with wraparound. */
inline size_t normalize_index(
    long long index,
    size_t size,
    bool truncate = false
) {
    // wraparound
    if (index < 0) {
        index += size;
    }

    // boundscheck
    if (index < 0 || index >= (long long)size) {
        if (truncate) {
            if (index < 0) {
                return 0;
            }
            return size - 1;
        }
        PyErr_SetString(IndexError, "list index out of range");
        return MAX_SIZE_T;
    }

    return (size_t)index;
}


/* Index operations for singly-linked lists. */
namespace SingleIndex {

    /* Get a node at a given index of a list. */
    template <typename NodeType>
    inline NodeType* node_at_index(ListView<NodeType>* view, size_t index) {
        NodeType* curr = view->head;
        for (size_t i = 0; i < index; i++) {
            curr = curr->next;
        }
        return curr;
    }

    /* Get a node at a given index of a set. */
    template <typename NodeType>
    inline Hashed<NodeType>* node_at_index(SetView<NodeType>* view, size_t index) {
        Hashed<NodeType>* curr = view->head;
        for (size_t i = 0; i < index; i++) {
            curr = (Hashed<NodeType>*)curr->next;
        }
        return curr;
    }

    /* Get a node at a given index of a dictionary. */
    template <typename NodeType>
    inline Mapped<NodeType>* node_at_index(DictView<NodeType>* view, size_t index) {
        Mapped<NodeType>* curr = view->head;
        for (size_t i = 0; i < index; i++) {
            curr = (Mapped<NodeType>*)curr->next;
        }
        return curr;
    }

    /* Get the index of an item within a list. */
    template <typename NodeType>
    inline size_t index(
        ListView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        // iterate forward from head
        return _get_index_single(view->head, view->size, item, start, stop);
    }

    /* Get the index of an item within a set. */
    template <typename NodeType>
    inline size_t index(
        SetView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        // check if item is in set
        if (view->search(item) == NULL) {
            PyErr_Format(PyExc_ValueError, "%R is not in set", item);
            return MAX_SIZE_T;
        }

        // iterate forward from head
        return _get_index_single(view->head, view->size, item, start, stop);
    }

    /* Get the index of a key within a dictionary. */
    template <typename NodeType>
    inline size_t index(
        DictView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        // check if item is in set
        if (view->search(item) == NULL) {
            PyErr_Format(PyExc_ValueError, "%R is not in set", item);
            return MAX_SIZE_T;
        }

        // iterate forward from head
        return _get_index_single(view->head, view->size, item, start, stop);
    }

}


/* Index operations for doubly-linked lists. */
namespace DoubleIndex {

    /* Get a node at a given index. */
    template <template<typename> class ViewType, typename NodeType>
    inline NodeType* node_at_index(ViewType<NodeType>* view, size_t index) {
        // if index is closer to head, use singly-linked version
        if (index <= view->size / 2) {
            return SingleIndex::node_at_index(view, index)
        }

        // else, start from tail
        NodeType* curr = view->tail;
        for (size_t i = view->size - 1; i > index; i--) {
            curr = curr->prev;
        }
        return curr;
    }

    /* Get the index of an item within the list. */
    template <template<typename> class ViewType, typename NodeType>
    size_t index(
        ViewType<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        size_t norm_start = normalize_index(start, view->size, true);
        size_t norm_stop = normalize_index(stop, view->size, true);
        if (norm_start > norm_stop) {
            PyErr_SetString(
                PyExc_ValueError,
                "start index must be less than or equal to stop index"
            );
            return MAX_SIZE_T;
        }

        // if starting index is closer to head, use singly-linked version
        if (norm_start <= view->size / 2) {
            return SingleIndex::index(view, item, start, stop);
        }

        // else, start from tail
        NodeType* curr = view->tail;
        size_t i = view->size - 1;
        for (i; i >= norm_stop; i--) {  // skip to stop index
            curr = curr->prev;
        }

        // search until we hit start index
        int comp;
        size_t last_observed;
        bool found = false;
        while (i >= norm_start) {
            // C API equivalent of the == operator
            comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
            if (comp == -1) {  // comparison raised an exception
                return MAX_SIZE_T;
            } else if (comp == 1) {  // found a match
                last_observed = i;
                found = true;
            }

            // advance to next node
            curr = curr->prev;
            i--;
        }

        // return first occurrence in range
        if (found) {
            return last_observed;
        }

        // item not found
        PyErr_Format(PyExc_ValueError, "%R is not in list", item);
        return MAX_SIZE_T;
    }

    /* Get the index of an item within the set. */
    template <typename NodeType>
    size_t index(
        SetView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        size_t norm_start = normalize_index(start, view->size, true);
        size_t norm_stop = normalize_index(stop, view->size, true);
        if (norm_start > norm_stop) {
            PyErr_SetString(
                PyExc_ValueError,
                "start index must be less than or equal to stop index"
            );
            return MAX_SIZE_T;
        }

        // check if item is in set
        Hashed<NodeType>* node = view->search(item);
        if (node == NULL) {
            PyErr_Format(PyExc_ValueError, "%R is not in set", item);
            return MAX_SIZE_T;
        }

        // count backwards to find index
        size_t idx = 0;
        while (node != NULL && idx < norm_stop) {
            node = node->prev;
            idx++;
        }

        // check if index is in range
        if (idx >= norm_start && idx < norm_stop) {
            return idx;
        }

        // item not found
        PyErr_Format(PyExc_ValueError, "%R is not in set", item);
        return MAX_SIZE_T;
    }

    /* Get the index of an item within the set. */
    template <typename NodeType>
    size_t index(
        DictView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        size_t norm_start = normalize_index(start, view->size, true);
        size_t norm_stop = normalize_index(stop, view->size, true);
        if (norm_start > norm_stop) {
            PyErr_SetString(
                PyExc_ValueError,
                "start index must be less than or equal to stop index"
            );
            return MAX_SIZE_T;
        }

        // check if item is in set
        Mapped<NodeType>* node = view->search(item);
        if (node == NULL) {
            PyErr_Format(PyExc_ValueError, "%R is not in set", item);
            return MAX_SIZE_T;
        }

        // count backwards to find index
        size_t idx = 0;
        while (node != NULL && idx < norm_stop) {
            node = node->prev;
            idx++;
        }

        // check if index is in range
        if (idx >= norm_start && idx < norm_stop) {
            return idx;
        }

        // item not found
        PyErr_Format(PyExc_ValueError, "%R is not in set", item);
        return MAX_SIZE_T;
    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Helper for getting the index of an item within a singly-linked list, set, or
dictionary. */
template <typename NodeType>
size_t _get_index_single(
    NodeType* head,
    size_t size,
    PyObject* item,
    long long start,
    long long stop
) {
    // normalize range
    size_t norm_start = normalize_index(start, size, true);
    size_t norm_stop = normalize_index(stop, size, true);
    if (norm_start > norm_stop) {
        PyErr_SetString(
            PyExc_ValueError,
            "start index must be less than or equal to stop index"
        );
        return MAX_SIZE_T;
    }

    // NOTE: we don't need to check for NULL due to normalize_index()

    // skip to start index
    NodeType* curr = head;
    size_t idx = 0;
    for (idx; idx < norm_start; i++) {
        curr = (NodeType*)curr->next;
    }

    // search until we hit stop index
    int comp;
    while (idx < norm_stop) {
        // C API equivalent of the == operator
        comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
        if (comp == -1) {  // comparison raised an exception
            return MAX_SIZE_T;
        } else if (comp == 1) {  // found a match
            return idx;
        }

        // advance to next node
        curr = (NodeType*)curr->next;
        idx++;
    }

    // item not found
    PyErr_Format(PyExc_ValueError, "%R is not in list", item);
    return MAX_SIZE_T;

}


#endif // INDEX_H include guard
