
// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_EXTEND_H
#define BERTRAND_STRUCTS_ALGORITHMS_EXTEND_H

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


namespace Ops {

    /* Add multiple items to the end of a list, set, or dictionary. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    inline void extend(
        ViewType<NodeType, Allocator>* view,
        PyObject* items,
        bool left
    ) {
        using Node = typename ViewType<NodeType, Allocator>::Node;
        Node* null = static_cast<Node*>(nullptr);

        if (left) {
            _extend_right_to_left(view, null, view->head, items);
        } else {
            _extend_left_to_right(view, view->tail, null, items);
        }
    }

    /* Insert elements into a set or dictionary immediately after the given sentinel
    value. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    inline void extendafter(
        ViewType<NodeType, Allocator>* view,
        PyObject* sentinel,
        PyObject* items
    ) {
        using Node = typename ViewType<NodeType, Allocator>::Node;

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
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    void extendbefore(
        ViewType<NodeType, Allocator>* view,
        PyObject* sentinel,
        PyObject* items
    ) {
        using Node = typename ViewType<NodeType, Allocator>::Node;

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

}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Insert items from the left node to the right node. */
template <
    template <typename, template <typename> class> class ViewType,
    typename NodeType,
    template <typename> class Allocator,
    typename Node
>
void _extend_left_to_right(
    ViewType<NodeType, Allocator>* view,
    Node* left,
    Node* right,
    PyObject* items
) {
    // CPython API equivalent of `iter(items)`
    PyObject* iterator = PyObject_GetIter(items);
    if (iterator == nullptr) {  // TypeError() during iter()
        return;
    }

    Node* prev = left;
    Node* curr;

    // CPython API equivalent of `for item in items:`
    while (true) {
        PyObject* item = PyIter_Next(iterator);  // next(iterator)
        if (item == nullptr) {  // end of iterator or error
            break;
        }

        // allocate a new node
        curr = view->node(item);
        if (curr == nullptr) {
            Py_DECREF(item);
            break;  // enter undo branch
        }

        // insert from left to right
        view->link(prev, curr, right);
        if (PyErr_Occurred()) {  // ValueError() item is already in list
            Py_DECREF(item);
            break;  // enter undo branch
        }

        // advance to next item
        prev = curr;
        Py_DECREF(item);
    }

    // release iterator
    Py_DECREF(iterator);

    // check for error
    if (PyErr_Occurred()) {  // recover original list
        // remove staged nodes from left to right bounds
        prev = left;
        curr = static_cast<Node*>(prev->next);
        while (curr != right) {
            Node* next = static_cast<Node*>(curr->next);
            view->unlink(prev, curr, next);
            view->recycle(curr);
            curr = next;
        }

        // join left and right bounds (can be NULL)
        Node::join(left, right);

        // reset tail if necessary
        if (right == nullptr) {
            view->tail = right;
        }
    }
}


/* Insert items from the right node to the left node. */
template <
    template <typename, template <typename> class> class ViewType,
    typename NodeType,
    template <typename> class Allocator,
    typename Node
>
void _extend_right_to_left(
    ViewType<NodeType, Allocator>* view,
    Node* left,
    Node* right,
    PyObject* items
) {
    // CPython API equivalent of `iter(items)`
    PyObject* iterator = PyObject_GetIter(items);
    if (iterator == nullptr) {  // TypeError() during iter()
        return;
    }

    Node* next = right;
    Node* curr;

    // CPython API equivalent of `for item in items:`
    while (true) {
        PyObject* item = PyIter_Next(iterator);  // next(iterator)
        if (item == nullptr) {  // end of iterator or error
            break;
        }

        // allocate a new node
        curr = view->node(item);
        if (curr == nullptr) {  // error during node allocation
            Py_DECREF(item);
            break;  // enter undo branch
        }

        // insert from right to left
        view->link(left, curr, next);
        if (PyErr_Occurred()) {  // error during list insertion
            Py_DECREF(item);
            break;  // enter undo branch
        }

        // advance to next item
        next = curr;
        Py_DECREF(item);
    }

    // release iterator
    Py_DECREF(iterator);

    // check for error
    if (PyErr_Occurred()) {  // recover original list
        // NOTE: the list isn't guaranteed to be doubly-linked, so we have to
        // iterate from left to right to delete the staged nodes.
        Node* prev;
        if (left == nullptr) {
            prev = view->head;
        } else {
            prev = left;
        }

        // remove staged nodes from left to right bounds
        curr = static_cast<Node*>(prev->next);
        while (curr != right) {
            next = static_cast<Node*>(curr->next);
            view->unlink(prev, curr, next);
            view->recycle(curr);
            curr = next;
        }

        // join left and right bounds (can be NULL)
        Node::join(left, right);

        // reset head if necessary
        if (left == nullptr) {
            view->head = left;
        }
    }
}


#endif // BERTRAND_STRUCTS_ALGORITHMS_EXTEND_H include guard
