// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_SET_SLICE_H
#define BERTRAND_STRUCTS_ALGORITHMS_SET_SLICE_H

#include <cstddef>  // size_t
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/bounds.h"  // normalize_slice()
#include "../core/node.h"  // has_prev<>
#include "../core/view.h"  // views


// NOTE: slice assignment in general can be extremely complicated in Python,
// and is especially difficult to translate over to linked lists.  This is made
// worse in the case of sets and dictionaries, where the uniqueness of keys
// must be strictly enforced.  This opens up the possibility of errors
// occurring during the assignment, which must be handled gracefully.  Here's
// how this is achieved in the following algorithm:

//  1)  Allocate a contiguous array of nodes to hold the contents of the slice.
//      This allows us to recover the original list in the event of an error.
//  2)  Remove all the nodes in the slice, transferring them to the recovery
//      array.  This removes the nodes from consideration during uniqueness
//      checks.
//  3)  Iterate through the sequence, inserting each item into the slice.
//      Errors can occur if an item is invalid in some way (e.g. not hashable,
//      unique, or of a compatible type).
//        a)  If an error occurs, we reverse the process and remove all the
//            nodes that we've added thus far.  Instead of storing them in
//            a recovery array, however, we directly free them.
//        b)  We then iterate through the recovery array and reinsert the
//            original nodes into the list.  This returns the list to its
//            original state.
//  4)  If no error is encountered, iterate through the recovery array and free
//      all the nodes that we staged in step 2.

// Where slicing gets especially hairy is when the slice length does not match
// the sequence length, or when the slice does not represent a valid interval.
// Python allows these cases, but the behavior is not always intuitive.  For
// example, the following code is valid:

// >>> l = [1, 2, 3, 4, 5]
// >>> l[1:3] = [6, 7, 8, 9, 10]  # slice length does not match sequence length
// >>> l
// [1, 6, 7, 8, 9, 10, 4, 5]
// >>> l[3:1:1] = [11, 12, 13]  # empty slice
// >>> l
// [1, 6, 7, 11, 12, 13, 8, 9, 10, 4, 5]
// >>> l[1:3] = []  # empty sequence
// >>> l
// [1, 11, 12, 13, 8, 9, 10, 4, 5]


/* Set a ValueError in case of an invalid slice assignment. */
inline void _invalid_slice_error(size_t seq_length, size_t slice_length) {
    PyErr_Format(
        PyExc_ValueError,
        "attempt to assign sequence of size %zu to extended slice of size %zu",
        seq_length,
        slice_length
    );
}


// forward declare RecoveryArray for use in Slice::replace()
template <typename Node>
struct RecoveryArray;


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    /* Set the value at a particular index of a linked list, set, or dictionary. */
    template <typename View, typename T>
    void set_index(View& view, T index, PyObject* item) {
        using Node = typename View::Node;

        // allow python-style negative indexing + boundschecking
        size_t idx = normalize_index(index, view.size, false);
        if (idx == MAX_SIZE_T && PyErr_Occurred()) {
            return;  // propagate error
        }

        // allocate a new node
        Node* new_node = view.node(item);
        if (new_node == nullptr) {
            return;  // propagate error
        }

        // get neighboring nodes at index
        std::tuple<Node*, Node*, Node*> bounds = neighbors(&view, view.head, idx);
        Node* prev = std::get<0>(bounds);
        Node* old_node = std::get<1>(bounds);
        Node* next = std::get<2>(bounds);

        // replace node
        view.unlink(prev, old_node, next);
        view.link(prev, new_node, next);
        if (PyErr_Occurred()) {  // restore list to original state
            view.link(prev, old_node, next);
            view.recycle(new_node);
            return;  // propagate error
        }

        // free old node
        view.recycle(old_node);
    }

}


namespace SliceOps {

    /* Replace a slice from a linked list, set, or dictionary. */
    template <typename SliceProxy>
    void set(SliceProxy& slice, PyObject* items) {
        using Node = typename SliceProxy::Node;

        // unpack iterable into reversible sequence
        PyObject* sequence = PySequence_Fast(items, "can only assign an iterable");
        if (sequence == nullptr) {
            return;  // propagate TypeError: can only assign an iterable
        }

        // check for no-op
        size_t seq_length = static_cast<size_t>(PySequence_Fast_GET_SIZE(sequence));
        if (slice.length() == 0 && seq_length == 0) {
            Py_DECREF(sequence);
            return;
        }

        // check slice length matches sequence length
        if (slice.length() != seq_length && slice.step() != 1) {
            // NOTE: Python allows forced insertion if and only if the step size is 1
            PyErr_Format(
                PyExc_ValueError,
                "attempt to assign sequence of size %zu to extended slice of size %zu",
                seq_length,
                slice.length()
            );
            Py_DECREF(sequence);
            return;
        }

        // allocate recovery array
        RecoveryArray<Node> recovery(slice.length());
        if (PyErr_Occurred()) {  // error during array allocation
            Py_DECREF(sequence);
            return;
        }

        // loop 1: remove current nodes in slice
        for (auto iter = slice.iter(); iter != iter.end(); ++iter) {
            Node* node = iter.remove();  // remove node from list
            Node::init_copy(&recovery[iter.index()], node);  // copy to recovery array
            slice.view().recycle(node);  // recycle original node
        }

        // TODO: this implementation doesn't work if the slice is empty and the
        // sequence is not.
        // -> seems to create wierd reversing/misalignment behavior in the list
        // -> slice parameters are not properly normalized


        // TODO: move the contents of loop 2 into a private helper, and then remove
        // the _undo helper.  This will densify the code and make it easier to read.

        // loop 2: insert new nodes from sequence into vacated slice
        for (auto iter = slice.iter(seq_length); iter != iter.end(); ++iter) {
            // NOTE: PySequence_Fast_GET_ITEM() returns a borrowed reference (no
            // DECREF required)
            PyObject* item;
            if (iter.inverted()) {  // count from the back
                size_t idx = seq_length - 1 - iter.index();
                item = PySequence_Fast_GET_ITEM(sequence, idx);
            } else {  // count from the front
                item = PySequence_Fast_GET_ITEM(sequence, iter.index());
            }

            // allocate a new node for the item
            Node* new_node = slice.view().node(item);
            if (new_node == nullptr) {
                _undo(slice, recovery, iter.index());  // recover original list
                Py_DECREF(sequence);
                return;
            }

            // insert node into slice at current index
            iter.insert(new_node);
            if (PyErr_Occurred()) {
                slice.view().recycle(new_node);  // free new node
                _undo(slice, recovery, iter.index());  // recover original list
                Py_DECREF(sequence);
                return;
            }
        }

        // loop 3: deallocate removed nodes
        for (size_t i = 0; i < slice.length(); i++) {
            Node::teardown(&recovery[i]);
        }

        Py_DECREF(sequence);
    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* A raw, contiguous memory block that nodes can be copied into and out of in case
of an error. */
template <typename Node>
struct RecoveryArray {
    Node* nodes;
    size_t length;

    /* Allocate a contiguous array of nodes. */
    RecoveryArray(size_t length) : length(length) {
        nodes = static_cast<Node*>(malloc(sizeof(Node) * length));
        if (nodes == nullptr) {
            PyErr_NoMemory();  // set a Python exception
        }
    }

    /* Tear down all nodes and free the recovery array. */
    ~RecoveryArray() {
        if (nodes != nullptr) {
            free(nodes);
        }
    }

    /* Index the array to access a particular node. */
    Node& operator[](size_t index) {
        return nodes[index];
    }

};


/* Undo a call to Slice::replace() in the event of an error. */
template <typename SliceProxy, typename Node>
void _undo(SliceProxy& slice, RecoveryArray<Node>& recovery, size_t n_staged) {
    // loop 3: remove nodes that have already been added to slice
    for (auto iter = slice.iter(n_staged); iter != iter.end(); ++iter) {
        Node* node = iter.remove();  // remove node from list
        slice.view().recycle(node);  // return node to allocator
    }

    // loop 4: reinsert original nodes
    for (auto iter = slice.iter(); iter != iter.end(); ++iter) {
        Node* node = slice.view().copy(&recovery[iter.index()]);  // copy from recovery
        Node::teardown(&recovery[iter.index()]);  // release recovery node
        iter.insert(node);  // insert into list
    }
}


#endif // BERTRAND_STRUCTS_ALGORITHMS_SET_SLICE_H include guard
