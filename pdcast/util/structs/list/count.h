
// include guard prevents multiple inclusion
#ifndef COUNT_H
#define COUNT_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <node.h>  // for node definitions
#include <view.h>  // for view definitions


// NOTE: we don't need to check for NULLs due to view->normalize_index()


namespace SinglyLinked {

    /* Count the number of occurrences of an item within a list. */
    template <typename NodeType>
    size_t count(
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
        size_t start,
        size_t stop
    ) {
        // check if item is in set
        Hashed<NodeType>* node = view->search(item);
        if (node == NULL) {
            return 0;
        }

        // if range includes all items, return 1
        if (start == 0 && stop == view->size - 1) {
            return 1;
        }

        // else, find index of item
        Mapped<NodeType>* curr = view->head;
        size_t idx = 0;
        while (curr != node && idx < stop) {
            curr = (Mapped<NodeType>*)curr->next;
            idx++;
        }

        // check if index is in range
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
        size_t start,
        size_t stop
    ) {
        // check if item is in set
        Mapped<NodeType>* node = view->search(item);
        if (node == NULL) {
            return 0;
        }

        // if range includes all items, return 1
        if (start == 0 && stop == view->size - 1) {
            return 1;
        }

        // else, find index of item
        Mapped<NodeType>* curr = view->head;
        size_t idx = 0;
        while (curr != node && idx < stop) {
            curr = (Mapped<NodeType>*)curr->next;
            idx++;
        }

        // check if index is in range
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
        size_t start,
        size_t stop
    ) {
        // if starting index is closer to head, use singly-linked version
        if (start <= view->size / 2) {
            return SinglyLinked::count(view, item, start, stop);
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
        size_t start,
        size_t stop
    ) {
        // check if item is in set
        Hashed<NodeType>* node = view->search(item);
        if (node == NULL) {
            return 0;
        }

        // if range includes all items, return 1
        if (start == 0 && stop == view->size - 1) {
            return 1;
        }

        // else, count backwards to find index
        size_t idx = 0;
        while (node != NULL && idx < stop) {
            node = (Mapped<NodeType>*)node->prev;
            idx++;
        }

        // check if index is in range
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
        size_t start,
        size_t stop
    ) {
        // check if item is in set
        Mapped<NodeType>* node = view->search(item);
        if (node == NULL) {
            return 0;
        }

        // if range includes all items, return 1
        if (start == 0 && stop == view->size - 1) {
            return 1;
        }

        // else, count backwards to find index
        size_t idx = 0;
        while (node != NULL && idx < stop) {
            node = (Mapped<NodeType>*)node->prev;
            idx++;
        }

        // check if index is in range
        if (idx >= start && idx < stop) {
            return 1;
        }
        return 0;
    }

}


#endif // COUNT_H include guard
