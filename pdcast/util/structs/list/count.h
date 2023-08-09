
// include guard prevents multiple inclusion
#ifndef COUNT_H
#define COUNT_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <node.h>  // for nodes
#include <view.h>  // for views


// NOTE: we don't need to check for NULLs due to view->normalize_index()


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Count the number of occurrences of an item within a singly-linked set or
dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline size_t count_single(
    ViewType<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    return _count_setlike(view, item, start, stop);
}


/* Count the number of occurrences of an item within a singly-linked list. */
template <typename NodeType>
inline size_t count_single(
    ListView<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    using Node = typename ListView<NodeType>::Node;

    // skip to start index
    Node* curr = view->head;
    size_t idx;
    for (idx = 0; idx < start; idx++) {
        curr = (Node*)curr->next;
    }

    // search until we hit stop index
    int comp;
    size_t observed = 0;
    while (idx < stop) {
        // C API equivalent of the == operator
        comp = PyObject_RichCompareBool(curr->value, item, Py_EQ);
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


/* Count the number of occurrences of an item within a doubly-linked set or
dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline size_t count_double(
    ViewType<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    return _count_setlike(view, item, start, stop);
}


/* Count the number of occurrences of an item within a doubly-linked list. */
template <typename NodeType>
inline size_t count_double(
    ListView<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    using Node = typename ListView<NodeType>::Node;

    // if starting index is closer to head, use singly-linked version
    if (start <= view->size / 2) {
        return count_single(view, item, start, stop);
    }

    // else, start from tail
    Node* curr = view->tail;
    size_t idx;
    for (idx = view->size - 1; idx >= stop; idx--) {  // skip to stop index
        curr = (Node*)curr->prev;
    }

    // search until we hit start index
    int comp;
    size_t observed = 0;
    while (idx >= start) {
        // C API equivalent of the == operator
        comp = PyObject_RichCompareBool(curr->value, item, Py_EQ);
        if (comp == -1) {  // comparison raised an exception
            return MAX_SIZE_T;
        } else if (comp == 1) {  // found a match
            observed++;
        }

        // advance to next node
        curr = (Node*)curr->prev;
        idx--;
    }

    // return final count
    return observed;
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Count the number of occurrences of an item within a set-like list. */
template <template <typename> class ViewType, typename NodeType>
inline size_t _count_setlike(
    ViewType<NodeType>* view,
    PyObject* item,
    size_t start,
    size_t stop
) {
    using Node = typename ViewType<NodeType>::Node;

    // check if item is contained in set
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


////////////////////////
////    WRAPPERS    ////
////////////////////////


// NOTE: Cython doesn't play well with nested templates, so we need to
// explicitly instantiate specializations for each combination of node/view
// type.  This is a bit of a pain, put it's the only way to get Cython to
// properly recognize the functions.

// Maybe in a future release we won't have to do this:


template size_t count_single(
    ListView<SingleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count_single(
    SetView<SingleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count_single(
    DictView<SingleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count_single(
    ListView<DoubleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count_single(
    SetView<DoubleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count_single(
    DictView<DoubleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count_double(
    ListView<DoubleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count_double(
    SetView<DoubleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);
template size_t count_double(
    DictView<DoubleNode>* view,
    PyObject* item,
    size_t start,
    size_t stop
);


#endif // COUNT_H include guard
