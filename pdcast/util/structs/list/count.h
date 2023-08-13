
// include guard prevents multiple inclusion
#ifndef COUNT_H
#define COUNT_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include "node.h"  // for nodes
#include "view.h"  // for views


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Count the number of occurrences of an item within a singly-linked set or
dictionary. */
template <template <typename> class ViewType, typename NodeType, typename Allocator>
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

    // if range includes all items, return 1
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


/* Count the number of occurrences of an item within a singly-linked list. */
template <typename NodeType, typename Allocator>
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


///////////////////////
////    ALIASES    ////
///////////////////////


// NOTE: Cython doesn't play well with heavily templated functions, so we need
// to explicitly instantiate the specializations we need.  Maybe in a future
// release we won't have to do this:


// direct allocation
template size_t count(
    ListView<SingleNode, DirectAllocator>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count(
    SetView<SingleNode, DirectAllocator>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count(
    DictView<SingleNode, DirectAllocator>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count(
    ListView<DoubleNode, DirectAllocator>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count(
    SetView<DoubleNode, DirectAllocator>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count(
    DictView<DoubleNode, DirectAllocator>* view,
    PyObject* item,
    size_t start,
    size_t stop
);


// freelist allocation
template size_t count(
    ListView<SingleNode, FreeListAllocator>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count(
    SetView<SingleNode, FreeListAllocator>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count(
    DictView<SingleNode, FreeListAllocator>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count(
    ListView<DoubleNode, FreeListAllocator>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count(
    SetView<DoubleNode, FreeListAllocator>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count(
    DictView<DoubleNode, FreeListAllocator>* view,
    PyObject* item,
    size_t start,
    size_t stop
);


#endif // COUNT_H include guard
