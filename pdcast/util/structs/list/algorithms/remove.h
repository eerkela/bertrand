// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_REMOVE_H
#define BERTRAND_STRUCTS_ALGORITHMS_REMOVE_H

#include <Python.h>  // CPython API
#include "../core/node.h"  // has_prev<>
#include "../core/view.h"  // views


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    /* Remove the first occurrence of an item from a linked list. */
    template <typename NodeType, template <typename> class Allocator>
    void remove(ListView<NodeType, Allocator>& view, PyObject* item) {
        using Node = typename ListView<NodeType, Allocator>::Node;

        // find the node to remove
        Node* prev = nullptr;
        Node* curr = view.head;
        while (curr != nullptr) {
            Node* next = static_cast<Node*>(curr->next);

            // C API equivalent of the == operator
            int comp = PyObject_RichCompareBool(curr->value, item, Py_EQ);
            if (comp == -1) {  // comparison raised an exception
                return;  // propagate
            } else if (comp == 1) {  // found a match
                view.unlink(prev, curr, next);
                view.recycle(curr);
                return;
            }

            // advance to next node
            prev = curr;
            curr = next;
        }

        // item not found
        PyErr_Format(PyExc_ValueError, "%R not in list", item);
    }

    /* Remove an item from a linked set or dictionary. */
    template <typename View>
    inline void remove(View& view, PyObject* item) {
        _drop_setlike(view, item, true);  // propagate errors
    }

    /* Remove an item from a linked set or dictionary relative to a given sentinel
    value. */
    template <typename View>
    inline void remove_relative(View& view, PyObject* sentinel, Py_ssize_t offset) {
        _drop_relative(view, sentinel, offset, true);  // propagate errors
    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


// NOTE: these are reused for discard() as well


/* Implement both remove() and discard() for sets and dictionaries depending on error
handling flag. */
template <typename View>
void _drop_setlike(View& view, PyObject* item, bool raise) {
    using Node = typename View::Node;

    // search for node
    Node* curr = view.search(item);
    if (curr == nullptr) {  // item not found
        if (raise) {
            PyErr_Format(PyExc_KeyError, "%R not in set", item);
        }
        return;
    }

    // get previous node
    Node* prev;
    if constexpr (has_prev<Node>::value) {
        // NOTE: this is O(1) for doubly-linked sets and dictionaries because
        // we already have a pointer to the previous node.
        prev = static_cast<Node*>(curr->prev);
    } else {
        // NOTE: this is O(n) for singly-linked sets and dictionaries because we
        // have to traverse the whole list to find the previous node.
        prev = nullptr;
        Node* temp = view.head;
        while (temp != curr) {
            prev = temp;
            temp = static_cast<Node*>(temp->next);
        }
    }

    // unlink and free node
    view.unlink(prev, curr, static_cast<Node*>(curr->next));
    view.recycle(curr);
}


/* Implement both remove_relative() and discard_relative() depending on error handling
flag. */
template <typename View>
void _drop_relative(View& view, PyObject* sentinel, Py_ssize_t offset, bool raise) {
    using Node = typename View::Node;

    // ensure offset is nonzero
    if (offset == 0) {
        PyErr_Format(PyExc_ValueError, "offset must be non-zero");
        return;
    } else if (offset < 0) {
        offset += 1;
    }

    // search for sentinel
    Node* node = view.search(sentinel);
    if (node == nullptr) {
        if (raise) {  // sentinel not found
            PyErr_Format(PyExc_KeyError, "%R is not contained in the set", sentinel);
        }
        return;
    }

    // walk according to offset
    std::tuple<Node*, Node*, Node*> neighbors = relative_neighbors(
        &view, node, offset, false
    );
    Node* prev = std::get<0>(neighbors);
    Node* curr = std::get<1>(neighbors);
    Node* next = std::get<2>(neighbors);
    if (prev == nullptr  && curr == nullptr && next == nullptr) {
        if (raise) {  // walked off end of list
            PyErr_Format(PyExc_IndexError, "offset %zd is out of range", offset);
        }
        return;
    }

    // remove node between boundaries
    view.unlink(prev, curr, next);
    view.recycle(curr);
}


#endif  // BERTRAND_STRUCTS_ALGORITHMS_REMOVE_H include guard
