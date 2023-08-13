
// include guard prevents multiple inclusion
#ifndef EXTEND_H
#define EXTEND_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include "node.h"  // for nodes
#include "view.h"  // for views


// TODO: append() for sets and dicts should mimic set.update() and dict.update(),
// respectively.  If the item is already contained in the set or dict, then
// we just ignore it and move on.  Errors are only thrown if the input is
// invalid, i.e. not hashable or not a tuple of length 2 in the case of
// dictionaries, or if a memory allocation error occurs.

// in the case of dictionaries, we should replace the current node's value
// with the new value if the key is already contained in the dictionary.  This
// overwrites the current mapped value without allocating a new node.


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Add multiple items to the end of a list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline void extend(ViewType<NodeType>* view, PyObject* items, bool left) {
    using Node = typename ViewType<NodeType>::Node;
    Node* null = static_cast<Node*>(nullptr);

    if (left) {
        _extend_right_to_left(view, null, view->head, items);
    } else {
        _extend_left_to_right(view, view->tail, null, items);
    }
}


/* Insert elements into a set or dictionary immediately after the given sentinel
value. */
template <template <typename> class ViewType, typename NodeType>
inline void extendafter(
    ViewType<NodeType>* view,
    PyObject* sentinel,
    PyObject* items
) {
    using Node = typename ViewType<NodeType>::Node;

    // search for sentinel
    Node* left = view->search(sentinel);
    if (left == nullptr) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R is not contained in the list", sentinel);
        return;
    }

    // insert items after sentinel
    _extend_left_to_right(view, left, static_cast<Node*>(left->next), items);
}


/* Insert elements into a linked set or dictionary immediately before a given
sentinel value. */
template <template <typename> class ViewType, typename NodeType>
inline void extendbefore(
    ViewType<NodeType>* view,
    PyObject* sentinel,
    PyObject* items
) {
    using Node = typename ViewType<NodeType>::Node;

    // search for sentinel
    Node* right = view->search(sentinel);
    if (right == nullptr) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R is not contained in the list", sentinel);
        return;
    }

    // NOTE: if the list is doubly-linked, then we can just use the node's
    // `prev` pointer to find the left bound.
    if constexpr (is_doubly_linked<Node>::value) {
        _extend_right_to_left(view, static_cast<Node*>(right->prev), right, items);
        return;
    }

    // NOTE: otherwise, we have to iterate from the head of the list.
    Node* left;
    Node* next;
    if (right == view->head) {
        left = nullptr;
    } else {
        left = view->head;
        next = static_cast<Node*>(left->next);
        while (next != right) {
            left = next;
            next = static_cast<Node*>(next->next);
        }
    }

    // insert items between the left and right bounds
    _extend_right_to_left(view, left, right, items);
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Insert items from the left node to the right node. */
template <template <typename> class ViewType, typename NodeType, typename Node>
void _extend_left_to_right(
    ViewType<NodeType>* view,
    Node* left,
    Node* right,
    PyObject* items
) {
    // CPython API equivalent of `iter(items)`
    PyObject* iterator = PyObject_GetIter(items);
    if (iterator == nullptr) {  // TypeError() during iter()
        return;
    }

    // CPython API equivalent of `for item in items:`
    Node* prev = left;
    while (true) {
        PyObject* item = PyIter_Next(iterator);  // next(iterator)
        if (item == nullptr) {  // end of iterator or error
            break;
        }

        // allocate a new node
        Node* node = view->node(item);
        if (node == nullptr) {
            Py_DECREF(item);
            break;  // enter undo branch
        }

        // insert from left to right
        view->link(prev, node, right);
        if (PyErr_Occurred()) {  // ValueError() item is already in list
            Py_DECREF(item);
            break;  // enter undo branch
        }

        // advance to next item
        prev = node;  // left bound becomes new node
        Py_DECREF(item);
    }

    // release iterator
    Py_DECREF(iterator);

    // check for error
    if (PyErr_Occurred()) {
        _undo_left_to_right(view, left, right);  // recover original list
        if (right == nullptr) {
            view->tail = right;  // replace original tail
        }
    }
}


/* Insert items from the right node to the left node. */
template <template <typename> class ViewType, typename NodeType, typename Node>
void _extend_right_to_left(
    ViewType<NodeType>* view,
    Node* left,
    Node* right,
    PyObject* items
) {
    // CPython API equivalent of `iter(items)`
    PyObject* iterator = PyObject_GetIter(items);
    if (iterator == nullptr) {  // TypeError() during iter()
        return;
    }

    // CPython API equivalent of `for item in items:`
    Node* next = right;
    while (true) {
        PyObject* item = PyIter_Next(iterator);  // next(iterator)
        if (item == nullptr) {  // end of iterator or error
            break;
        }

        // allocate a new node
        Node* node = view->node(item);
        if (node == nullptr) {  // TypeError() during hash() / tuple unpacking
            Py_DECREF(item);
            break;  // enter undo branch
        }

        // insert from right to left
        view->link(left, node, next);
        if (PyErr_Occurred()) {  // ValueError() item is already in list
            Py_DECREF(item);
            break;  // enter undo branch
        }

        // advance to next item
        next = node;  // right bound becomes new node
        Py_DECREF(item);
    }

    // release iterator
    Py_DECREF(iterator);

    // check for error
    if (PyErr_Occurred()) {
        _undo_right_to_left(view, left, right);  // recover original list
        if (left == nullptr) {
            view->head = left;  // replace original head
        }
    }
}


/* Rewind an `extend()`/`extendafter()` call in the event of an error. */
template <template <typename> class ViewType, typename NodeType, typename Node>
void _undo_left_to_right(
    ViewType<NodeType>* view,
    Node* left,
    Node* right
) {
    Node* prev = left;  // NOTE: left must not be NULL, but right can be
    Node* curr = static_cast<Node*>(prev->next);
    while (curr != right) {
        Node* next = static_cast<Node*>(curr->next);
        view->unlink(prev, curr, next);
        view->recycle(curr);
        curr = next;
    }

    // join left and right
    Node::join(left, right);  // handles NULLs
}


/* Rewind an `extendleft()`/`extendbefore()` call in the event of an error. */
template <template <typename> class ViewType, typename NodeType, typename Node>
void _undo_right_to_left(
    ViewType<NodeType>* view,
    Node* left,
    Node* right
) {
    // NOTE: right must not be NULL, but left can be
    Node* prev;
    if (left == nullptr) {
        prev = view->head;
    } else {
        prev = left;
    }

    // free staged nodes
    Node* curr = static_cast<Node*>(prev->next);
    while (curr != right) {
        Node* next = static_cast<Node*>(curr->next);
        view->unlink(prev, curr, next);
        view->recycle(curr);
        curr = next;
    }

    // join left and right
    Node::join(left, right);  // handles NULLs
}


///////////////////////
////    ALIASES    ////
///////////////////////


// NOTE: Cython doesn't play well with heavily templated functions, so we need
// to explicitly instantiate the specializations we need.  Maybe in a future
// release we won't have to do this:


template void extend(ListView<SingleNode>* view, PyObject* items, bool left);
template void extend(SetView<SingleNode>* view, PyObject* items, bool left);
template void extend(DictView<SingleNode>* view, PyObject* items, bool left);
template void extend(ListView<DoubleNode>* view, PyObject* items, bool left);
template void extend(SetView<DoubleNode>* view, PyObject* items, bool left);
template void extend(DictView<DoubleNode>* view, PyObject* items, bool left);
template void extendafter(
    SetView<SingleNode>* view,
    PyObject* sentinel,
    PyObject* items
);
template void extendafter(
    DictView<SingleNode>* view,
    PyObject* sentinel,
    PyObject* items
);
template void extendafter(
    SetView<DoubleNode>* view,
    PyObject* sentinel,
    PyObject* items
);
template void extendafter(
    DictView<DoubleNode>* view,
    PyObject* sentinel,
    PyObject* items
);
template void extendbefore(
    SetView<SingleNode>* view,
    PyObject* sentinel,
    PyObject* items
);
template void extendbefore(
    DictView<SingleNode>* view,
    PyObject* sentinel,
    PyObject* items
);
template void extendbefore(
    SetView<DoubleNode>* view,
    PyObject* sentinel,
    PyObject* items
);
template void extendbefore(
    DictView<DoubleNode>* view,
    PyObject* sentinel,
    PyObject* items
);


#endif // EXTEND_H include guard
