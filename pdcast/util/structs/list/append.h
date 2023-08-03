// include guard prevents multiple inclusion
#ifndef APPEND_H
#define APPEND_H

/* This module contains various `append()`-related utilities for adding elements
to linked list-based data structures.  The Cython wrappers for these functions
are declared in single.pyx, double.pyx, hashed.pyx, and dict.pyx. */

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <node.h>  // for node definitions, views, etc.


// TODO: can't template the ViewType since they each use different node types.
// They each need their own version of each function.

/* Add an item to the end of the list. */
template <template<typename> class ViewType, typename NodeType>
inline void append(ViewType<NodeType> view, PyObject* item) {
    NodeType* node = view->allocate(item);  // allocate a new node
    if (node == NULL) {
        return;
    }
    view->link(view->head, node, NULL);  // link to tail
}


/* Add an item to the end of the dictionary. */
template <typename NodeType>
inline void append(DictView<NodeType> view, PyObject* item, PyObject* mapped) {
    NodeType* node = view->allocate(item, mapped);  // allocate a new node
    if (node == NULL) {
        return;
    }
    view->link(view->head, node, NULL);  // link to tail
}


/* Add an item to the beginning of the list. */
template <template<typename> class ViewType, typename NodeType>
inline void appendleft(ViewType<NodeType> view, PyObject* item) {
    NodeType* node = view->allocate(item);  // allocate a new node
    if (node == NULL) {
        return;
    }
    view->link(NULL, node, view->head);  // link to head
}


/* Add an item to the beginning of the dictionary. */
template <typename NodeType>
inline void appendleft(DictView<NodeType> view, PyObject* item, PyObject* mapped) {
    NodeType* node = view->allocate(item, mapped);  // allocate a new node
    if (node == NULL) {
        return;
    }
    view->link(NULL, node, view->head);  // link to head
}


/* Add multiple items to the end of the list. */
template <template<typename> class ViewType, typename NodeType>
inline void extend(ViewType<NodeType> view, PyObject* items) {
    // CPython API equivalent of `iter(items)`
    PyObject* iterator = PyObject_GetIter(items);
    if (iterator == NULL) {
        return;
    }

    // record original tail if any errors are encountered
    NodeType* original = view->tail;
    NodeType* node;
    PyObject* item;

    while (true) {
        // CPython API equivalent of `next(iterator)`
        item = PyIter_Next(iterator);
        if (item == NULL) {  // end of iterator or error
            if (PyErr_Occurred()) {  // error during next()
                Py_DECREF(iterator);
                Py_DECREF(item);
                undo_extend(view, original);  // recover original list
                return;
            }
            break;  // end of iterator
        }

        // allocate a new node
        node = view->allocate(item);
        if (node == NULL) {  // MemoryError() or TypeError() during hash()
            Py_DECREF(iterator);
            Py_DECREF(item);
            undo_extend(view, original);
            return;
        }

        // link to tail
        view->link(view->tail, node, NULL);
        if (PyErr_Occurred()) {  // ValueError() during link()
            Py_DECREF(iterator);
            Py_DECREF(item);
            undo_extend(view, original);
            return;
        }

        // advance to next item
        Py_DECREF(item);
    }

    // release iterator
    Py_DECREF(iterator);
}


/* Add multiple items to the beginning of the list. */
template <template<typename> class ViewType, typename NodeType>
inline void extendleft(ViewType<NodeType> view, PyObject* items) {
    // CPython API equivalent of `iter(items)`
    PyObject* iterator = PyObject_GetIter(items);
    if (iterator == NULL) {
        return;
    }

    // record original tail if any errors are encountered
    NodeType* original = view->head;
    NodeType* node;
    PyObject* item;

    // TODO: when freeing staged nodes, we start from view->head and free
    // until we reach the original head.  We split at that point

    while (true) {
        // CPython API equivalent of `next(iterator)`
        item = PyIter_Next(iterator);
        if (item == NULL) {  // end of iterator or error
            if (PyErr_Occurred()) {  // error during next()
                Py_DECREF(iterator);
                Py_DECREF(item);
                undo_extendleft(view, original);  // recover original list
                return;
            }
            break;  // end of iterator
        }

        // allocate a new node
        node = view->allocate(item);
        if (node == NULL) {  // MemoryError() or TypeError() during hash()
            Py_DECREF(iterator);
            Py_DECREF(item);
            undo_extendleft(view, original);
            return;
        }

        // link to head
        view->link(NULL, node, view->head);
        if (PyErr_Occurred()) {  // ValueError() during link()
            Py_DECREF(iterator);
            Py_DECREF(item);
            undo_extendleft(view, original);
            return;
        }

        // advance to next item
        Py_DECREF(item);
    }

    // release iterator
    Py_DECREF(iterator);
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Rewind an `extend()` call in the event of an error. */
template <template<typename> class ViewType, typename NodeType>
void undo_extend(ViewType<NodeType>* view, NodeType* original_tail) {
    if (view->tail == original_tail) {
        return;  // nothing to do
    }

    view->tail = original_tail;  // replace original tail
    original_tail = original_tail->next;  // get head of staged nodes
    NodeType::split(view->tail, original_tail);  // split at junction

    // free staged nodes
    NodeType* next;
    while (original_tail != NULL) {
        next = original_tail->next;
        NodeType::deallocate(original_tail);
        original_tail = next;
    }
}


/* Rewind an `extendleft()` call in the event of an error. */
template <template<typename> class ViewType, typename NodeType>
void undo_extendleft(ViewType<NodeType>* view, NodeType* original_head) {
    if (view->head == original_head) {
        return;  // nothing to do
    }

    // free staged nodes
    NodeType* next = view->head->next;
    while (next != original_head) {
        NodeType::deallocate(view->head);
        view->head = next;
        next = next->next;
    }

    NodeType::split(view->head, original_head);  // split at junction
    NodeType::deallocate(view->head);  // deallocate last node
    view->head = original_head;  // replace original head
}


#endif // APPEND_H include guard
