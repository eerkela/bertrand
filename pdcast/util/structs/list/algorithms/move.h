// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_MOVE_H
#define BERTRAND_STRUCTS_ALGORITHMS_MOVE_H

#include <cstddef>  // size_t
#include <tuple>  // std::tuple
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/bounds.h"  // walk()
#include "../core/node.h"  // is_doubly_linked<>
#include "../core/view.h"  // views, MAX_SIZE_T


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {


    /* Move an item within a linked set or dictionary. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    void move(ViewType<NodeType, Allocator>* view, PyObject* item, Py_ssize_t steps) {
        using Node = typename ViewType<NodeType, Allocator>::Node;

        // search for node in hash table
        Node* node = view->search(item);
        if (node == nullptr) {
            PyErr_Format(PyExc_KeyError, "%R is not in the set", item);
            return;
        }

        // check for no-op
        if (steps == 0) {
            return;  // do nothing
        }

        // TODO: similar to walk, but gets the current node's predecessor at
        // the same time.

    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


// TODO: implement a shared method that walks the list and returns the current
// node's predecessor as well as the insertion bounds.  This is relevant for
// move operations since we're moving the node from one position to another.


/* Traverse a list relative to a given sentinel to find the left and right
neighbors for an insertion or removal. */
template <
    template <typename, template <typename> class> class ViewType,
    typename NodeType,
    template <typename> class Allocator,
    typename Node
>
std::tuple<Node*, Node*, Node*> _walk_double(
    ViewType<NodeType, Allocator>* view,
    Node* sentinel,
    Py_ssize_t steps,
    bool truncate
) {
    // adjust steps to account for forward/backward traversal
    if (steps == 0) {
        PyErr_Format(PyExc_ValueError, "steps must be non-zero");
        return std::make_tuple(nullptr, nullptr, nullptr);
    }

    // NOTE: if the list is doubly-linked, then finding the previous node and
    // iterating to the junction point is trivial in both directions.

    Node* left;
    Node* right;
    if (steps > 0) {  // iterate forward 
        left = sentinel;
        right = static_cast<Node*>(left->next);
        for (Py_ssize_t i = 0; i < steps; i++) {
            if (right == nullptr) {
                if (truncate) {
                    break;  // truncate to end of list
                } else {
                    PyErr_SetString(PyExc_IndexError, "list index out of range");
                    return std::tuple<Node*, Node*, Node*> std::make_tuple(
                        nullptr, nullptr, nullptr
                    );
                }
            }
            left = right;
            right = static_cast<Node*>(right->next);
        }
    } else {  // iterate backward
        right = sentinel;
        left = static_cast<Node*>(right->prev);
        for (Py_ssize_t i = -1; i > steps; i--) {
            if (left == nullptr) {
                if (truncate) {
                    break;  // truncate to start of list
                } else {
                    PyErr_SetString(PyExc_IndexError, "list index out of range");
                    return std::tuple<Node*, Node*, Node*> std::make_tuple(
                        nullptr, nullptr, nullptr
                    );
                }
            }
            right = left;
            left = static_cast<Node*>(left->prev);
        }
    }

    // return as 3-tuple
    return std::make_tuple(static_cast<Node*>(sentinel->prev), left, right);

}

#endif // BERTRAND_STRUCTS_ALGORITHMS_MOVE_H include guard
