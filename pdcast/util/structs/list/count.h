
// include guard prevents multiple inclusion
#ifndef COUNT_H
#define COUNT_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <view.h>  // for views
#include <index.h>  // for index(), normalize_index()


namespace SinglyLinked {

    /* Count the number of occurrences of an item within a list. */
    template <typename NodeType>
    size_t count(
        ListView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        // normalize range
        size_t norm_start = normalize_index(start, view->size, true);
        size_t norm_stop = normalize_index(stop, view->size, true);
        if (norm_start > norm_stop) {
            PyErr_SetString(
                PyExc_ValueError,
                "start index must be less than or equal to stop index"
            );
            return MAX_SIZE_T;
        }

        // skip to start index
        // NOTE: we don't need to check for NULLs due to normalize_index()
        NodeType* curr = view->head;
        size_t idx = 0;
        for (idx; idx < norm_start; idx++) {
            curr = curr->next;
        }

        // search until we hit stop index
        int comp;
        size_t observed = 0;
        while (idx < norm_stop) {
            // C API equivalent of the == operator
            comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
            if (comp == -1) {  // comparison raised an exception
                return MAX_SIZE_T;
            } else if (comp == 1) {  // found a match
                count++;
            }

            // advance to next node
            curr = curr->next;
            idx++;
        }

        return observed;
    }

    /* Count the number of occurrences of an item within a set. */
    template <typename NodeType>
    size_t count(
        SetView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        // normalize range
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
        if (view->search(item) == NULL) {
            return 0;
        }

        // if range includes all items, return 1
        if (norm_start == 0 && norm_stop == view->size - 1) {
            return 1;
        }

        // find index of item
        size_t idx = SinglyLinked::index(view, item);
        if (idx >= start && idx < stop) {
            return 1;
        }
        return 0;
    }

    /* Count the number of occurrences of an item within a set. */
    template <typename NodeType>
    size_t count(
        DictView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        // normalize range
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
        if (view->search(item) == NULL) {
            return 0;
        }

        // if range includes all items, return 1
        if (norm_start == 0 && norm_stop == view->size - 1) {
            return 1;
        }

        // else, find index of item
        size_t idx = index(view, item);
        if (idx >= start && idx < stop) {
            return 1;
        }
        return 0;
    }

}


namespace DoublyLinked {

    /* Count the number of occurrences of an item within the list. */
    template <template <typename> class ViewType, typename NodeType>
    size_t count(
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
            return SinglyLinked::count(view, item, start, stop);
        }

        // else, start from tail
        NodeType* curr = view->tail;
        size_t i = view->size - 1;
        for (i; i >= norm_stop; i--) {  // skip to stop index
            curr = curr->prev;
        }

        // search until we hit start index
        int comp;
        size_t observed = 0;
        while (i >= norm_start) {
            // C API equivalent of the == operator
            comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
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

    /* Count the number of occurrences of an item within the set. */
    template <typename NodeType>
    size_t count(
        SetView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        // check if item is in set
        if (view->search(item) == NULL) {
            return 0;
        }

        size_t norm_start = normalize_index(start, view->size, true);
        size_t norm_stop = normalize_index(stop, view->size, true);
        if (norm_start > norm_stop) {
            PyErr_SetString(
                PyExc_ValueError,
                "start index must be less than or equal to stop index"
            );
            return MAX_SIZE_T;
        }

        // if range includes all items, return 1
        if (norm_start == 0 && norm_stop == view->size - 1) {
            return 1;
        }

        // find index of item
        size_t idx = DoublyLinked::index(view, item);
        if (idx >= start && idx < stop) {
            return 1;
        }
        return 0;
    }

    /* Count the number of occurrences of a key within the dictionary. */
    template <typename NodeType>
    size_t count(
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
        if (view->search(item) == NULL) {
            return 0;
        }

        // if range includes all items, return 1
        if (norm_start == 0 && norm_stop == view->size - 1) {
            return 1;
        }

        // find index of item
        size_t idx = DoublyLinked::index(view, item);
        if (idx >= start && idx < stop) {
            return 1;
        }
        return 0;
    }

}


#endif // COUNT_H include guard
