// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_EXTEND_H
#define BERTRAND_STRUCTS_ALGORITHMS_EXTEND_H

#include <cstddef>  // size_t
#include <Python.h>  // CPython API
#include "../core/bounds.h"  // walk()
#include "../core/node.h"  // is_doubly_linked<>
#include "../core/view.h"  // views


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

    ////////////////////////
    ////    EXTEND()    ////
    ////////////////////////

    /* Add multiple items to the end of a list, set, or dictionary. */
    template <typename View>
    inline void extend(View* view, PyObject* items, bool left) {
        using Node = typename View::Node;
        Node* null = static_cast<Node*>(nullptr);

        if (left) {
            _extend_right_to_left(view, null, view->head, items, false);
        } else {
            _extend_left_to_right(view, view->tail, null, items, false);
        }
    }

    /////////////////////////////////
    ////    EXTEND_RELATIVE()    ////
    /////////////////////////////////

    /* Insert elements into a linked set or dictionary relative to the given
    sentinel value. */
    template <typename View>
    void extend_relative(
        View* view,
        PyObject* items,
        PyObject* sentinel,
        Py_ssize_t offset,
        bool reverse
    ) {
        // propagate errors
        _extend_relative(view, items, sentinel, offset, reverse, false);
    }

    ////////////////////////
    ////    UPDATE()    ////
    ////////////////////////

    /* Update a set or dictionary, appending items that are not already present. */
    template <typename View>
    inline void update(View* view, PyObject* items, bool left) {
        using Node = typename View::Node;
        Node* null = static_cast<Node*>(nullptr);

        if (left) {
            _extend_right_to_left(view, null, view->head, items, true);
        } else {
            _extend_left_to_right(view, view->tail, null, items, true);
        }
    }

    /////////////////////////////////
    ////    UPDATE_RELATIVE()    ////
    /////////////////////////////////

    /* Update a set or dictionary relative to a given sentinel value, appending
    items that are not already present. */
    template <typename View>
    inline void update_relative(
        View* view,
        PyObject* items,
        PyObject* sentinel,
        Py_ssize_t offset,
        bool reverse
    ) {
        // suppress errors
        _extend_relative(view, items, sentinel, offset, reverse, true);
    }

    ////////////////////////////////
    ////    CLEAR_RELATIVE()    ////
    ////////////////////////////////

    /* Remove a sequence of items from a linked set or dictionary relative to a
    given sentinel value. */
    template <typename View>
    void clear_relative(
        View* view,
        PyObject* sentinel,
        Py_ssize_t offset,
        Py_ssize_t length
    ) {
        using Node = typename View::Node;

        // ensure offset is nonzero
        if (offset == 0) {
            PyErr_SetString(PyExc_ValueError, "offset must be non-zero");
            return;
        }

        // search for sentinel
        Node* node = view->search(sentinel);
        if (node == nullptr) {  // sentinel not found
            PyErr_Format(PyExc_KeyError, "%R is not contained in the set", sentinel);
            return;
        }

        // get neighbors for deletion
        std::pair<Node*, Node*> bounds = relative_junction(
            view, node, offset, true
        );

        // TODO: if length = -1, clear to the end of the list.  
    }
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Insert items from the left node to the right node. */
template <typename View, typename Node>
void _extend_left_to_right(
    View* view,
    Node* left,
    Node* right,
    PyObject* items,
    bool update
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

        // check if we should update existing nodes
        if constexpr (is_setlike<View>::value) {
            if (update) {
                Node* existing = view->search(curr);
                if (existing != nullptr) {  // item already exists
                    if constexpr (has_mapped<Node>::value) {
                        // update mapped value
                        Py_DECREF(existing->mapped);
                        Py_INCREF(curr->mapped);
                        existing->mapped = curr->mapped;
                    }
                    view->recycle(curr);
                    Py_DECREF(item);
                    continue;  // advance to next item without updating `prev`
                }
            }
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
        // remove staged nodes from left to right
        prev = left;
        curr = static_cast<Node*>(prev->next);
        while (curr != right) {
            Node* next = static_cast<Node*>(curr->next);
            view->unlink(prev, curr, next);
            view->recycle(curr);
            curr = next;
        }

        // join left and right bounds
        Node::join(left, right);
        if (right == nullptr) {
            view->tail = right;  // reset tail if necessary
        }
    }
}


/* Insert items from the right node to the left node. */
template <typename View, typename Node>
void _extend_right_to_left(
    View* view,
    Node* left,
    Node* right,
    PyObject* items,
    bool update
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

        // check if we should update existing nodes
        if constexpr (is_setlike<View>::value) {
            if (update) {
                Node* existing = view->search(curr);
                if (existing != nullptr) {  // item already exists
                    if constexpr (has_mapped<Node>::value) {
                        // update mapped value
                        Py_DECREF(existing->mapped);
                        Py_INCREF(curr->mapped);
                        existing->mapped = curr->mapped;
                    }
                    view->recycle(curr);
                    Py_DECREF(item);
                    continue;  // advance to next item without updating `next`
                }
            }
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
        if (left == nullptr) {
            view->head = left;  // reset head if necessary
        }
    }
}


/* Implement both extend_relative() and update_relative() depending on error handling
flag. */
template <typename View>
void _extend_relative(
    View* view,
    PyObject* items,
    PyObject* sentinel,
    Py_ssize_t offset,
    bool reverse,
    bool update
) {
    using Node = typename View::Node;

    // ensure offset is nonzero
    if (offset == 0) {
        PyErr_SetString(PyExc_ValueError, "offset must be non-zero");
        return;
    }

    // search for sentinel
    Node* node = view->search(sentinel);
    if (node == nullptr) {  // sentinel not found
        if (!update) {
            PyErr_Format(PyExc_KeyError, "%R is not contained in the set", sentinel);
        }
        return;
    }

    // get neighbors for insertion
    // NOTE: truncate = true means we will never raise an error
    std::pair<Node*, Node*> bounds = relative_junction(view, node, offset, true);

    // insert items between left and right bounds
    if (reverse) {
        _extend_right_to_left(view, bounds.first, bounds.second, items, update);
    } else {
        _extend_left_to_right(view, bounds.first, bounds.second, items, update);
    }
}


#endif // BERTRAND_STRUCTS_ALGORITHMS_EXTEND_H include guard
