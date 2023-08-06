// include guard prevents multiple inclusion
#ifndef REMOVE_H
#define REMOVE_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <node.h>  // for node definitions
#include <view.h>  // for views


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Remove the first occurrence of an item within a list. */
template <typename NodeType>
inline void remove(ListView<NodeType>* view, PyObject* item) {
    // find the node to remove
    NodeType* prev = NULL;
    NodeType* curr = view->head;
    NodeType* next;
    int comp;
    while (curr != NULL) {
        next = curr->next;

        // C API equivalent of the == operator
        comp = PyObject_RichCompareBool(curr->value, item, Py_EQ);
        if (comp == -1) {  // comparison raised an exception
            return;
        } else if (comp == 1) {  // found a match
            view->unlink(prev, curr, next);
            view->deallocate(curr);
            return;
        }

        // advance to next node
        prev = curr;
        curr = next;
    }

    // item not found
    PyErr_Format(PyExc_ValueError, "%R not in list", item);
}


/* Remove an item from a singly-linked set. */
template <typename NodeType>
inline void remove_single(SetView<NodeType>* view, PyObject* item) {
    // search for node
    Hashed<NodeType>* node = view->search(item);
    if (node == NULL) {  // item not found
        PyErr_Format(PyExc_KeyError, "%R not in set", item);
        return;
    }

    _set_remove_single(view, node);
}


/* Remove a key from a singly-linked dictionary. */
template <typename NodeType>
inline void remove_single(DictView<NodeType>* view, PyObject* item) {
    // search for node
    Mapped<NodeType>* node = view->search(item);
    if (node == NULL) {  // item not found
        PyErr_Format(PyExc_KeyError, "%R not in set", item);
        return;
    }

    _set_remove_single(view, node);
}


/* Remove an item from a doubly-linked set. */
template <typename NodeType>
inline void remove_double(SetView<NodeType>* view, PyObject* item) {
    // search for node
    Hashed<NodeType>* node = view->search(item);
    if (node == NULL) {  // item not found
        PyErr_Format(PyExc_KeyError, "%R not in set", item);
        return;
    }

    _set_remove_double(view, node);
}


/* Remove a key from a doubly-linked dictionary. */
template <typename NodeType>
inline void remove_double(DictView<NodeType>* view, PyObject* item) {
    // search for node
    Mapped<NodeType>* node = view->search(item);
    if (node == NULL) {  // item not found
        PyErr_Format(PyExc_KeyError, "%R not in set", item);
        return;
    }

    _set_remove_double(view, node);
}


/* Remove an item from a set immediately after the specified sentinel value. */
template <typename NodeType>
inline void removeafter(SetView<NodeType>* view, PyObject* sentinel) {
    // search for node
    Hashed<NodeType>* node = view->search(sentinel);
    if (node == NULL) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R not in set", sentinel);
        return;
    }

    _remove_next(view, node);
}


/* Remove an key from a dictionary immediately after the specified sentinel value. */
template <typename NodeType>
inline void removeafter(DictView<NodeType>* view, PyObject* sentinel) {
    // search for node
    Mapped<NodeType>* node = view->search(sentinel);
    if (node == NULL) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R not in set", sentinel);
        return;
    }

    _remove_next(view, node);
}


/* Remove an item from a singly-linked set immediately before the specified sentinel
value. */
template <typename NodeType>
inline void removebefore_single(SetView<NodeType>* view, PyObject* sentinel) {
    // search for node
    Hashed<NodeType>* node = view->search(sentinel);
    if (node == NULL) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R not in set", sentinel);
        return;
    }

    _remove_prev_single(view, node);
}


/* Remove an item from a singly-linked dictionary immediately before the specified
sentinel value. */
template <typename NodeType>
inline void removebefore_single(DictView<NodeType>* view, PyObject* sentinel) {
    // search for node
    Mapped<NodeType>* node = view->search(sentinel);
    if (node == NULL) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R not in set", sentinel);
        return;
    }

    _remove_prev_single(view, node);
}


/* Remove an item from a doubly-linked set immediately before the specified sentinel
value. */
template <typename NodeType>
inline void removebefore_double(SetView<NodeType>* view, PyObject* sentinel) {
    // search for node
    Hashed<NodeType>* node = view->search(sentinel);
    if (node == NULL) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R not in set", sentinel);
        return;
    }

    _remove_prev_double(view, node);
}


/* Remove an item from a doubly-linked dictionary immediately before the specified
sentinel value. */
template <typename NodeType>
inline void removebefore_double(DictView<NodeType>* view, PyObject* sentinel) {
    // search for node
    Mapped<NodeType>* node = view->search(sentinel);
    if (node == NULL) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R not in set", sentinel);
        return;
    }

    _remove_prev_double(view, node);
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Remove the node immediately after a given sentinel. */
template <template <typename> class ViewType, typename T, typename U>
inline void _remove_next(ViewType<T>* view, U* node) {
    // get neighboring nodes
    U* curr = (U*)node->next;
    if (curr == NULL) {
        PyErr_SetString(PyExc_IndexError, "node is tail of list");
        return;
    }

    // unlink and free node
    view->unlink(node, curr, (U*)curr->next);
    view->deallocate(curr);
}


/* Remove the node immediately before a doubly-linked sentinel node. */
template <template <typename> class ViewType, typename T, typename U>
inline void _remove_prev_double(ViewType<T>* view, U* node) {
    // get neighboring nodes
    U* curr = (U*)node->prev;
    if (curr == NULL) {
        PyErr_SetString(PyExc_IndexError, "node is head of list");
        return;
    }

    // unlink and free node
    view->unlink((U*)curr->prev, curr, node);
    view->deallocate(curr);
}


/* Remove the node immediately before a singly-linked sentinel node. */
template <template <typename> class ViewType, typename T, typename U>
inline void _remove_prev_single(ViewType<T>* view, U* node) {
    // iterate from head to find previous node
    U* curr = view->head;
    if (curr == node) {
        PyErr_SetString(PyExc_IndexError, "node is head of list");
        return;
    }

    U* prev = NULL;
    U* next = curr->next;
    while (next != node) {
        prev = curr;
        curr = next;
        next = (U*)next->next;
    }

    // unlink and free node
    view->unlink(prev, curr, next);
    view->deallocate(curr);
}


/* Remove a node from a doubly-linked set. */
template <template <typename> class ViewType, typename T, typename U>
inline void _set_remove_double(ViewType<T>* view, U* node) {
    // get neighboring nodes
    U* prev = (U*)node->prev;
    U* next = (U*)node->next;

    // unlink and free node
    view->unlink(prev, node, next);
    view->deallocate(node);
}


/* Remove a node from a singly-linked set. */
template <template <typename> class ViewType, typename T, typename U>
inline void _set_remove_single(ViewType<T>* view, U* node) {
    // iterate from head to find previous node
    U* prev = NULL;
    U* curr = view->head;
    while (curr != node) {
        prev = curr;
        curr = (U*)curr->next;
    }

    // unlink and free node
    view->unlink(prev, curr, (U*)curr->next);
    view->deallocate(curr);
}


#endif  // REMOVE_H include guard
