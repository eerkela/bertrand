// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_GET_SLICE_H
#define BERTRAND_STRUCTS_ALGORITHMS_GET_SLICE_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/bounds.h"  // normalize_slice()
#include "../core/node.h"  // has_prev<>
#include "../core/view.h"  // views


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    /* Get the value at a particular index of a linked list, set, or dictionary. */
    template <typename View, typename T>
    PyObject* get_index(View* view, T index) {
        using Node = typename View::Node;

        // allow python-style negative indexing + boundschecking
        size_t norm_index = normalize_index(index, view->size, false);
        if (norm_index == MAX_SIZE_T && PyErr_Occurred()) {
            return nullptr;  // propagate error
        }

        // get node at index
        Node* curr = node_at_index(view, view->head, norm_index);

        // return a new reference to the node's value
        Py_INCREF(curr->value);
        return curr->value;
    }

    /* Get a value from a linked set or dictionary relative to a given sentinel
    value. */
    template <typename View>
    PyObject* get_relative(View* view, PyObject* sentinel, Py_ssize_t offset) {
        using Node = typename View::Node;

        // ensure offset is nonzero
        if (offset == 0) {
            PyErr_Format(PyExc_ValueError, "offset must be non-zero");
            return nullptr;
        } else if (offset < 0) {
            offset += 1;
        }

        // search for sentinel
        Node* curr = view->search(sentinel);
        if (curr == nullptr) {  // sentinel not found
            PyErr_Format(PyExc_KeyError, "%R is not contained in the set", sentinel);
            return nullptr;
        }

        // If we're iterating forward from the sentinel, then the process is the same
        // for singly- and doubly-linked lists
        if (offset > 0) {
            for (Py_ssize_t i = 0; i < offset; i++) {
                curr = static_cast<Node*>(curr->next);
                if (curr == nullptr) {
                    PyErr_Format(PyExc_IndexError, "offset %zd is out of range", offset);
                    return nullptr;
                }
            }
            Py_INCREF(curr->value);
            return curr->value;
        }

        // If we're iterating backwards and the list is doubly-linked, then we can
        // just use the `prev` pointer at each node
        if constexpr (has_prev<Node>::value) {
            for (Py_ssize_t i = 0; i > offset; i--) {
                curr = static_cast<Node*>(curr->prev);
                if (curr == nullptr) {
                    PyErr_Format(PyExc_IndexError, "offset %zd is out of range", offset);
                    return nullptr;
                }
            }
            Py_INCREF(curr->value);
            return curr->value;
        }

        // Otherwise, we have to start from the head and walk forward using a 2-pointer
        // approach.
        Node* lookahead = view->head;
        for (Py_ssize_t i = 0; i > offset; i--) {  // advance lookahead to offset
            lookahead = static_cast<Node*>(lookahead->next);
            if (lookahead == curr) {
                PyErr_Format(PyExc_IndexError, "offset %zd is out of range", offset);
                return nullptr;
            }
        }

        // advance both pointers until lookahead hits the end of the list
        Node* temp = view->head;
        while (lookahead != curr) {
            temp = static_cast<Node*>(temp->next);
            lookahead = static_cast<Node*>(lookahead->next);
        }
        Py_INCREF(temp->value);
        return temp->value;
    }

}


namespace Slice {

    /* Extract a slice from a linked list, set, or dictionary. */
    template <typename SliceProxy>
    auto extract(SliceProxy& slice) -> std::optional<typename SliceProxy::View> {
        using View = typename SliceProxy::View;
        using Node = typename SliceProxy::Node;

        // allocate a new view to hold the slice
        PyObject* specialization = slice.view()->specialization;
        Py_ssize_t max_size = slice.view()->max_size; 
        if (max_size >= 0) {
            max_size = static_cast<Py_ssize_t>(slice.length());
        }
        View result(max_size, specialization);

        // if slice is empty, return empty view
        if (slice.length() == 0) {
            return std::optional<View>(std::move(result));
        }

        // copy nodes from original view into result
        for (auto node : slice) {
            Node* copy = result.copy(node);
            if (copy == nullptr) {
                return std::nullopt;  // propagate
            }

            // link to slice
            if (slice.reverse()) {
                result.link(nullptr, copy, result.head);
            } else {
                result.link(result.tail, copy, nullptr);
            }
            if (PyErr_Occurred()) {
                result.recycle(copy);  // clean up staged node
                return std::nullopt;  // propagate
            }
        }

        return std::optional<View>(std::move(result));
    }

}


namespace Relative {

    // TODO: move get_relative() here
}


#endif // BERTRAND_STRUCTS_ALGORITHMS_GET_SLICE_H include guard
