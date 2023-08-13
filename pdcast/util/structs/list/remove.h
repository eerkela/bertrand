// include guard prevents multiple inclusion
#ifndef REMOVE_H
#define REMOVE_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include "node.h"  // for nodes
#include "view.h"  // for views


//////////////////////
////    REMOVE    ////
//////////////////////


/* Remove an item from a linked set or dictionary. */
template <template <typename> class ViewType, typename NodeType>
void remove(ViewType<NodeType>* view, PyObject* item) {
    using Node = typename ViewType<NodeType>::Node;

    // search for node
    Node* curr = view->search(item);
    if (curr == nullptr) {  // item not found
        PyErr_Format(PyExc_KeyError, "%R not in set", item);
        return;
    }

    // get previous node
    Node* prev;
    if constexpr (is_doubly_linked<Node>::value) {
        // NOTE: this is O(1) for doubly-linked sets and dictionaries because
        // we already have a pointer to the previous node.
        prev = static_cast<Node*>(curr->prev);
    } else {
        // NOTE: this is O(n) for singly-linked sets and dictionaries because we
        // have to traverse the whole list to find the previous node.
        prev = nullptr;
        Node* temp = view->head;
        while (temp != curr) {
            prev = temp;
            temp = static_cast<Node*>(temp->next);
        }
    }

    // unlink and free node
    view->unlink(prev, curr, static_cast<Node*>(curr->next));
    view->recycle(curr);
}


/* Remove the first occurrence of an item within a list. */
template <typename NodeType>
void remove(ListView<NodeType>* view, PyObject* item) {
    using Node = typename ListView<NodeType>::Node;

    // find the node to remove
    Node* prev = nullptr;
    Node* curr = view->head;
    while (curr != nullptr) {
        Node* next = static_cast<Node*>(curr->next);

        // C API equivalent of the == operator
        int comp = PyObject_RichCompareBool(curr->value, item, Py_EQ);
        if (comp == -1) {  // comparison raised an exception
            return;
        } else if (comp == 1) {  // found a match
            view->unlink(prev, curr, next);
            view->recycle(curr);
            return;
        }

        // advance to next node
        prev = curr;
        curr = next;
    }

    // item not found
    PyErr_Format(PyExc_ValueError, "%R not in list", item);
}


/* Remove an item from a linked set or dictionary if it is present. */
template <template <typename> class ViewType, typename NodeType>
void discard(ViewType<NodeType>* view, PyObject* item) {
    using Node = typename ViewType<NodeType>::Node;

    // search for node
    Node* curr = view->search(item);
    if (curr == nullptr) {  // item not found
        return;  // do not raise
    }

    // get previous node
    Node* prev;
    if constexpr (is_doubly_linked<Node>::value) {
        // NOTE: this is O(1) for doubly-linked sets and dictionaries because
        // we already have a pointer to the previous node.
        prev = static_cast<Node*>(curr->prev);
    } else {
        // NOTE: this is O(n) for singly-linked sets and dictionaries because we
        // have to traverse the whole list to find the previous node.
        prev = nullptr;
        Node* temp = view->head;
        while (temp != curr) {
            prev = temp;
            temp = static_cast<Node*>(temp->next);
        }
    }

    // unlink and free node
    view->unlink(prev, curr, static_cast<Node*>(curr->next));
    view->recycle(curr);
}


/* Remove an item from a set or dictionary immediately after the specified
sentinel value. */
template <template <typename> class ViewType, typename NodeType>
inline void discardafter(ViewType<NodeType>* view, PyObject* sentinel) {
    using Node = typename ViewType<NodeType>::Node;

    // search for node
    Node* prev = view->search(sentinel);
    if (prev == nullptr || prev == view->tail) {
        return;
    }

    // unlink and free node
    Node* curr = static_cast<Node*>(prev->next);
    view->unlink(prev, curr, static_cast<Node*>(curr->next));
    view->recycle(curr);
}


/* Remove an item from a singly-linked set immediately before the specified sentinel
value. */
template <template <typename> class ViewType, typename NodeType>
inline void discardbefore(ViewType<NodeType>* view, PyObject* sentinel) {
    using Node = typename ViewType<NodeType>::Node;

    // search for node
    Node* next = view->search(sentinel);
    if (next == nullptr || next == view->head) {
        return;
    }

    // get previous node
    Node* curr;
    Node* prev;
    if constexpr (is_doubly_linked<Node>::value) {
        // NOTE: this is O(1) for doubly-linked sets because we already have a
        // pointer to the previous node.
        curr = static_cast<Node*>(next->prev);
        prev = static_cast<Node*>(curr->prev);
    } else {
        // NOTE: this is O(n) for singly-linked sets because we have to traverse
        // the whole list to find the previous node.
        curr = view->head;
        prev = nullptr;
        Node* temp = static_cast<Node*>(curr->next);
        while (temp != next) {
            prev = curr;
            curr = temp;
            temp = static_cast<Node*>(temp->next);
        }
    }

    // unlink and free node
    view->unlink(prev, curr, next);
    view->recycle(curr);
}


///////////////////////
////    ALIASES    ////
///////////////////////


// NOTE: Cython doesn't play well with heavily templated functions, so we need
// to explicitly instantiate the specializations we need.  Maybe in a future
// release we won't have to do this:


template void remove(ListView<SingleNode>* view, PyObject* item);
template void remove(SetView<SingleNode>* view, PyObject* item);
template void remove(DictView<SingleNode>* view, PyObject* item);
template void remove(ListView<DoubleNode>* view, PyObject* item);
template void remove(SetView<DoubleNode>* view, PyObject* item);
template void remove(DictView<DoubleNode>* view, PyObject* item);
template void discard(SetView<SingleNode>* view, PyObject* item);
template void discard(DictView<SingleNode>* view, PyObject* item);
template void discard(SetView<DoubleNode>* view, PyObject* item);
template void discard(DictView<DoubleNode>* view, PyObject* item);
template void discardafter(SetView<SingleNode>* view, PyObject* sentinel);
template void discardafter(DictView<SingleNode>* view, PyObject* sentinel);
template void discardafter(SetView<DoubleNode>* view, PyObject* sentinel);
template void discardafter(DictView<DoubleNode>* view, PyObject* sentinel);
template void discardbefore(SetView<SingleNode>* view, PyObject* sentinel);
template void discardbefore(DictView<SingleNode>* view, PyObject* sentinel);
template void discardbefore(SetView<DoubleNode>* view, PyObject* sentinel);
template void discardbefore(DictView<DoubleNode>* view, PyObject* sentinel);


#endif  // REMOVE_H include guard
