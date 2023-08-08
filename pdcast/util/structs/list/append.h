// include guard prevents multiple inclusion
#ifndef APPEND_H
#define APPEND_H

#include <Python.h>  // for CPython API
#include <view.h>  // for view definitions


// append() for sets and dicts should mimic set.add() and dict.__setitem__(),
// respectively.  If the item is already contained in the set or dict, then
// we just silently return.  Errors are only thrown if the input is invalid,
// i.e. not hashable or not a tuple of length 2 in the case of dictionaries,
// or if a memory allocation error occurs.


//////////////////////
////    APPEND    ////
//////////////////////


/* Add an item to the end of a list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline void append(ViewType<NodeType>* view, PyObject* item) {
    using Node = typename ViewType<NodeType>::Node;

    // allocate a new node
    Node* node = view->node(item);
    if (node == NULL) {  // Error during node initialization
        return;
    }

    // link to end of list
    view->link(view->tail, node, NULL);
    if (PyErr_Occurred()) {
        view->recycle(node);  // free node on error
    }
}


/* Add a key-value pair to the end of a dictionary. */
template <typename NodeType>
inline void append(DictView<NodeType>* view, PyObject* item, PyObject* mapped) {
    using Node = typename DictView<NodeType>::Node;

    // allocate a new node
    Node* node = view->node(item, mapped);  // use 2-argument init()
    if (node == NULL) {  // Error during node initialization
        return;
    }

    // link to end of list
    view->link(view->tail, node, NULL);
    if (PyErr_Occurred()) {
        view->recycle(node);  // free node on error
    }
}


//////////////////////////
////    APPENDLEFT    ////
//////////////////////////


/* Add an item to the beginning of a list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline void appendleft(ViewType<NodeType>* view, PyObject* item) {
    using Node = typename ViewType<NodeType>::Node;

    // allocate a new node
    Node* node = view->node(item);
    if (node == NULL) {  // Error during node initialization
        return;
    }

    // link to beginning of list
    view->link(NULL, node, view->head);
    if (PyErr_Occurred()) {
        view->recycle(node);  // free node on error
    }
}


/* Add a key-value pair to the beginning of a dictionary. */
template <typename NodeType>
inline void appendleft(DictView<NodeType>* view, PyObject* item, PyObject* mapped) {
    using Node = typename DictView<NodeType>::Node;

    // allocate a new node
    Node* node = view->node(item, mapped);  // use 2-argument init()
    if (node == NULL) {  // Error during node initialization
        return;
    }

    // link to beginning of list
    view->link(NULL, node, view->head);
    if (PyErr_Occurred()) {
        view->recycle(node);  // free node on error
    }
}


#endif // APPEND_H include guard
