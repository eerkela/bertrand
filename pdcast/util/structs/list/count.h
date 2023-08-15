
// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_COUNT_H
#define BERTRAND_STRUCTS_ALGORITHMS_COUNT_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include "node.h"  // for nodes
#include "view.h"  // for views


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    /* Count the number of occurrences of an item within a linked set or dictionary. */
    template<
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    size_t count(
        ViewType<NodeType, Allocator>* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) {
        using Node = typename ViewType<NodeType, Allocator>::Node;

        // check if item is contained in hash table
        Node* node = view->search(item);
        if (node == nullptr) {
            return 0;
        }

        // if range includes all items, return true
        if (start == 0 && stop == view->size - 1) {
            return 1;
        }

        // else, find index of item
        Node* curr = view->head;
        size_t idx = 0;
        while (curr != node && idx < stop) {
            curr = static_cast<Node*>(curr->next);
            idx++;
        }

        // check if index is in range
        if (idx >= start && idx < stop) {
            return 1;
        }
        return 0;
    }

    /* Count the number of occurrences of an item within a linked list. */
    template <typename NodeType, template <typename> class Allocator>
    size_t count(
        ListView<NodeType, Allocator>* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) {
        using Node = typename ListView<NodeType, Allocator>::Node;
        size_t observed = 0;
        Node* curr;
        size_t idx;

        // NOTE: if start index is closer to tail and the list is doubly-linked,
        // we can iterate from the tail to save time.
        if constexpr (is_doubly_linked<Node>::value) {
            if (start > view->size / 2) {
                // else, start from tail
                curr = view->tail;
                for (idx = view->size - 1; idx >= stop; idx--) {  // skip to stop index
                    curr = static_cast<Node*>(curr->prev);
                }

                // search until we hit start index
                while (idx >= start) {
                    // C API equivalent of the == operator
                    int comp = PyObject_RichCompareBool(curr->value, item, Py_EQ);
                    if (comp == -1) {  // comparison raised an exception
                        return MAX_SIZE_T;
                    } else if (comp == 1) {  // found a match
                        observed++;
                    }

                    // advance to next node
                    curr = static_cast<Node*>(curr->prev);
                    idx--;
                }

                return observed;
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
            if (comp == -1) {  // comparison raised an exception
                return MAX_SIZE_T;
            } else if (comp == 1) {  // found a match
                observed++;
            }

            // advance to next node
            curr = static_cast<Node*>(curr->next);
            idx++;
        }

        return observed;
    }

}


#endif // BERTRAND_STRUCTS_ALGORITHMS_COUNT_H include guard
