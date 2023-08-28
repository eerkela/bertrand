// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_MOVE_H
#define BERTRAND_STRUCTS_ALGORITHMS_MOVE_H

#include <cstddef>  // size_t
#include <tuple>  // std::tuple
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/bounds.h"  // relative_junction()
#include "../core/node.h"  // has_prev<>
#include "../core/view.h"  // views, MAX_SIZE_T


// TODO: move_relative() is probably buggy for singly-linked lists, since it's
// so complicated.  It will need thorough testing.


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
        if constexpr (has_prev<Node>::value) {
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

        // get prev pointers at both insertion and removal point
        Node* old_prev;
        Node* old_next = static_cast<Node*>(node->next);
        Node* new_prev;
        Node* new_next;

        // NOTE: if the list is doubly-linked, then we can use the `prev` pointer
        // to get the previous node in constant time.
        if constexpr (has_prev<Node>::value) {
            old_prev = static_cast<Node*>(node->prev);
            std::pair<Node*, Node*> bounds = relative_junction(
                view, node, steps, true
            );
            new_prev = bounds.first;
            new_next = bounds.second;

        // NOTE: otherwise, we have to iterate from the head of the list.  If
        // we're careful, we can do this in a single traversal for both the old
        // and new pointers, without having to repeat any work.
        } else {
            if (steps > 0) {
                // if we're moving forwards, then we'll hit the removal point before
                // the insertion point, so we don't need any lookahead pointers
                old_prev = nullptr
                Node* temp = view->head;
                while (temp != node) {
                    old_prev = temp;
                    temp = static_cast<Node*>(temp->next);
                }

                // we then iterate forward to find the insertion point
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
                // if we're moving backwards, then we'll hit the insertion point
                // before the removal point, so we need a lookahead pointer
                old_prev = nullptr;
                Node* lookahead = view->head;
                bool truncate = false;
                for (Py_ssize_t i = 0; i > steps; i--) {
                    if (lookahead == node) {  // truncate to beginning of list
                        new_prev = nullptr;
                        new_next = view->head;
                        truncate = true;
                        break;
                    }
                    old_prev = lookahead;
                    lookahead = static_cast<Node*>(lookahead->next);
                }

                // if we didn't truncate, then we advance both pointers until
                // we find the removal point
                if (!truncate) {
                    new_prev = view->head;
                    while (lookahead != node) {
                        new_prev = static_cast<Node*>(new_prev->next);
                        old_prev = lookahead;
                        lookahead = static_cast<Node*>(lookahead->next);
                    }
                    new_next = static_cast<Node*>(new_prev->next);
                }
            }
        }

        // move node to new position
        view->unlink(old_prev, node, old_next);
        view->link(new_prev, node, new_next);
    }

    /* Move an item to a particular index of a linked set or dictionary. */
    template <typename View, typename T>
    void move_to_index(View* view, PyObject* item, T index) {
        using Node = typename View::Node;

        // search for node in hash table
        Node* node = view->search(item);
        if (node == nullptr) {
            PyErr_Format(PyExc_KeyError, "%R is not in the set", item);
            return;
        }

        // normalize index
        size_t idx = normalize_index(index, view->size, true);

        // get prev pointers at both insertion and removal point
        Node* old_prev;
        Node* old_next = static_cast<Node*>(node->next);
        Node* new_prev;
        Node* new_next;

        // NOTE: if the list is doubly-linked, then we can use the `prev` pointer
        // to get the previous node in constant time.
        if constexpr (has_prev<Node>::value) {
            old_prev = static_cast<Node*>(node->prev);
            std::pair<Node*, Node*> bounds = junction(view, view->head, idx);
            new_prev = bounds.first;
            new_next = bounds.second;

        // NOTE: otherwise, we have to iterate from the head of the list.  If
        // we're careful, we can do this in a single traversal for both the old
        // and new pointers, without having to repeat any work.
        } else {
            new_prev = nullptr;
            new_next = view->head;
            bool found = false;
            for (size_t i = 0; i < idx; i++) {
                if (new_next == node) {
                    old_prev = new_prev;
                    found = true;
                }
                new_prev = new_next;
                new_next = static_cast<Node*>(new_next->next);
            }

            // if we didn't find the removal point, then we need to continue
            // iterating until we do
            if (!found) {
                Node* temp = new_next;
                while (temp != node) {
                    old_prev = temp;
                    temp = static_cast<Node*>(temp->next);
                }
            }
        }

        // move node to new position
        view->unlink(old_prev, node, old_next);
        view->link(new_prev, node, new_next);
    }

    /* Move an item within a linked set or dictionary relative to a given sentinel
    value. */
    template <typename View>
    void move_relative(
        View* view,
        PyObject* item,
        PyObject* sentinel,
        Py_ssize_t offset
    ) {
        using Node = typename View::Node;

        // check for trivial case
        int comp = PyObject_RichCompareBool(item, sentinel, Py_EQ);
        if (comp == -1) {
            return;  // propagate error
        } else if (comp == 1) {  // this devolves into a simple move()
            move(view, item, offset);
            return;
        }

        // search for item in hash table
        Node* node = view->search(item);
        if (node == nullptr) {
            PyErr_Format(PyExc_KeyError, "%R is not in the set", item);
            return;
        }

        // search for sentinel in hash table
        Node* sentinel_node = view->search(sentinel);
        if (sentinel_node == nullptr) {
            PyErr_Format(PyExc_KeyError, "%R is not in the set", sentinel);
            return;
        }

        // get prev pointers at both insertion and removal point
        Node* old_prev;
        Node* old_next = static_cast<Node*>(node->next);
        Node* new_prev;
        Node* new_next;

        // NOTE: if the list is doubly-linked, then we can use the `prev` pointer
        // to get the previous node in constant time.
        if constexpr (has_prev<Node>::value) {
            // NOTE: because we're moving relative to some other node, we can
            // run into a situation where the junction includes the node to be
            // moved, which can cause an error.  If we remove the node before
            // searching for the junction, then we can avoid this problem.
            old_prev = static_cast<Node*>(node->prev);
            view->unlink(old_prev, node, old_next);
            std::pair<Node*, Node*> bounds = relative_junction(
                view, sentinel_node, offset, true
            );
            new_prev = bounds.first;
            new_next = bounds.second;

        // NOTE: otherwise, we have to iterate from the head of the list.  If
        // we're careful, we can do this in a single traversal for both the old
        // and new pointers, without having to repeat any work.
        } else {
            if (offset > 0) {
                // advance to sentinel, recording the original node if we encounter it.
                new_prev = nullptr;
                new_next = view->head;
                bool found = false;
                while (new_next != sentinel_node) {
                    if (new_next == node) {
                        old_prev = new_prev;
                        found = true;
                    } else {
                        new_prev = new_next;
                    }
                    new_next = static_cast<Node*>(new_next->next);
                }

                // continue out to offset to find insertion point
                for (Py_ssize_t i = 0; i < offset; i++) {
                    if (new_next == nullptr) {
                        break;  // truncate to end of list
                    } else if (new_next == node) {
                        old_prev = new_prev;
                        found = true;
                    } else {
                        new_prev = new_next;
                    }
                    new_next = static_cast<Node*>(new_next->next);
                }

                // if we still haven't found the original node, then we need to
                // continue iterating until we do
                if (!found) {
                    Node* temp = new_next;
                    while (temp != node) {
                        old_prev = temp;
                        temp = static_cast<Node*>(temp->next);
                    }
                }
            } else {
                // create lookahead pointer
                Node* temp = nullptr;
                Node* lookahead = view->head;
                bool found = false;
                bool truncate = false;
                for (Py_ssize_t i = 0; i > offset; i--) {
                    if (lookahead == sentinel_node) {  // truncate to beginning of list
                        new_prev = nullptr;
                        new_next = view->head;
                        truncate = true;
                        break;
                    } else if (lookahead == node) {
                        old_prev = temp;
                        found = true;
                    }
                    temp = lookahead;
                    lookahead = static_cast<Node*>(lookahead->next);
                }

                // if we didn't truncate, then we advance both pointers until
                // we find the removal point
                if (!truncate) {
                    new_prev = view->head;
                    while (lookahead != sentinel_node) {
                        if (lookahead == node) {
                            old_prev = temp;
                            found = true;
                        } else {
                            new_prev = static_cast<Node*>(new_prev->next);
                        }
                        temp = lookahead;
                        lookahead = static_cast<Node*>(lookahead->next);
                    }
                    new_next = static_cast<Node*>(new_prev->next);
                }

                // if we still haven't found the original node, then we need to
                // continue iterating until we do
                if (!found) {
                    temp = new_next;
                    while (temp != node) {
                        old_prev = temp;
                        temp = static_cast<Node*>(temp->next);
                    }
                }
            }

            // remove node from original position
            view->unlink(old_prev, node, old_next);
        }

        // insert node at new position
        view->link(new_prev, node, new_next);
    }

}


#endif // BERTRAND_STRUCTS_ALGORITHMS_MOVE_H include guard
