
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


// NOTE: we don't need to check for NULL due to view->normalize_index()


namespace SinglyLinked {

    /* Get the index of an item within a list. */
    template <typename NodeType>
    inline size_t index(
        ListView<NodeType>* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) {
        // skip to start index
        NodeType* curr = head;
        size_t idx = 0;
        for (idx; idx < start; i++) {
            curr = curr->next;
        }

        // search until we hit stop index
        int comp;
        while (idx < stop) {
            // C API equivalent of the == operator
            comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
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

    /* Get the index of an item within a set. */
    template <typename NodeType>
    inline size_t index(
        SetView<NodeType>* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) {
        // check if item is in set
        Hashed<NodeType>* node = view->search(item);
        if (node == NULL) {
            PyErr_Format(PyExc_ValueError, "%R is not in set", item);
            return MAX_SIZE_T;
        }

        // skip to start index
        Hashed<NodeType>* curr = view->head;
        size_t idx = 0;
        for (idx; idx < start; i++) {
            curr = (Hashed<NodeType>*)curr->next;
        }

        // search until we hit stop index
        // NOTE: no need to use Python API here since we already know the target node
        while (idx < stop) {
            if (curr == node) {  // found a match
                return idx;
            }

            // advance to next node
            curr = (Hashed<NodeType>*)curr->next;
            idx++;
        }

        // item not found
        PyErr_Format(PyExc_ValueError, "%R is not in list", item);
        return MAX_SIZE_T;
    }

    /* Get the index of a key within a dictionary. */
    template <typename NodeType>
    inline size_t index(
        DictView<NodeType>* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) {
        // check if item is in set
        Mapped<NodeType>* node = view->search(item);
        if (node == NULL) {
            PyErr_Format(PyExc_ValueError, "%R is not in set", item);
            return MAX_SIZE_T;
        }

        // skip to start index
        Mapped<NodeType>* curr = view->head;
        size_t idx = 0;
        for (idx; idx < start; i++) {
            curr = (Mapped<NodeType>*)curr->next;
        }

        // search until we hit stop index
        // NOTE: no need to use Python API here since we already know the target node
        while (idx < stop) {
            if (curr == node) {  // found a match
                return idx;
            }

            // advance to next node
            curr = (Mapped<NodeType>*)curr->next;
            idx++;
        }

        // item not found
        PyErr_Format(PyExc_ValueError, "%R is not in list", item);
        return MAX_SIZE_T;
    }

}


namespace DoublyLinked {

    /* Get the index of an item within the list. */
    template <template<typename> class ViewType, typename NodeType>
    size_t index(
        ViewType<NodeType>* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) {
        // if starting index is closer to head, use singly-linked version
        if (start <= view->size / 2) {
            return SinglyLinked::index(view, item, start, stop);
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
            comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
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

    /* Get the index of an item within the set. */
    template <typename NodeType>
    size_t index(
        SetView<NodeType>* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) {
        // check if item is in set
        Hashed<NodeType>* node = view->search(item);
        if (node == NULL) {
            PyErr_Format(PyExc_ValueError, "%R is not in set", item);
            return MAX_SIZE_T;
        }

        // count backwards to find index
        size_t idx = 0;
        while (node != NULL && idx < stop) {
            node = (Hashed<NodeType>*)node->prev;
            idx++;
        }

        // check if index is in range
        if (idx >= start && idx < stop) {
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
        size_t start,
        size_t stop
    ) {
        // check if item is in set
        Mapped<NodeType>* node = view->search(item);
        if (node == NULL) {
            PyErr_Format(PyExc_ValueError, "%R is not in set", item);
            return MAX_SIZE_T;
        }

        // count backwards to find index
        size_t idx = 0;
        while (node != NULL && idx < stop) {
            node = (Mapped<NodeType>*)node->prev;
            idx++;
        }

        // check if index is in range
        if (idx >= start && idx < stop) {
            return idx;
        }

        // item not found
        PyErr_Format(PyExc_ValueError, "%R is not in set", item);
        return MAX_SIZE_T;
    }

}


#endif // INDEX_H include guard
