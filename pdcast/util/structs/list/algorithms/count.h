// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_COUNT_H
#define BERTRAND_STRUCTS_ALGORITHMS_COUNT_H

#include <cstddef>  // size_t
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/node.h"  // is_doubly_linked<>
#include "../core/view.h"  // views, MAX_SIZE_T


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    /* Count the number of occurrences of an item within a linked set or dictionary. */
    template <typename View, typename T>
    size_t count(View* view, PyObject* item, T start, T stop) {
        using Node = typename View::Node;

        // allow Python-style negative indexing + boundschecking
        std::pair<size_t, size_t> bounds = normalize_bounds(
            start, stop, view->size, true
        );

        // check if item is contained in hash table
        Node* node = view->search(item);
        if (node == nullptr) {
            return 0;
        }

        // if range includes all items, return true
        if (bounds.first == 0 && bounds.second == view->size - 1) {
            return 1;
        }

        // else, find index of item
        Node* curr = view->head;
        size_t idx = 0;
        while (curr != node && idx < bounds.second) {
            curr = static_cast<Node*>(curr->next);
            idx++;
        }

        // check if index is in range
        if (idx >= bounds.first && idx < bounds.second) {
            return 1;
        }
        return 0;
    }

    /* Count the number of occurrences of an item within a linked list. */
    template <typename NodeType, template <typename> class Allocator, typename T>
    size_t count(
        ListView<NodeType, Allocator>* view,
        PyObject* item,
        T start,
        T stop
    ) {
        using Node = typename ListView<NodeType, Allocator>::Node;
        size_t observed = 0;
        Node* curr;
        size_t idx;

        // allow Python-style negative indexing + boundschecking
        std::pair<size_t, size_t> bounds = normalize_bounds(
            start, stop, view->size, true
        );

        // NOTE: if start index is closer to tail and the list is doubly-linked,
        // we can iterate from the tail to save time.
        if constexpr (is_doubly_linked<Node>::value) {
            if (bounds.first > view->size / 2) {
                // else, start from tail
                curr = view->tail;

                // skip to stop index
                for (idx = view->size - 1; idx >= bounds.second; idx--) {
                    curr = static_cast<Node*>(curr->prev);
                }

                // search until we hit start index
                while (idx >= bounds.first) {
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
        for (idx = 0; idx < bounds.first; idx++) {  // skip to start index
            curr = static_cast<Node*>(curr->next);
        }

        // search until we hit stop index
        while (idx < bounds.second) {
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
