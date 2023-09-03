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

    /* Set a slice within a linked list, set, or dictionary. */
    template <typename View>
    void set_slice(
        View* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) {
        // unpack iterable into a reversible sequence
        PyObject* sequence = PySequence_Fast(items, "can only assign an iterable");
        if (sequence == nullptr) {
            return;  // propagate TypeError: can only assign an iterable
        }

        // get direction in which to traverse slice that minimizes iterations
        std::pair<size_t, size_t> bounds = normalize_slice(view, start, stop, step);
        size_t begin = bounds.first;
        size_t end = bounds.second;
        size_t abs_step = static_cast<size_t>(llabs(step));
        size_t seq_length = static_cast<size_t>(PySequence_Fast_GET_SIZE(sequence));
        if (begin == MAX_SIZE_T && end == MAX_SIZE_T && PyErr_Occurred()) {
            PyErr_Clear();  // swallow error
            if (step == 1) {  // Python allows inserting slices in this case only
                _replace_slice(
                    view,
                    (size_t)start,  // start inserting nodes at start index
                    (size_t)stop,   // determines whether to traverse from head/tail
                    abs_step,
                    0,              // slice is of length 0
                    seq_length,     // insert all items from sequence
                    sequence,
                    (start > stop)  // reverse if traversing from tail
                );
            } else {
                _invalid_slice_error(seq_length, 0);  // re-raise with correct message
            }
            Py_DECREF(sequence);  // release sequence
            return;
        }

        // get number of nodes in slice and ensure that it matches sequence length
        size_t length = slice_length(begin, end, abs_step);
        if (step != 1 && length != seq_length) {  // allow mismatch if step == 1
            _invalid_slice_error(seq_length, length);
            Py_DECREF(sequence);  // release sequence
            return;
        }

        // replace nodes in slice and rewind if an error occurs
        _replace_slice(
            view,
            begin,
            end,
            abs_step,
            length,
            seq_length,
            sequence,
            (step < 0) ^ (begin > end)  // reverse if sign of step != iter direction
        );

        // release sequence
        Py_DECREF(sequence);
    }

}


namespace SliceOps {

    /* Replace a slice from a linked list, set, or dictionary. */
    template <typename SliceProxy>
    void replace(SliceProxy& slice, PyObject* items) {
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
        if (slice.length() != seq_length && slice.step != 1) {
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
        for (auto iter = slice.begin(1), end = slice.end(); iter != end; ++iter) {
            Node* node = iter.remove();  // remove node from list
            Node::init_copy(&recovery[iter.index()], node);  // copy to recovery array
            slice.view().recycle(node);  // recycle original node
        }

        // TODO: this implementation doesn't work if the slice is empty and the
        // sequence is not.  In this case, the loop below will never execute.
        // -> need to find a way to handle this.
        // -> use the optional parameters to begin()/end()

        // TODO: move the contents of loop 2 into a private helper, and then remove
        // the _undo helper.  This will densify the code and make it easier to read.

        // loop 2: insert new nodes from sequence into vacated slice
        for (auto iter = slice.begin(), end = slice.end(); iter != end; ++iter) {
            // NOTE: PySequence_Fast_GET_ITEM() returns a borrowed reference (no
            // DECREF required)
            PyObject* item;
            if (slice.reverse()) {  // count from the back
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
    for (auto iter = slice.begin(1), end = slice.end(n_staged); iter != end; ++iter) {
        Node* node = iter.remove();  // remove node from list
        slice.view().recycle(node);  // return node to allocator
    }

    // loop 4: reinsert original nodes
    for (auto iter = slice.begin(), end = slice.end(); iter != end; ++iter) {
        Node* node = slice.view().copy(&recovery[iter.index()]);  // copy from recovery
        Node::teardown(&recovery[iter.index()]);  // release recovery node
        iter.insert(node);  // insert into list
    }
}












/* Replace a slice in a linked list, set, or dictionary. */
template <typename View>
void _replace_slice(
    View* view,
    size_t begin,
    size_t end,
    size_t abs_step,
    size_t slice_length,
    size_t seq_length,
    PyObject* sequence,
    bool reverse
) {
    using Node = typename View::Node;

    // allocate recovery array
    Node* recovery = new Node[slice_length];

    // NOTE: the recovery array contains a contiguous block of empty nodes that we
    // copy nodes into and out of during the assignment.  Since these are not managed
    // by the original allocator, they are not subject to any size restrictions.

    // replace existing nodes with new ones and rewind if an error occurs
    if constexpr (has_prev<Node>::value) {
        if (begin > end) {  // backward traversal
            _replace_slice_backward(
                view,
                begin,
                abs_step,
                slice_length,
                seq_length,
                sequence,
                recovery,
                reverse
            );
            delete[] recovery;  // free recovery array
            return;
        }
    }

    // forward traversal
    _replace_slice_forward(
        view,
        begin,
        abs_step,
        slice_length,
        seq_length,
        sequence,
        recovery,
        reverse
    );

    // free recovery array
    delete[] recovery;
}


/* Assign a slice from left to right, replacing the existing nodes with new ones. */
template <typename View, typename Node>
void _replace_slice_forward(
    View* view,
    const size_t begin,
    const size_t abs_step,
    const size_t slice_length,
    const size_t seq_length,
    PyObject* sequence,
    Node* recovery,
    const bool reverse
) {
    // get first node in slice by iterating from head
    Node* prev = nullptr;
    Node* curr = view->head;
    Node* next;
    for (size_t i = 0; i < begin; i++) {
        prev = curr;
        curr = static_cast<Node*>(curr->next);
    }

    // remember beginning of slice so we can reset later
    Node* source = prev;

    // loop 1: unlink current nodes in slice
    for (size_t i = 0; i < slice_length; i++) {
        next = static_cast<Node*>(curr->next);
        Node::init_copy(&recovery[i], curr);  // copy to recovery array
        view->unlink(prev, curr, next);
        view->recycle(curr);  // return node to allocator
        curr = next;

        // advance according to step size
        if (i < slice_length - 1) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step - 1; j++) {
                prev = curr;
                curr = static_cast<Node*>(curr->next);
            }
        }
    }

    // loop 2: insert new nodes from sequence
    prev = source;  // reset to beginning of slice
    for (size_t seq_idx = 0; seq_idx < seq_length; seq_idx++) {
        PyObject* item;  // get item from sequence
        if (reverse) {  // reverse sequence as we add nodes
            item = PySequence_Fast_GET_ITEM(sequence, seq_length - 1 - seq_idx);
        } else {
            item = PySequence_Fast_GET_ITEM(sequence, seq_idx);
        }

        // get next node in slice
        if (prev == nullptr) {  // slice originates from head of list
            next = view->head;  // points to new head (head->next)
        } else {
            next = static_cast<Node*>(prev->next);
        }

        // allocate a new node and link it to the list
        curr = _insert_node(view, item, prev, next);
        if (curr == nullptr) {
            _undo_set_slice_forward(
                view,
                source,
                slice_length,
                seq_idx,
                abs_step,
                recovery
            );
            return;
        }

        // advance according to step size
        if (seq_idx < seq_length - 1) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                prev = curr;
                curr = static_cast<Node*>(curr->next);
            }
        }
    }

    // loop 3: deallocate removed nodes
    for (size_t i = 0; i < slice_length; i++) {
        Node::teardown(&recovery[i]);
    }
}


/* Assign a slice from right to left, replacing the existing nodes with new ones. */
template <typename View, typename Node>
void _replace_slice_backward(
    View* view,
    const size_t begin,
    const size_t abs_step,
    const size_t slice_length,
    const size_t seq_length,
    PyObject* sequence,
    Node* recovery,
    const bool reverse
) {
    // get first node in slice by iterating from tail
    Node* prev;
    Node* curr = view->tail;
    Node* next = nullptr;
    for (size_t i = view->size - 1; i > begin; i--) {
        next = curr;
        curr = static_cast<Node*>(curr->prev);
    }

    // remember beginning of slice so we can reset later
    Node* source = next;

    // loop 1: unlink nodes in slice
    for (size_t i = 0; i < slice_length; i++) {
        prev = static_cast<Node*>(curr->prev);
        Node::init_copy(&recovery[i], curr);  // push to recovery queue
        view->unlink(prev, curr, next);
        view->recycle(curr);  // node can be reused immediately
        curr = prev;

        // advance according to step size
        if (i < slice_length - 1) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step - 1; j++) {
                next = curr;
                curr = static_cast<Node*>(curr->prev);
            }
        }
    }

    // loop 2: insert new nodes from sequence
    next = source;  // reset to beginning of slice
    for (size_t seq_idx = 0; seq_idx < seq_length; seq_idx++) {
        PyObject* item;  // get item from sequence
        if (reverse) {  // reverse sequence as we add nodes
            item = PySequence_Fast_GET_ITEM(sequence, seq_length - 1 - seq_idx);
        } else {
            item = PySequence_Fast_GET_ITEM(sequence, seq_idx);
        }

        // get previous node in slice
        if (next == nullptr) {  // slice originates from tail of list
            prev = view->tail;  // points to new tail (tail->prev)
        } else {
            prev = static_cast<Node*>(next->prev);
        }

        // allocate a new node and link it to the list
        curr = _insert_node(view, item, prev, next);
        if (curr == nullptr) {
            _undo_set_slice_backward(  // replace original nodes
                view,
                source,
                slice_length,
                seq_idx,
                abs_step,
                recovery
            );
            return;
        }

        // advance according to step size
        if (seq_idx < seq_length - 1) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                next = curr;
                curr = static_cast<Node*>(curr->prev);
            }
        }
    }

    // loop 3: deallocate removed nodes
    for (size_t i = 0; i < slice_length; i++) {
        Node::teardown(&recovery[i]);
    }
}


/* Attempt to allocate a new node and insert it into the slice. */
template <typename View, typename Node>
inline Node* _insert_node(View* view, PyObject* item, Node* prev, Node* next) {
    // allocate a new node
    Node* curr = view->node(item);
    if (curr == nullptr) {  // error during init()
        return nullptr;
    }

    // link to list
    view->link(prev, curr, next);
    if (PyErr_Occurred()) {  // item violates hash invariant
        view->recycle(curr);  // clean up orphaned node
        return nullptr;
    }

    return curr;
}


/* Undo a slice assignment. */
template <typename View, typename Node>
void _undo_set_slice_forward(
    View* view,
    Node* source,
    const size_t slice_length,
    const size_t n_staged,
    const size_t abs_step,
    Node* recovery
) {
    Node* prev = source;
    Node* curr;
    Node* next;
    if (prev == nullptr) {  // slice originates from head of list
        curr = view->head;  // points to new head (head->next)
    } else {
        curr = static_cast<Node*>(prev->next);
    }

    // loop 3: unlink and deallocate nodes that have already been added to slice
    for (size_t i = 0; i < n_staged; i++) {
        next = static_cast<Node*>(curr->next);
        view->unlink(prev, curr, next);
        view->recycle(curr);  // return node to allocator
        curr = next;

        // advance according to step size
        if (i < n_staged - 1) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step - 1; j++) {
                prev = curr;
                curr = static_cast<Node*>(curr->next);
            }
        }
    }

    // loop 4: reinsert original nodes
    prev = source;
    for (size_t i = 0; i < slice_length; i++) {
        curr = view->copy(&recovery[i]);  // copy recovery node
        Node::teardown(&recovery[i]);

        // link to list
        if (prev == nullptr) {  // slice originates from head of list
            next = view->head;  // points to new head (head->next)
        } else {
            next = static_cast<Node*>(prev->next);
        }
        view->link(prev, curr, next);  // NOTE: cannot cause error

        // advance according to step size
        if (i < slice_length - 1) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                prev = curr;
                curr = static_cast<Node*>(curr->next);
            }
        }
    }
}


/* Undo a slice assignment. */
template <typename View, typename Node>
void _undo_set_slice_backward(
    View* view,
    Node* source,
    const size_t slice_length,
    const size_t n_staged,
    const size_t abs_step,
    Node* recovery
) {
    Node* prev;
    Node* curr;
    Node* next = source;
    if (next == nullptr) {  // slice originates from tail of list
        curr = view->tail;  // points to new tail (tail->prev)
    } else {
        curr = static_cast<Node*>(next->prev);
    }

    // loop 3: unlink and deallocate nodes that have already been added to slice
    for (size_t i = 0; i < n_staged; i++) {
        prev = static_cast<Node*>(curr->prev);
        view->unlink(prev, curr, next);
        view->recycle(curr);  // return node to allocator
        curr = prev;

        // advance according to step size
        if (i < n_staged - 1) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step - 1; j++) {
                next = curr;
                curr = static_cast<Node*>(curr->prev);
            }
        }
    }

    // loop 4: reinsert original nodes
    next = source;
    for (size_t i = 0; i < slice_length; i++) {
        curr = view->copy(&recovery[i]);  // copy recovery node
        Node::teardown(&recovery[i]);

        // link to list
        if (next == nullptr) {  // slice originates from tail of list
            prev = view->tail;  // points to new tail (tail->prev)
        } else {
            prev = static_cast<Node*>(next->prev);
        }
        view->link(prev, curr, next);  // NOTE: cannot cause error

        // advance according to step size
        if (i < slice_length - 1) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                next = curr;
                curr = static_cast<Node*>(curr->prev);
            }
        }
    }
}


#endif // BERTRAND_STRUCTS_ALGORITHMS_SET_SLICE_H include guard
