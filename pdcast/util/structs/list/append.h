// include guard prevents multiple inclusion
#ifndef APPEND_H
#define APPEND_H

#include <Python.h>  // for CPython API
#include "node.h"  // for nodes
#include "view.h"  // for views


// append() for sets and dicts should mimic set.add() and dict.__setitem__(),
// respectively.  If the item is already contained in the set or dict, then
// we just silently return.  Errors are only thrown if the input is invalid,
// i.e. not hashable or not a tuple of length 2 in the case of dictionaries,
// or if a memory allocation error occurs.


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Add an item to the end of a list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline void append(ViewType<NodeType>* view, PyObject* item, bool left) {
    using Node = typename ViewType<NodeType>::Node;

    // allocate a new node
    Node* node = view->node(item);
    if (node == nullptr) {  // Error during node initialization
        return;
    }

    // link to beginning/end of list
    if (left) {
        view->link(nullptr, node, view->head);
    } else {
        view->link(view->tail, node, nullptr);
    }
    if (PyErr_Occurred()) {  // Error during link
        view->recycle(node);
    }
}


/* Add a key-value pair to the end of a dictionary. */
template <typename NodeType>
inline void append(
    DictView<NodeType>* view,
    PyObject* item,
    PyObject* mapped,
    bool left
) {
    using Node = typename DictView<NodeType>::Node;

    // allocate a new node
    Node* node = view->node(item, mapped);  // use 2-argument init()
    if (node == nullptr) {  // Error during node initialization
        return;
    }

    // link to beginning/end of list
    if (left) {
        view->link(nullptr, node, view->head);
    } else {
        view->link(view->tail, node, nullptr);
    }
    view->link(view->tail, node, nullptr);
    if (PyErr_Occurred()) {  // Error during link
        view->recycle(node);
    }
}


////////////////////////
////    WRAPPERS    ////
////////////////////////


// NOTE: Cython doesn't play well with heavily templated functions, so we need
// to explicitly instantiate the specializations we need.  Maybe in a future
// release we won't have to do this:


template void append(ListView<SingleNode>* view, PyObject* item, bool left);
template void append(SetView<SingleNode>* view, PyObject* item, bool left);
template void append(DictView<SingleNode>* view, PyObject* item, bool left);
template void append(
    DictView<SingleNode>* view,
    PyObject* item,
    PyObject* mapped,
    bool left
);
template void append(ListView<DoubleNode>* view, PyObject* item, bool left);
template void append(SetView<DoubleNode>* view, PyObject* item, bool left);
template void append(DictView<DoubleNode>* view, PyObject* item, bool left);
template void append(
    DictView<DoubleNode>* view,
    PyObject* item,
    PyObject* mapped,
    bool left
);


#endif // APPEND_H include guard
