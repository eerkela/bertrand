
// include guard prevents multiple inclusion
#ifndef POP_H
#define POP_H

#include <cstddef>  // for size_t
#include <queue>  // for std::queue
#include <Python.h>  // for CPython API
#include "node.h"  // for nodes
#include "view.h"  // for views


///////////////////
////    POP    ////
///////////////////


/* Pop an item from a linked list, set, or dictionary at the given index. */
template <template <typename> class ViewType, typename NodeType>
inline PyObject* pop(ViewType<NodeType>* view, size_t index) {
    using Node = typename ViewType<NodeType>::Node;
    Node* prev;
    Node* curr;
    Node* next;

    // NOTE: index is normalized at the Cython level and raises an out of bounds
    // error, so we don't need to worry about negative values or empty lists.

    // check for pop at head of list (O(1) in both cases)
    if (index == 0) {
        prev = nullptr;
        curr = view->head;
        next = static_cast<Node*>(curr->next);
        return _pop_node(view, prev, curr, next);
    }

    // get neighboring nodes
    if constexpr (is_doubly_linked<Node>::value) {
        // check for pop at tail of list
        if (index == view->size - 1) {
            next = nullptr;
            curr = view->tail;
            prev = static_cast<Node*>(curr->prev);  // use tail's prev pointer
            return _pop_node(view, prev, curr, next);

        // NOTE: if the list is doubly-linked, then we can iterate from either
        // end to find the neighboring nodes.
        } else if (index > view->size / 2) {
            curr = view->tail;
            for (size_t i = view->size - 1; i > index; i--) {
                curr = static_cast<Node*>(curr->prev);
            }
            prev = static_cast<Node*>(curr->prev);
            next = static_cast<Node*>(curr->next);
            return _pop_node(view, prev, curr, next);
        }
    }

    // NOTE: due to the singly-linked nature of the list, popping from the
    // front of the list is O(1) while popping from the back is O(n).  This is
    // because we need to traverse the entire list to find the previous node.
    prev = nullptr;
    curr = view->head;
    for (size_t i = 0; i < index; i++) {
        prev = curr;
        curr = static_cast<Node*>(curr->next);
    }
    next = static_cast<Node*>(curr->next);

    // recycle node and return a new reference to its value
    return _pop_node(view, prev, curr, next);
}


/* Pop a key from a linked dictionary and return its corresponding value. */
template <typename NodeType>
inline PyObject* pop(
    DictView<NodeType>* view,
    PyObject* key,
    PyObject* default_value
) {
    using Node = typename DictView<NodeType>::Node;
    Node* prev;
    Node* curr;

    // search for node
    curr = view->search(key);
    if (curr == nullptr) {
        return default_value;
    }

    // get neighboring nodes
    if constexpr (is_doubly_linked<Node>::value) {
        // NOTE: this is O(1) for doubly-linked dictionaries because we can use
        // the node's prev and next pointers to unlink it from the list.
        prev = static_cast<Node*>(curr->prev);
    } else {
        // NOTE: this is O(n) for singly-linked dictionaries because we have to
        // traverse the whole list to find the node that precedes the popped node.
        prev = nullptr;
        Node* temp = view->head;
        while (temp != curr) {
            prev = temp;
            temp = static_cast<Node*>(temp->next);
        }
    }

    // recycle node and return a new reference to its value
    return _pop_node(view, prev, curr, static_cast<Node*>(curr->next));
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Unlink and remove a node and return its value. */
template <template <typename> class ViewType, typename NodeType, typename Node>
inline PyObject* _pop_node(
    ViewType<NodeType>* view,
    Node* prev,
    Node* curr,
    Node* next
) {
    // get return value
    PyObject* value = curr->value;
    Py_INCREF(value);  // have to INCREF because we DECREF in recycle()

    // unlink and deallocate node
    view->unlink(prev, curr, next);
    view->recycle(curr);
    return value;  // caller takes ownership of value
}


///////////////////////
////    ALIASES    ////
///////////////////////


// NOTE: Cython doesn't play well with heavily templated functions, so we need
// to explicitly instantiate the specializations we need.  Maybe in a future
// release we won't have to do this:


template PyObject* pop(ListView<SingleNode>* view, size_t index);
template PyObject* pop(SetView<SingleNode>* view, size_t index);
template PyObject* pop(DictView<SingleNode>* view, size_t index);
template PyObject* pop(
    DictView<SingleNode>* view,
    PyObject* key,
    PyObject* default_value
);
template PyObject* pop(ListView<DoubleNode>* view, size_t index);
template PyObject* pop(SetView<DoubleNode>* view, size_t index);
template PyObject* pop(DictView<DoubleNode>* view, size_t index);
template PyObject* pop(
    DictView<DoubleNode>* view,
    PyObject* key,
    PyObject* default_value
);


#endif // POP_H include guard
