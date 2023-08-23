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
        if (
            steps == 0 ||
            (steps > 0 && node == view->tail) ||
            (steps < 0 && node == view->head)
        ) {
            return;  // do nothing
        }

        // TODO: all we really need to do is find old_prev and new_prev.  The
        // next pointers can be inferred from the prev pointers.


        Node* old_prev;
        Node* old_next = static_cast<Node*>(node->next);
        Node* new_prev;
        Node* new_next;

        // get neighbors at both the insertion and removal points
        if constexpr (is_doubly_linked<Node>::value) {
            // get old_prev from `prev` pointer
            old_prev = static_cast<Node*>(node->prev);

            // determine new_prev and new_next
            if (steps > 0) {
                new_prev = old_next;
                new_next = static_cast<Node*>(new_prev->next);
                for (Py_ssize_t i = 1; i < steps; i++) {
                    if (new_next == nullptr) {
                        break;  // truncate to end of list
                    }
                    new_prev = new_next;
                    new_next = static_cast<Node*>(new_next->next);
                }
            } else {
                new_next = old_prev;
                new_prev = static_cast<Node*>(new_next->prev);
                for (Py_ssize_t i = -1; i > steps; i--) {
                    if (new_prev == nullptr) {
                        break;  // truncate to start of list
                    }
                    new_next = new_prev;
                    new_prev = static_cast<Node*>(new_prev->prev);
                }
            }
        } else {
            if (steps > 0) {
                old_prev = nullptr;
                Node* temp = view->head;
                while (temp != node) {
                    old_prev = temp;
                    temp = static_cast<Node*>(temp->next);
                }
                new_prev = old_next;
                new_next = static_cast<Node*>(new_prev->next);
                for (Py_ssize_t i = 1; i < steps; i++) {
                    if (new_next == nullptr) {
                        break;  // truncate to end of list
                    }
                    new_prev = new_next;
                    new_next = static_cast<Node*>(new_next->next);
                }
            } else {
                new_prev = nullptr;

                // TODO: 2-pointer approach.  Lagging pointer is the new_prev.
                // Lookahead pointer is old_prev.

            }
        }

    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Get the prev pointers for a relative move operation. */
template <typename View, typename Node>
std::pair<Node*, Node*> _walk_relative(
    View* view,
    Node* node,
    Node* sentinel,
    Py_ssize_t steps
) {
    



}





#endif // BERTRAND_STRUCTS_ALGORITHMS_MOVE_H include guard
