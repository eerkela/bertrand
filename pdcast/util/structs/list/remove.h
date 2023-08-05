// include guard prevents multiple inclusion
#ifndef REMOVE_H
#define REMOVE_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <node.h>  // for node definitions
#include <view.h>  // for views


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


/* Remove an item from a set immediately after the specified sentinel value. */
template <typename NodeType>
inline void removeafter(SetView<NodeType>* view, PyObject* sentinel) {
    // search for node
    Hashed<NodeType>* prev = view->search(sentinel);
    if (node == NULL) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R not in set", sentinel);
        return;
    }

    // get neighboring nodes
    Hashed<NodeType>* curr = (Hashed<NodeType>*)prev->next;
    Hashed<NodeType>* next;
    if (curr == NULL) {  // sentinel is the last node
        next = NULL;
    } else {
        next = (Hashed<NodeType>*)curr->next;
    }

    // unlink and free node
    view->unlink(prev, curr, next);
    view->deallocate(curr);
}


/* Remove an key from a dictionary immediately after the specified sentinel value. */
template <typename NodeType>
inline void removeafter(DictView<NodeType>* view, PyObject* sentinel) {
    // search for node
    Mapped<NodeType>* prev = view->search(sentinel);
    if (node == NULL) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R not in set", sentinel);
        return;
    }

    // get neighboring nodes
    Mapped<NodeType>* curr = (Mapped<NodeType>*)prev->next;
    Mapped<NodeType>* next;
    if (curr == NULL) {  // sentinel is the last node
        next = NULL;
    } else {
        next = (Mapped<NodeType>*)curr->next;
    }

    // unlink and free node
    view->unlink(prev, curr, next);
    view->deallocate(curr);
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

    // iterate from head to find previous node
    Hashed<NodeType>* prev = NULL;
    Hashed<NodeType>* curr = view->head;
    while (curr != node) {
        prev = curr;
        curr = (Hashed<NodeType>*)curr->next;
    }

    // unlink and free node
    Hashed<NodeType>* next = (Hashed<NodeType>*)curr->next;
    view->unlink(prev, curr, next);
    view->deallocate(curr);
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

    // iterate from head to find previous node
    Mapped<NodeType>* prev = NULL;
    Mapped<NodeType>* curr = view->head;
    while (curr != node) {
        prev = curr;
        curr = (Mapped<NodeType>*)curr->next;
    }

    // unlink and free node
    Mapped<NodeType>* next = (Mapped<NodeType>*)curr->next;
    view->unlink(prev, curr, next);
    view->deallocate(curr);
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

    // check if node is head of list
    if (node == view->head) {
        return;  // do nothing
    }

    // iterate from head to find previous node
    Hashed<NodeType>* prev = NULL;
    Hashed<NodeType>* curr = view->head;
    Hashed<NodeType>* next = curr->next;
    while (next != node) {
        prev = curr;
        curr = next;
        next = (Hashed<NodeType>*)next->next;
    }

    // unlink and free node
    view->unlink(prev, curr, next);
    view->deallocate(curr);
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

    // check if node is head of list
    if (node == view->head) {
        return;  // do nothing
    }

    // iterate from head to find previous node
    Mapped<NodeType>* prev = NULL;
    Mapped<NodeType>* curr = view->head;
    Mapped<NodeType>* next = curr->next;
    while (next != node) {
        prev = curr;
        curr = next;
        next = (Mapped<NodeType>*)next->next;
    }

    // unlink and free node
    view->unlink(prev, curr, next);
    view->deallocate(curr);
}


/* Remove an item from a doubly-linked set. */
template <typename NodeType>
inline void remove_double(SetView<NodeType>* view, PyObject* item) {
    // search for node
    Hashed<NodeType>* curr = view->search(item);
    if (node == NULL) {  // item not found
        PyErr_Format(PyExc_KeyError, "%R not in set", item);
        return;
    }

    // use prev to find previous node
    Hashed<NodeType>* prev = (Hashed<NodeType>*)curr->prev;
    Hashed<NodeType>* next = (Hashed<NodeType>*)curr->next;

    // unlink and free node
    view->unlink(prev, curr, next);
    view->deallocate(curr);
}


/* Remove a key from a doubly-linked dictionary. */
template <typename NodeType>
inline void remove_double(DictView<NodeType>* view, PyObject* item) {
    // search for node
    Mapped<NodeType>* curr = view->search(item);
    if (node == NULL) {  // item not found
        PyErr_Format(PyExc_KeyError, "%R not in set", item);
        return;
    }

    // use prev to find previous node
    Mapped<NodeType>* prev = (Mapped<NodeType>*)curr->prev;
    Mapped<NodeType>* next = (Mapped<NodeType>*)curr->next;

    // unlink and free node
    view->unlink(prev, curr, next);
    view->deallocate(curr);
}


/* Remove an item from a doubly-linked set immediately before the specified sentinel
value. */
template <typename NodeType>
inline void removebefore_double(SetView<NodeType>* view, PyObject* sentinel) {
    // search for node
    Hashed<NodeType>* next = view->search(sentinel);
    if (next == NULL) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R not in set", sentinel);
        return;
    }

    // check if node is head of list
    if (next == view->head) {
        return;  // do nothing
    }

    // use prev to find previous node
    Hashed<NodeType>* curr = (Hashed<NodeType>*)next->prev;
    Hashed<NodeType>* prev = (Hashed<NodeType>*)curr->prev;

    // unlink and free node
    view->unlink(prev, curr, next);
    view->deallocate(curr);
}


/* Remove an item from a doubly-linked dictionary immediately before the specified
sentinel value. */
template <typename NodeType>
inline void removebefore_double(DictView<NodeType>* view, PyObject* sentinel) {
    // search for node
    Mapped<NodeType>* next = view->search(sentinel);
    if (next == NULL) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R not in set", sentinel);
        return;
    }

    // check if node is head of list
    if (next == view->head) {
        return;  // do nothing
    }

    // use prev to find previous node
    Mapped<NodeType>* curr = (Mapped<NodeType>*)next->prev;
    Mapped<NodeType>* prev = (Mapped<NodeType>*)curr->prev;

    // unlink and free node
    view->unlink(prev, curr, next);
    view->deallocate(curr);
}


#endif  // REMOVE_H include guard
