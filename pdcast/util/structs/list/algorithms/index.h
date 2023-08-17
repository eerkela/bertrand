// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_INDEX_H
#define BERTRAND_STRUCTS_ALGORITHMS_INDEX_H

#include <cstddef>  // size_t
#include <Python.h>  // CPython API
#include "../core/node.h"  // is_doubly_linked<>
#include "../core/view.h"  // views, MAX_SIZE_T


// TODO: add a list.edge(a, b, compare="<") method for sets and dictionaries
// that checks whether A comes before or after B in the list.


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    /* Get the index of an item within a linked set or dictionary. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    size_t index(
        ViewType<NodeType, Allocator>* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) {
        using Node = typename ViewType<NodeType, Allocator>::Node;

        // search for item in hash table
        Node* node = view->search(item);
        if (node == nullptr) {
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
            curr = static_cast<Node*>(curr->next);
        }

        // iterate until we hit match or stop index
        while (curr != node && idx < stop) {
            curr = static_cast<Node*>(curr->next);
            idx++;
        }
        if (curr == node) {
            return idx;
        }

        // item exists, but comes after range
        PyErr_Format(PyExc_ValueError, "%R is not in the set", item);
        return MAX_SIZE_T;
    }

    /* Get the index of an item within a linked list. */
    template <typename NodeType, template <typename> class Allocator>
    size_t index(
        ListView<NodeType, Allocator>* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) {
        using Node = typename ListView<NodeType, Allocator>::Node;
        Node* curr;
        size_t idx;

        // NOTE: if start index is closer to tail and the list is doubly-linked,
        // we can iterate from the tail to save time.
        if constexpr (is_doubly_linked<Node>::value) {
            if (start > view->size / 2) {
                curr = view->tail;
                for (idx = view->size - 1; idx > stop; idx--) {  // skip to stop index
                    curr = static_cast<Node*>(curr->prev);
                }

                // search until we hit start index
                bool found = false;
                size_t last_observed;
                while (idx >= start) {
                    // C API equivalent of the == operator
                    int comp = PyObject_RichCompareBool(item, curr->value, Py_EQ);
                    if (comp == -1) {  // comparison raised an exception
                        return MAX_SIZE_T;
                    } else if (comp == 1) {  // found a match
                        last_observed = idx;
                        found = true;
                    }

                    // advance to next node
                    curr = static_cast<Node*>(curr->prev);
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
            curr = static_cast<Node*>(curr->next);
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
            curr = static_cast<Node*>(curr->next);
            idx++;
        }

        // item not found
        PyErr_Format(PyExc_ValueError, "%R is not in list", item);
        return MAX_SIZE_T;
    }

}


#endif // BERTRAND_STRUCTS_ALGORITHMS_INDEX_H include guard
