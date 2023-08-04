// include guard prevents multiple inclusion
#ifndef APPEND_H
#define APPEND_H

#include <Python.h>  // for CPython API
#include <node.h>  // for node definitions
#include <view.h>  // for view definitions


/* Add an item to the end of a list. */
template <typename NodeType>
inline void append(ListView<NodeType> view, PyObject* item) {
    NodeType* node = view->allocate(item);
    if (node == NULL) {
        return;
    }
    view->link(view->tail, node, NULL);
}


/* Add an item to the end of a set. */
template <typename NodeType>
inline void append(SetView<NodeType> view, PyObject* item) {
    Hashed<NodeType>* node = view->allocate(item);
    if (node == NULL) {
        return;
    }
    view->link(view->tail, node, NULL);
}


/* Add a key-value pair to the end of a dictionary. */
template <typename NodeType>
inline void append(DictView<NodeType> view, PyObject* item) {
    Mapped<NodeType>* node = view->allocate(item);
    if (node == NULL) {
        return;
    }
    view->link(view->tail, node, NULL);
}


/* Add a separate key and value to the end of a dictionary. */
template <typename NodeType>
inline void append(DictView<NodeType> view, PyObject* item, PyObject* mapped) {
    Mapped<NodeType>* node = view->allocate(item, mapped);
    if (node == NULL) {
        return;
    }
    view->link(view->tail, node, NULL);
}


/* Add an item to the beginning of a list. */
template <typename NodeType>
inline void appendleft(ListView<NodeType> view, PyObject* item) {
    NodeType* node = view->allocate(item);
    if (node == NULL) {
        return;
    }
    view->link(NULL, node, view->head);
}


/* Add an item to the beginning of a set. */
template <typename NodeType>
inline void appendleft(SetView<NodeType> view, PyObject* item) {
    Hashed<NodeType>* node = view->allocate(item);
    if (node == NULL) {
        return;
    }
    view->link(NULL, node, view->head);
}


/* Add a key-value pair to the beginning of the dictionary. */
template <typename NodeType>
inline void appendleft(DictView<NodeType> view, PyObject* item) {
    Mapped<NodeType>* node = view->allocate(item);
    if (node == NULL) {
        return;
    }
    view->link(NULL, node, view->head);
}


/* Add a separate key and value to the beginning of a dictionary. */
template <typename NodeType>
inline void appendleft(DictView<NodeType> view, PyObject* item, PyObject* mapped) {
    Mapped<NodeType>* node = view->allocate(item, mapped);
    if (node == NULL) {
        return;
    }
    view->link(NULL, node, view->head);
}


#endif // APPEND_H include guard
