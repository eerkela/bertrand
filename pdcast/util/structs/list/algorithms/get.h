// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_GET_SLICE_H
#define BERTRAND_STRUCTS_ALGORITHMS_GET_SLICE_H

#include <cstddef>  // size_t
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

    /* Extract a slice from a linked list, set, or dictionary. */
    template <typename View>
    inline View* get_slice(
        View* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step
    ) {
        using Node = typename View::Node;

        // get direction in which to traverse slice that minimizes iterations
        std::pair<size_t, size_t> bounds = normalize_slice(view, start, stop, step);
        size_t begin = bounds.first;
        size_t end = bounds.second;
        if (begin == MAX_SIZE_T && end == MAX_SIZE_T && PyErr_Occurred()) {
            PyErr_Clear();  // swallow error
            return new View();  // Python returns an empty list in this case
        }

        // get number of nodes in slice
        size_t abs_step = static_cast<size_t>(llabs(step));
        size_t length = slice_length(begin, end, abs_step);

        // create a new view to hold the slice
        View* slice;
        try {
            if (view->max_size < 0) {
                slice = new View(view->max_size, view->specialization);
            } else {
                slice = new View(length, view->specialization);
            }
        } catch (const std::bad_alloc&) {
            PyErr_NoMemory();
            return nullptr;
        }

        // NOTE: if the slice is closer to the end of a doubly-linked list, we can
        // iterate from the tail to save time.
        if constexpr (has_prev<Node>::value) {
            if (begin > end) {
                // backward traversal
                return _extract_slice_backward(
                    view,
                    slice,
                    begin,
                    length,
                    abs_step,
                    (step > 0)
                );
            }
        }

        // forward traversal
        return _extract_slice_forward(
            view,
            slice,
            begin,
            length,
            abs_step,
            (step < 0)
        );
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
    auto get_(SliceProxy* slice) -> std::optional<typename SliceProxy::View> {
        using View = typename SliceProxy::View;
        using Node = typename SliceProxy::Node;

        // create a new view to hold the slice
        PyObject* specialization = slice->view->specialization;
        Py_ssize_t max_size = slice->view->max_size; 
        if (max_size >= 0) {
            max_size = static_cast<Py_ssize_t>(slice->length);
        }

        // allocate a new view to hold the slice
        View result(max_size, specialization);

        // copy nodes from original view into result
        for (auto iter = slice->begin(), end = slice->end(); iter != end; ++iter) {
            Node* copy = result.copy(*iter);
            if (copy == nullptr) {  // error during copy()
                return std::nullopt;  // propagate error
            }

            // link to slice
            if (slice->reverse) {
                result.link(nullptr, copy, result.head);
            } else {
                result.link(result.tail, copy, nullptr);
            }
            if (PyErr_Occurred()) {
                result.recycle(copy);  // clean up staged node
                return std::nullopt;  // propagate error
            }
        }

        return std::optional<View>(std::move(result));
    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Extract a slice from left to right. */
template <typename View>
View* _extract_slice_forward(
    View* view,
    View* slice,
    size_t begin,
    size_t slice_length,
    size_t abs_step,
    bool reverse
) {
    using Node = typename View::Node;

    // get first node in slice by iterating from head
    Node* curr = view->head;
    for (size_t i = 0; i < begin; i++) {
        curr = static_cast<Node*>(curr->next);
    }

    // copy nodes from original view
    for (size_t i = 0; i < slice_length; i++) {
        Node* copy = slice->copy(curr);
        if (copy == nullptr) {  // error during copy()
            delete slice;  // clean up staged slice
            return nullptr;  // propagate error
        }

        // link to slice
        if (reverse) {  // reverse slice as we add nodes
            slice->link(nullptr, copy, slice->head);
        } else {
            slice->link(slice->tail, copy, nullptr);
        }
        if (PyErr_Occurred()) {
            slice->recycle(copy);  // clean up staged node
            delete slice;
            return nullptr;  // propagate error
        }

        // advance according to step size
        if (i < slice_length - 1) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                curr = static_cast<Node*>(curr->next);
            }
        }
    }

    return slice;
}


/* Extract a slice from right to left. */
template <typename View>
View* _extract_slice_backward(
    View* view,
    View* slice,
    size_t begin,
    size_t slice_length,
    size_t abs_step,
    bool reverse
) {
    using Node = typename View::Node;

    // get first node in slice by iterating from tail
    Node* curr = view->tail;
    for (size_t i = view->size - 1; i > begin; i--) {
        curr = static_cast<Node*>(curr->prev);
    }

    // copy nodes from original view
    for (size_t i = 0; i < slice_length; i++) {
        Node* copy = slice->copy(curr);
        if (copy == nullptr) {  // error during copy()
            delete slice;  // clean up staged slice
            return nullptr;  // propagate error
        }

        // link to slice
        if (reverse) {  // reverse slice as we add nodes
            slice->link(nullptr, copy, slice->head);
        } else {
            slice->link(slice->tail, copy, nullptr);
        }
        if (PyErr_Occurred()) {
            slice->recycle(copy);  // clean up staged node
            delete slice;
            return nullptr;  // propagate error
        }

        // advance according to step size
        if (i < slice_length - 1) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                curr = static_cast<Node*>(curr->prev);
            }
        }
    }

    return slice;
}


#endif // BERTRAND_STRUCTS_ALGORITHMS_GET_SLICE_H include guard
