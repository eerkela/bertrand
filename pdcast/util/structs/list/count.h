
// include guard prevents multiple inclusion
#ifndef COUNT_H
#define COUNT_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <node.h>  // for nodes
#include <view.h>  // for views
#include <index.h>  // for MAX_SIZE_T


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Count the number of occurrences of an item within a singly-linked set or
dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline size_t count(
    ViewType<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    using Node = typename ViewType<NodeType>::Node;

    // check if item is contained in hash table
    Node* node = view->search(item);
    if (node == NULL) {
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
        curr = (Node*)curr->next;
        idx++;
    }

    // check if index is in range
    if (idx >= start && idx < stop) {
        return 1;
    }
    return 0;
}


/* Count the number of occurrences of an item within a singly-linked list. */
template <typename NodeType>
inline size_t count(
    ListView<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    using Node = typename ListView<NodeType>::Node;
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
                curr = (Node*)curr->prev;
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
                curr = (Node*)curr->prev;
                idx--;
            }

            return observed;
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
        if (comp == -1) {  // comparison raised an exception
            return MAX_SIZE_T;
        } else if (comp == 1) {  // found a match
            observed++;
        }

        // advance to next node
        curr = (Node*)curr->next;
        idx++;
    }

    return observed;
}


////////////////////////
////    WRAPPERS    ////
////////////////////////


// NOTE: Cython doesn't play well with nested templates, so we need to
// explicitly instantiate specializations for each combination of node/view
// type.  This is a bit of a pain, put it's the only way to get Cython to
// properly recognize the functions.

// Maybe in a future release we won't have to do this:


template size_t count(
    ListView<SingleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count(
    SetView<SingleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count(
    DictView<SingleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count(
    ListView<DoubleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count(
    SetView<DoubleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count(
    DictView<DoubleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);


#endif // COUNT_H include guard
