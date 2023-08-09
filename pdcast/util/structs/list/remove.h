// include guard prevents multiple inclusion
#ifndef REMOVE_H
#define REMOVE_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <node.h>  // for nodes
#include <view.h>  // for views


//////////////////////
////    REMOVE    ////
//////////////////////


/* Remove the first occurrence of an item within a list. */
template <typename NodeType>
inline void remove(ListView<NodeType>* view, PyObject* item) {
    using Node = typename ListView<NodeType>::Node;

    // find the node to remove
    Node* prev = NULL;
    Node* curr = view->head;
    while (curr != NULL) {
        Node* next = (Node*)curr->next;

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


/* Remove an item from a singly-linked set or dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline void remove_single(ViewType<NodeType>* view, PyObject* item) {
    using Node = typename ViewType<NodeType>::Node;

    // search for node
    Node* node = view->search(item);
    if (node == NULL) {  // item not found
        PyErr_Format(PyExc_KeyError, "%R not in set", item);
        return;
    }

    // NOTE: this is O(n) for singly-linked sets and dictionaries because we
    // have to traverse the whole list to find the previous node.

    // iterate forwards from head to find previous node
    Node* prev = NULL;
    Node* curr = view->head;
    while (curr != node) {
        prev = curr;
        curr = (Node*)curr->next;
    }

    // unlink and free node
    view->unlink(prev, curr, (Node*)curr->next);
    view->recycle(curr);
}


/* Remove an item from a doubly-linked set or dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline void remove_double(ViewType<NodeType>* view, PyObject* item) {
    using Node = typename ViewType<NodeType>::Node;

    // search for node
    Node* node = view->search(item);
    if (node == NULL) {  // item not found
        PyErr_Format(PyExc_KeyError, "%R not in set", item);
        return;
    }

    // NOTE: this is O(1) for doubly-linked sets and dictionaries because we
    // already have a pointer to the node that precedes the popped node.

    // unlink and free node
    view->unlink((Node*)node->prev, node, (Node*)node->next);
    view->recycle(node);
}


///////////////////////////
////    REMOVEAFTER    ////
///////////////////////////


/* Remove an item from a set or dictionary immediately after the specified
sentinel value. */
template <template <typename> class ViewType, typename NodeType>
inline void removeafter(ViewType<NodeType>* view, PyObject* sentinel) {
    using Node = typename ViewType<NodeType>::Node;

    // search for node
    Node* node = view->search(sentinel);
    if (node == NULL) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R not in set", sentinel);
        return;
    }
    if (node == view->tail) {
        PyErr_SetString(PyExc_IndexError, "node is tail of list");
        return;
    }

    // unlink and free node
    Node* curr = (Node*)node->next;
    view->unlink(node, curr, (Node*)curr->next);
    view->recycle(curr);
}


////////////////////////////
////    REMOVEBEFORE    ////
////////////////////////////


/* Remove an item from a singly-linked set immediately before the specified sentinel
value. */
template <template <typename> class ViewType, typename NodeType>
inline void removebefore_single(ViewType<NodeType>* view, PyObject* sentinel) {
    using Node = typename ViewType<NodeType>::Node;

    // search for node
    Node* node = view->search(sentinel);
    if (node == NULL) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R not in set", sentinel);
        return;
    }
    if (node == view->head) {
        PyErr_SetString(PyExc_IndexError, "node is head of list");
        return;
    }

    // NOTE: this is O(n) for singly-linked sets because we have to traverse
    // the whole list to find the previous node.

    // iterate from head to find previous node
    Node* prev = NULL;
    Node* curr = view->head;
    Node* next = (Node*)curr->next;
    while (next != node) {
        prev = curr;
        curr = next;
        next = (Node*)next->next;
    }

    // unlink and free node
    view->unlink(prev, curr, next);
    view->recycle(curr);
}


/* Remove an item from a doubly-linked set immediately before the specified sentinel
value. */
template <template <typename> class ViewType, typename NodeType>
inline void removebefore_double(ViewType<NodeType>* view, PyObject* sentinel) {
    using Node = typename ViewType<NodeType>::Node;

    // search for node
    Node* node = view->search(sentinel);
    if (node == NULL) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R not in set", sentinel);
        return;
    }
    if (node == view->head) {
        PyErr_SetString(PyExc_IndexError, "node is head of list");
        return;
    }

    // NOTE: this is O(1) for doubly-linked sets because we already have a
    // pointer to the node that precedes the sentinel.

    // unlink and free node
    Node* curr = (Node*)node->prev;
    view->unlink((Node*)curr->prev, curr, node);
    view->recycle(curr);
}


////////////////////////
////    WRAPPERS    ////
////////////////////////


// NOTE: Cython doesn't play well with nested templates, so we need to
// explicitly instantiate specializations for each combination of node/view
// type.  This is a bit of a pain, put it's the only way to get Cython to
// properly recognize the functions.

// Maybe in a future release we won't have to do this:


template void remove(ListView<SingleNode>* view, PyObject* item);
template void remove(ListView<DoubleNode>* view, PyObject* item);
template void remove_single(SetView<SingleNode>* view, PyObject* item);
template void remove_single(DictView<SingleNode>* view, PyObject* item);
template void remove_single(SetView<DoubleNode>* view, PyObject* item);
template void remove_single(DictView<DoubleNode>* view, PyObject* item);
template void remove_double(SetView<DoubleNode>* view, PyObject* item);
template void remove_double(DictView<DoubleNode>* view, PyObject* item);
template void removeafter(SetView<SingleNode>* view, PyObject* sentinel);
template void removeafter(DictView<SingleNode>* view, PyObject* sentinel);
template void removeafter(SetView<DoubleNode>* view, PyObject* sentinel);
template void removeafter(DictView<DoubleNode>* view, PyObject* sentinel);
template void removebefore_single(SetView<SingleNode>* view, PyObject* sentinel);
template void removebefore_single(DictView<SingleNode>* view, PyObject* sentinel);
template void removebefore_single(SetView<DoubleNode>* view, PyObject* sentinel);
template void removebefore_single(DictView<DoubleNode>* view, PyObject* sentinel);
template void removebefore_double(SetView<DoubleNode>* view, PyObject* sentinel);
template void removebefore_double(DictView<DoubleNode>* view, PyObject* sentinel);


#endif  // REMOVE_H include guard
