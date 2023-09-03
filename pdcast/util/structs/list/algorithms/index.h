// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_INDEX_H
#define BERTRAND_STRUCTS_ALGORITHMS_INDEX_H

#include <cstddef>  // size_t
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/bounds.h"  // normalize_bounds(), etc.
#include "../core/node.h"  // has_prev<>
#include "../core/view.h"  // views, MAX_SIZE_T


namespace Ops {

    /* Get the index of an item within a linked list. */
    template <typename NodeType, template <typename> class Allocator, typename T>
    size_t index(
        ListView<NodeType, Allocator>& view,
        PyObject* item,
        T start,
        T stop
    ) {
        using Node = typename ListView<NodeType, Allocator>::Node;
        Node* curr;
        size_t idx;

        // allow python-style negative indexing + boundschecking
        std::pair<size_t, size_t> bounds = normalize_bounds(
            start, stop, view.size, true
        );

        // NOTE: if start index is closer to tail and the list is doubly-linked,
        // we can iterate from the tail to save time.
        if constexpr (has_prev<Node>::value) {
            if (bounds.first > view.size / 2) {
                curr = view.tail;

                // skip to stop index
                for (idx = view.size - 1; idx > bounds.second; idx--) {
                    curr = static_cast<Node*>(curr->prev);
                }

                // search until we hit start index
                bool found = false;
                size_t last_observed;
                while (idx >= bounds.first) {
                    // C API equivalent of the == operator
                    int comp = PyObject_RichCompareBool(item, curr->value, Py_EQ);
                    if (comp == -1) {  // comparison raised an exception
                        return MAX_SIZE_T;
                    } else if (comp == 1) {  // found a match
                        last_observed = idx;
                        found = true;
                    }

                    // advance to next node
                    curr = static_cast<Node*>(curr->prev);
                    idx--;
                }

                // return first occurrence in range
                if (found) {
                    return last_observed;
                }

                // item not found
                PyErr_Format(PyExc_ValueError, "%R is not in list", item);
                return MAX_SIZE_T;
            }
        }

        // NOTE: otherwise, we iterate forward from the head
        curr = view.head;
        for (idx = 0; idx < bounds.first; idx++) {  // skip to start index
            curr = static_cast<Node*>(curr->next);
        }

        // search until we hit stop index
        while (idx < bounds.second) {
            // C API equivalent of the == operator
            int comp = PyObject_RichCompareBool(curr->value, item, Py_EQ);
            if (comp == -1) {  // `==` raised an exception
                return MAX_SIZE_T;
            } else if (comp == 1) {  // found a match
                return idx;
            }

            // advance to next node
            curr = static_cast<Node*>(curr->next);
            idx++;
        }

        // item not found
        PyErr_Format(PyExc_ValueError, "%R is not in list", item);
        return MAX_SIZE_T;
    }

    /* Get the index of an item within a linked set or dictionary. */
    template <typename View, typename T>
    size_t index(View& view, PyObject* item, T start, T stop) {
        using Node = typename View::Node;

        // allow python-style negative indexing + boundschecking
        std::pair<size_t, size_t> bounds = normalize_bounds(
            start, stop, view.size, true
        );

        // search for item in hash table
        Node* node = view.search(item);
        if (node == nullptr) {
            PyErr_Format(PyExc_ValueError, "%R is not in the set", item);
            return MAX_SIZE_T;
        }

        // skip to start index
        Node* curr = view.head;
        size_t idx;
        for (idx = 0; idx < bounds.first; idx++) {
            if (curr == node) {  // item exists, but comes before range
                PyErr_Format(PyExc_ValueError, "%R is not in the set", item);
                return MAX_SIZE_T;
            }
            curr = static_cast<Node*>(curr->next);
        }

        // iterate until we hit match or stop index
        while (curr != node && idx < bounds.second) {
            curr = static_cast<Node*>(curr->next);
            idx++;
        }
        if (curr == node) {
            return idx;
        }

        // item exists, but comes after range
        PyErr_Format(PyExc_ValueError, "%R is not in the set", item);
        return MAX_SIZE_T;
    }

    /* Get the linear distance between two values in a linked set or dictionary. */
    template <typename View>
    Py_ssize_t distance(View& view, PyObject* item1, PyObject* item2) {
        using Node = typename View::Node;

        // search for nodes in hash table
        Node* node1 = view.search(item1);
        if (node1 == nullptr) {
            PyErr_Format(PyExc_KeyError, "%R is not in the set", item1);
            return 0;
        }
        Node* node2 = view.search(item2);
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
        Node* curr = view.head;
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

}


#endif // BERTRAND_STRUCTS_ALGORITHMS_INDEX_H include guard
