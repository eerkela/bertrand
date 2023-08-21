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

    /* Get the linear distance between two values in a linked set or dictionary. */
    template <typename View>
    Py_ssize_t distance(View* view, PyObject* item1, PyObject* item2) {
        using Node = typename View::Node;

        // search for nodes in hash table
        Node* node1 = view->search(item1);
        if (node1 == nullptr) {
            PyErr_Format(PyExc_KeyError, "%R is not in the set", item1);
            return 0;
        }
        Node* node2 = view->search(item2);
        if (node2 == nullptr) {
            PyErr_Format(PyExc_KeyError, "%R is not in the set", item2);
            return 0;
        }

        // check for no-op
        if (node1 == node2) {
            return 0;  // do nothing
        }

        // get indices of both nodes
        Py_ssize_t idx = 0;
        Py_ssize_t index1 = -1;
        Py_ssize_t index2 = -1;
        Node* curr = view->head;
        while (true) {
            if (curr == node1) {
                index1 = idx;
                if (index2 != -1) {
                    break;  // both nodes found
                }
            } else if (curr == node2) {
                index2 = idx;
                if (index1 != -1) {
                    break;  // both nodes found
                }
            }
            curr = static_cast<Node*>(curr->next);
            idx++;
        }

        // return difference between indices
        return index2 - index1;
    }

    /* Swap the positions of two values in a linked set or dictionary. */
    template <typename View>
    void swap(View* view, PyObject* item1, PyObject* item2) {
        using Node = typename View::Node;

        // search for nodes in hash table
        Node* node1 = view->search(item1);
        if (node1 == nullptr) {
            PyErr_Format(PyExc_KeyError, "%R is not in the set", item1);
            return;
        }
        Node* node2 = view->search(item2);
        if (node2 == nullptr) {
            PyErr_Format(PyExc_KeyError, "%R is not in the set", item2);
            return;
        }

        // check for no-op
        if (node1 == node2) {
            return;  // do nothing
        }

        // get predecessors of both nodes
        Node* prev1;
        Node* prev2;
        if constexpr (is_doubly_linked<Node>::value) {
            // NOTE: if the list is doubly-linked, then we can use the `prev`
            // pointer to get the previous nodes in constant time.
            prev1 = static_cast<Node*>(node1->prev);
            prev2 = static_cast<Node*>(node2->prev);
        } else {
            // Otherwise, we have to iterate from the head of the list.
            prev1 = nullptr;
            prev2 = nullptr;
            Node* prev = nullptr;
            Node* curr = view->head;
            while (true) {
                if (curr == node1) {
                    prev1 = prev;
                    if (prev2 != nullptr) {
                        break;  // both nodes found
                    }
                } else if (curr == node2) {
                    prev2 = prev;
                    if (prev1 != nullptr) {
                        break;  // both nodes found
                    }
                }
                prev = curr;
                curr = static_cast<Node*>(curr->next);
            }
        }

        // swap nodes
        Node* next1 = static_cast<Node*>(node1->next);
        Node* next2 = static_cast<Node*>(node2->next);
        view->unlink(prev1, node1, next1);
        view->unlink(prev2, node2, next2);
        view->link(prev1, node2, next1);
        view->link(prev2, node1, next2);
    }

    /* Move an item within a linked set or dictionary. */
    template <typename View>
    void move(View* view, PyObject* item, Py_ssize_t steps) {
        using Node = typename View::Node;

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
template <typename View, typename Node>
std::tuple<Node*, Node*, Node*> _walk_double(
    View* view,
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
