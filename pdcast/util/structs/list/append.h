// include guard prevents multiple inclusion
#ifndef APPEND_H
#define APPEND_H

#include <Python.h>  // for CPython API
#include <node.h>  // for node definitions
#include <view.h>  // for view definitions


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Add an item to the end of a list. */
template <typename NodeType>
inline void append(ListView<NodeType> view, PyObject* item) {
    NodeType* node = view->allocate(item);
    view->link(view->tail, node, NULL);
}


/* Add an item to the end of a set. */
template <typename NodeType>
inline void append(SetView<NodeType> view, PyObject* item) {
    // allocate a new node
    Hashed<NodeType>* node = view->allocate(item);
    if (node == NULL) {  // TypeError() during hash()
        return;
    }

    // link node to end of list
    try {
        view->link(view->tail, node, NULL);
    } catch (const std::bad_alloc&) {  // error during resize()
        view->deallocate(node);
        throw;
    }
    if (PyErr_Occurred()) {  // ValueError() item is already contained in set
        view->deallocate(node);
        throw;
    }
}


/* Add a key-value pair to the end of a dictionary. */
template <typename NodeType>
inline void append(DictView<NodeType> view, PyObject* item) {
    // allocate a new node
    Mapped<NodeType>* node = view->allocate(item);
    if (node == NULL) {  // TypeError() during hash() / tuple unpacking
        return;
    }

    // link node to end of list
    try {
        view->link(view->tail, node, NULL);
    } catch (const std::bad_alloc&) {  // error during resize()
        view->deallocate(node);
        throw;
    }
    if (PyErr_Occurred()) {  // ValueError() item is already contained in dictionary
        view->deallocate(node);
        throw;
    }
}


/* Add a separate key and value to the end of a dictionary. */
template <typename NodeType>
inline void append(DictView<NodeType> view, PyObject* item, PyObject* mapped) {
    // allocate a new node
    Mapped<NodeType>* node = view->allocate(item, mapped);
    if (node == NULL) {  // TypeError() during hash()
        return;
    }

    // link node to end of list
    try {
        view->link(view->tail, node, NULL);
    } catch (const std::bad_alloc&) {  // error during resize()
        view->deallocate(node);
        throw;
    }
    if (PyErr_Occurred()) {  // ValueError() item is already contained in dictionary
        view->deallocate(node);
        throw;
    }
}


/* Add an item to the beginning of a list. */
template <typename NodeType>
inline void appendleft(ListView<NodeType> view, PyObject* item) {
    NodeType* node = view->allocate(item);
    view->link(NULL, node, view->head);
}


/* Add an item to the beginning of a set. */
template <typename NodeType>
inline void appendleft(SetView<NodeType> view, PyObject* item) {
    // allocate a new node
    Hashed<NodeType>* node = view->allocate(item);
    if (node == NULL) {  // TypeError() during hash()
        return;
    }

    // link node to beginning of list
    try {
        view->link(NULL, node, view->head);
    } catch (const std::bad_alloc&) {  // error during resize()
        view->deallocate(node);
        throw;
    }
    if (PyErr_Occurred()) {  // ValueError() item is already contained in set
        view->deallocate(node);
        throw;
    }
}


/* Add a key-value pair to the beginning of the dictionary. */
template <typename NodeType>
inline void appendleft(DictView<NodeType> view, PyObject* item) {
    // allocate a new node
    Mapped<NodeType>* node = view->allocate(item);
    if (node == NULL) {  // TypeError() during hash() / tuple unpacking
        return;
    }

    // link node to beginning of list
    try {
        view->link(NULL, node, view->head);
    } catch (const std::bad_alloc&) {  // error during resize()
        view->deallocate(node);
        throw;
    }
    if (PyErr_Occurred()) {  // ValueError() item is already contained in dictionary
        view->deallocate(node);
        throw;
    }
}


/* Add a separate key and value to the beginning of a dictionary. */
template <typename NodeType>
inline void appendleft(DictView<NodeType> view, PyObject* item, PyObject* mapped) {
    // allocate a new node
    Mapped<NodeType>* node = view->allocate(item, mapped);
    if (node == NULL) {  // TypeError() during hash()
        return;
    }

    // link node to beginning of list
    try {
        view->link(NULL, node, view->head);
    } catch (const std::bad_alloc&) {  // error during resize()
        view->deallocate(node);
        throw;
    }
    if (PyErr_Occurred()) {  // ValueError() item is already contained in dictionary
        view->deallocate(node);
        throw;
    }
}


#endif // APPEND_H include guard
