
// include guard prevents multiple inclusion
#ifndef SET_SLICE_H
#define SET_SLICE_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <utility>  // for std::pair
#include <view.h>  // for views
#include <get_slice.h>  // for _get_slice_direction()


// TODO: what about the case where the slice length does not match the sequence
// length and we're iterating backwards from the tail?  This only occurs if
// step == 1, so check the logic for that case.


// NOTE: slice assignment in general can be extremely complicated in Python,
// and is especially difficult to translate over to linked lists.  This is made
// worse in the case of sets and dictionaries, where the uniqueness of keys
// must be strictly enforced.  This opens up the possibility of errors
// occurring during the assignment, which must be handled gracefully.  Here's
// how this is achieved in the following algorithm:

//  1)  We remove (but do not free) all the nodes in the slice, and store them
//      in a temporary queue.  This removes the nodes from their current hash
//      table, but also allows them to be reinserted if an error occurs.
//  2)  We iterate through the sequence, inserting each item into the slice.
//      Errors can occur if an item is a duplicate, either within the input
//      iterable or in the rest of the list, or if one of the values is invalid
//      in some way (e.g. not hashable, or not a 2-tuple in the case of
//      dictionaries).  Note that since we removed the nodes in step 1, the
//      values that we are directly replacing are disregarded during these
//      checks.
//          a)  If an error occurs, we reverse the process and remove all the
//              nodes that we've added thus far.  This once again removes them
//              from the hash table and prevents collisions.  This time,
//              however, we directly free the nodes rather than staging them in
//              a recovery queue.
//          b)  We then iterate through the queue we populated in step 1 and
//              reinsert the original nodes into the list.  This returns the
//              list to its original state, and ensures that the hash table
//              is properly restored.
//  3)  If no error is encountered during the assignment, we iterate through
//      the queue and free all the nodes that we staged in step 1.

// Where slicing gets especially hairy is when the slice length does not match
// the sequence length, or when the slice does not represent a valid range.
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

// This seems consistent with the behavior we outlined above, but it makes
// things substantially more complicated and unintuitive.  This is a likely
// source of bugs, so we should be careful to test these cases thoroughly.


/////////////////////////
////    SET INDEX    ////
/////////////////////////


/* Set the value at a particular index of a singly-linked list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
void set_index_single(ViewType<NodeType>* view, size_t index, PyObject* item) {
    using Node = typename ViewType<NodeType>::Node;

    // allocate a new node
    Node* new_node = view->node(item);
    if (new_node == NULL) {
        return;
    }

    // iterate forwards from head
    Node* prev = NULL;
    Node* curr = view->head;
    for (size_t i = 0; i < index; i++) {
        prev = curr;
        curr = (Node*)curr->next;
    }
    Node* next = (Node*)curr->next;

    // replace node
    view->unlink(prev, curr, next);
    view->link(prev, new_node, next);
    if (PyErr_Occurred()) {  // error during link()
        view->link(prev, curr, next);  // restore list to original state
        view->recycle(new_node);  // free new node
        return;  // propagate error
    }

    // free old node
    view->recycle(curr);
}


/* Overwrite the value at a particular index of a singly-linked list. */
template <typename NodeType>
void set_index_single(ListView<NodeType>* view, size_t index, PyObject* item) {
    using Node = typename ListView<NodeType>::Node;

    // NOTE: because ListViews do not use a hash table to keep track of nodes,
    // we are free to overwrite the existing value directly, rather than
    // allocating a new node and linking it to the list.

    // iterate forwards from head
    Node* curr = view->head;
    for (size_t i = 0; i < index; i++) {
        curr = (Node*)curr->next;
    }

    // overwrite value
    PyObject* old = curr->value;
    Py_INCREF(item);
    curr->value = item;
    Py_DECREF(old);
}


/* Set the value at a particular index of a doubly-linked list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
void set_index_double(ViewType<NodeType>* view, size_t index, PyObject* item) {
    // if index is closer to head, use singly-linked version
    if (index <= view->size / 2) {
        set_index_single(view, index, item);
        return;
    }

    // otherwise, iterate backwards from tail
    using Node = typename ViewType<NodeType>::Node;

    // allocate a new node
    Node* new_node = view->node(item);
    if (new_node == NULL) {
        return;
    }

    // get neighboring nodes
    Node* next = NULL;
    Node* curr = view->tail;
    for (size_t i = view->size - 1; i > index; i--) {
        next = curr;
        curr = (Node*)curr->prev;
    }
    Node* prev = (Node*)curr->prev;

    // replace node
    view->unlink(prev, curr, next);
    view->link(prev, new_node, next);
    if (PyErr_Occurred()) {  // error during link()
        view->link(prev, curr, next);  // restore list to original state
        view->recycle(new_node);  // free new node
        return;  // propagate error
    }

    // free old node
    view->recycle(curr);
}


/* Overwrite the value at a particular index of a doubly-linked list. */
template <typename NodeType>
void set_index_double(ListView<NodeType>* view, size_t index, PyObject* item) {
    // if index is closer to head, use singly-linked version
    if (index <= view->size / 2) {
        set_index_single(view, index, item);
        return;
    }

    // otherwise, iterate backwards from tail
    using Node = typename ListView<NodeType>::Node;

    // NOTE: because ListViews do not use a hash table to keep track of nodes,
    // we are free to overwrite the existing value directly, rather than
    // allocating a new node and linking it to the list.

    // get existing node
    Node* curr = view->tail;
    for (size_t i = view->size - 1; i > index; i--) {
        curr = (Node*)curr->prev;
    }

    // overwrite value
    PyObject* old = curr->value;
    Py_INCREF(item);
    curr->value = item;
    Py_DECREF(old);
}


/////////////////////////
////    SET SLICE    ////
/////////////////////////


/* Set a slice within a singly-linked list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
void set_slice_single(
    ViewType<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
) {
    size_t abs_step = (size_t)abs(step);
    std::pair<size_t, size_t> bounds;

    // unpack iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(items, "can only assign an iterable");
    if (sequence == NULL) {  // TypeError(): items do not support sequence protocol
        return;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence);

    // determine direction of traversal to avoid backtracking
    bounds = _get_slice_direction_single(start, stop, step, view->size);
    if (PyErr_Occurred()) {  // invalid slice
        if (step == 1) {  // Python allows inserting slices in this case only
            _replace_slice_forward(
                view,
                (size_t)start,  // start inserting nodes at start index
                0,              // slice is of length 0
                seq_length,     // insert all items from sequence
                abs_step,
                sequence,
                (step < 0)
            );
        } else {
            PyErr_Format(
                PyExc_ValueError,
                "attempt to assign sequence of size %zd to extended slice of size 0",
                seq_length
            );
        }
        return;
    }

    // ensure that the slice is the same length as the sequence
    Py_ssize_t slice_length = (Py_ssize_t)bounds.second - (Py_ssize_t)bounds.first;
    slice_length = (abs(slice_length) / (Py_ssize_t)abs_step) + 1;
    if (step != 1 && slice_length != seq_length) {  // allow mismatch if step == 1
        PyErr_Format(
            PyExc_ValueError,
            "attempt to assign sequence of size %zd to extended slice of size %zd",
            seq_length,
            slice_length
        );
        return;
    }

    // replace existing nodes with new ones and rewind if an error occurs
    _replace_slice_forward(
        view,
        bounds.first,
        slice_length,
        seq_length,
        abs_step,
        sequence,
        (step < 0)
    );

    // release sequence
    Py_DECREF(sequence);
}


/* Overwrite a slice within a singly-linked list. */
template <typename NodeType>
void set_slice_single(
    ListView<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
) {
    size_t abs_step = (size_t)abs(step);
    std::pair<size_t, size_t> bounds;

    // unpack iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(items, "can only assign an iterable");
    if (sequence == NULL) {  // TypeError(): items do not support sequence protocol
        return;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence);

    // determine direction of traversal to avoid backtracking
    bounds = _get_slice_direction_single(start, stop, step, view->size);
    if (PyErr_Occurred) {  // invalid slice
        if (step == 1) {  // Python allows inserting slices in this case only
            _replace_slice_forward(
                view,
                (size_t)start,  // start inserting nodes at start index
                0,              // slice is of length 0
                seq_length,     // insert all items from sequence
                abs_step,
                sequence,
                (step < 0)
            );
        } else {
            PyErr_Format(
                PyExc_ValueError,
                "attempt to assign sequence of size %zd to extended slice of size 0",
                seq_length
            );
        }
        return;
    }

    // ensure that the slice is the same length as the sequence
    Py_ssize_t slice_length = (Py_ssize_t)bounds.second - (Py_ssize_t)bounds.first;
    slice_length = (abs(slice_length) / (Py_ssize_t)abs_step) + 1;
    if (slice_length != seq_length) {
        if (step == 1) {  // Python allows inserting slices in this case only
            _replace_slice_forward(
                view,
                bounds.first,
                slice_length,  // replace all nodes in slice
                seq_length,    // continue inserting the remaining items
                abs_step,
                sequence,
                (step < 0)
            );
        } else {
            PyErr_Format(
                PyExc_ValueError,
                "attempt to assign sequence of size %zd to extended slice of size %zd",
                seq_length,
                slice_length
            );
        }
        return;
    }

    // NOTE: Since ListViews don't employ a hash table, we are free to
    // directly overwrite the values in the slice.  This avoids the need to
    // allocate new nodes and link them to the list manually.
    _overwrite_slice_forward(
        view,
        bounds.first,
        seq_length,
        abs_step,
        sequence,
        (step < 0)
    );

    // release sequence
    Py_DECREF(sequence);
}


/* Set a slice within a doubly-linked list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
void set_slice_double(
    ViewType<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
) {
    size_t abs_step = (size_t)abs(step);
    std::pair<size_t, size_t> bounds;

    // unpack iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(items, "can only assign an iterable");
    if (sequence == NULL) {  // TypeError(): items do not support sequence protocol
        return;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence);

    // determine direction of traversal to avoid backtracking
    bounds = _get_slice_direction_single(start, stop, step, view->size);
    if (PyErr_Occurred()) {
        if (step == 1) {  // Python allows inserting slices in this case only
            if ((size_t)start <= view->size / 2) {  // closer to head
                _replace_slice_forward(
                    view,
                    (size_t)start,  // start inserting nodes at start index
                    0,              // slice is of length 0
                    seq_length,     // insert all items from sequence
                    abs_step,
                    sequence,
                    (step < 0)
                );
            } else {  // closer to tail
                _replace_slice_backward(
                    view,
                    (size_t)start,  // start inserting nodes at start index
                    0,              // slice is of length 0
                    seq_length,     // insert all items from sequence
                    abs_step,
                    sequence,
                    (step > 0)
                );
            }
        } else {
            PyErr_Format(
                PyExc_ValueError,
                "attempt to assign sequence of size %zd to extended slice of size 0",
                seq_length
            );
        }
        return;
    }

    // ensure that the slice is the same length as the sequence
    Py_ssize_t slice_length = (Py_ssize_t)bounds.second - (Py_ssize_t)bounds.first;
    slice_length = (abs(slice_length) / (Py_ssize_t)abs_step) + 1;
    if (step != 1 && slice_length != seq_length) {  // allow mismatch if step == 1
        PyErr_Format(
            PyExc_ValueError,
            "attempt to assign sequence of size %zd to extended slice of size %zd",
            seq_length,
            slice_length
        );
        return;
    }

    // replace existing nodes with new ones and rewind if an error occurs
    if (bounds.first <= bounds.second) {  // forward traversal
        _replace_slice_forward(
            view,
            bounds.first,
            slice_length,
            seq_length,
            abs_step,
            sequence,
            (step < 0)
        );
    } else {  // backward traversal
        _replace_slice_backward(
            view,
            bounds.first,
            slice_length,
            seq_length,
            abs_step,
            sequence,
            (step > 0)
        );
    }

    // release sequence
    Py_DECREF(sequence);
}


/* Overwrite a slice within a doubly-linked list. */
template <typename NodeType>
void set_slice_double(
    ListView<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
) {
    size_t abs_step = (size_t)abs(step);
    std::pair<size_t, size_t> bounds;

    // unpack iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(items, "can only assign an iterable");
    if (sequence == NULL) {  // TypeError(): items do not support sequence protocol
        return;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence);

    // determine direction of traversal to avoid backtracking
    bounds = _get_slice_direction_double(start, stop, step, view->size);
    if (PyErr_Occurred()) {  // invalid slice
        if (step == 1) {  // Python allows inserting slices in this case only
            if ((size_t)start <= view->size / 2) {  // closer to head
                _replace_slice_forward(
                    view,
                    (size_t)start,  // start inserting nodes at start index
                    0,              // slice is of length 0
                    seq_length,     // insert all items from sequence
                    abs_step,
                    sequence,
                    (step < 0)
                );
            } else {  // closer to tail
                _replace_slice_backward(
                    view,
                    (size_t)start,  // start inserting nodes at start index
                    0,              // slice is of length 0
                    seq_length,     // insert all items from sequence
                    abs_step,
                    sequence,
                    (step > 0)
                );
            }
        } else {
            PyErr_Format(
                PyExc_ValueError,
                "attempt to assign sequence of size %zd to extended slice of size 0",
                seq_length
            );
        }
        return;
    }

    // ensure that the slice is the same length as the sequence
    Py_ssize_t slice_length = (Py_ssize_t)bounds.second - (Py_ssize_t)bounds.first;
    slice_length = (abs(slice_length) / (Py_ssize_t)abs_step) + 1;
    if (slice_length != seq_length) {
        if (step == 1) {  // Python allows inserting slices in this case only
            if (bounds.first <= bounds.second) {  // closer to head
                _replace_slice_forward(
                    view,
                    view->head,
                    bounds.first,
                    slice_length,   // replace all nodes in slice
                    seq_length,     // continue inserting the remaining items
                    abs_step,
                    sequence,
                    (step < 0)
                );
            } else {  // closer to tail
                _replace_slice_backward(
                    view,
                    view->tail,
                    bounds.first,
                    slice_length,   // replace all nodes in slice
                    seq_length,     // continue inserting the remaining items
                    abs_step,
                    sequence,
                    (step > 0)
                );
            }
        } else {
            PyErr_Format(
                PyExc_ValueError,
                "attempt to assign sequence of size %zd to extended slice of size %zd",
                seq_length,
                slice_length
            );
        }
        return;
    }

    // NOTE: Since ListViews don't employ a hash table, we are free to
    // directly overwrite the values in the slice.  This avoids the need to
    // allocate new nodes and link them to the list manually.
    if (bounds.first <= bounds.second) {  // forward traversal
        _overwrite_slice_forward(
            view,
            bounds.first,
            seq_length,
            abs_step,
            sequence,
            (step < 0)
        );
    } else {  // backward traversal
        _overwrite_slice_backward(
            view,
            bounds.first,
            seq_length,
            abs_step,
            sequence,
            (step > 0)
        );
    }

    // release sequence
    Py_DECREF(sequence);
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Assign a slice from left to right, directly overwriting exisiting values. */
template <template <typename> class ViewType, typename NodeType>
void _overwrite_slice_forward(
    ViewType<NodeType>* view,
    size_t begin,
    Py_ssize_t seq_length,
    size_t abs_step,
    PyObject* sequence,
    bool reverse
) {
    // NOTE: this method should only be used in ListViews where the size of
    // iterable is the same as the length of the slice.  This guarantees that
    // we won't encounter an errors partway through the process.
    using Node = typename ViewType<NodeType>::Node;

    // get first node in slice by iterating from head
    Node* curr = view->head;
    for (size_t i = 0; i < begin; i++) {
        curr = (Node*)curr->next;
    }

    // iterate through sequence
    Py_ssize_t last_idx = seq_length - 1;
    for (Py_ssize_t seq_idx = 0; seq_idx < seq_length; seq_idx++) {
        // get item from sequence
        PyObject* item;
        if (reverse) {  // iterate from right to left
            item = PySequence_Fast_GET_ITEM(sequence, last_idx - seq_idx);
        } else {  // iterate from left to right
            item = PySequence_Fast_GET_ITEM(sequence, seq_idx);
        }

        // overwrite node's current value
        PyObject* old = curr->value;
        Py_INCREF(item);  // Fast_GET_ITEM() returns a borrowed reference
        curr->value = item;
        Py_DECREF(curr->value);

        // advance node according to step size
        if (seq_idx < last_idx) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                curr = (Node*)curr->next;
            }
        }
    }
}


/* Assign a slice from right to left, directly overwriting exisiting values. */
template <template <typename> class ViewType, typename NodeType>
void _overwrite_slice_backward(
    ViewType<NodeType>* view,
    size_t begin,
    size_t seq_length,
    size_t abs_step,
    PyObject* sequence,
    bool reverse
) {
    // NOTE: this method should only be used in ListViews where the size of
    // iterable is the same as the length of the slice.  This guarantees that
    // we won't encounter an errors partway through the process.
    using Node = typename ViewType<NodeType>::Node;

    // get first node in slice by iterating from tail
    Node* curr = view->tail;
    for (size_t i = view->size - 1; i > begin; i--) {
        curr = (Node*)curr->prev;
    }

    // iterate through sequence
    Py_ssize_t last_idx = seq_length - 1;
    for (Py_ssize_t seq_idx = 0; seq_idx < seq_length; seq_idx++) {
        // get item from sequence
        PyObject* item;
        if (reverse) {  // iterate from right to left
            item = PySequence_Fast_GET_ITEM(sequence, last_idx - seq_idx);
        } else {  // iterate from left to right
            item = PySequence_Fast_GET_ITEM(sequence, seq_idx);
        }

        // overwrite node's current value
        PyObject* old = curr->value;
        Py_INCREF(item);  // Fast_GET_ITEM() returns a borrowed reference
        curr->value = item;
        Py_DECREF(curr->value);

        // advance node according to step size
        if (seq_idx < last_idx) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                curr = (Node*)curr->prev;
            }
        }
    }
}


/* Assign a slice from left to right, replacing the existing nodes with new ones. */
template <template <typename> class ViewType, typename NodeType>
void _replace_slice_forward(
    ViewType<NodeType>* view,
    size_t begin,
    Py_ssize_t slice_length,
    Py_ssize_t seq_length,
    size_t abs_step,
    PyObject* sequence,
    bool reverse
) {
    using Node = typename ViewType<NodeType>::Node;

    // get first node in slice by iterating from head
    Node* prev = NULL;
    Node* curr = view->head;
    Node* next;
    for (size_t i = 0; i < begin; i++) {
        prev = curr;
        curr = (Node*)curr->next;
    }

    // remember beginning of slice so we can reset later
    Node* source = prev;
    std::queue<Node*> removed = new std::queue<Node*>();

    // loop 1: unlink nodes in slice
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    Py_ssize_t last_iter = slice_length - 1;
    for (Py_ssize_t i = 0; i < slice_length; i ++) {
        // unlink node
        next = (Node*)curr->next;
        view->unlink(prev, curr, next);
        removed.push(curr);  // push to recovery queue

        // advance node according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = (Node*)curr->next;
            }
        }
    }

    // loop 2: insert new nodes from sequence
    prev = source;  // reset to beginning of slice
    Py_ssize_t last_idx = seq_length - 1;
    for (Py_ssize_t seq_idx = 0; seq_idx < seq_length; seq_idx++) {
        // get item from sequence
        PyObject* item;
        if (reverse) {  // reverse sequence as we add nodes
            item = PySequence_Fast_GET_ITEM(sequence, last_idx - seq_idx);
        } else {
            item = PySequence_Fast_GET_ITEM(sequence, seq_idx);
        }

        // get next node in slice
        if (prev == NULL) {  // slice originates from head of list
            next = view->head;  // points to new head (head->next)
        } else {
            next = (Node*)prev->next;
        }

        // allocate a new node and link it to the list
        curr == _insert_node(view, item, prev, next);
        if (curr == NULL) {
            _undo_set_slice_forward(view, source, seq_idx, abs_step, removed);
            return;
        }

        // advance node according to step size
        if (seq_idx < last_idx) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                prev = curr;
                curr = (Node*)curr->next;
            }
        }
    }

    // loop 3: deallocate removed nodes
    for (size_t i = 0; i < removed.size(); i++) {
        curr = removed.front();
        removed.pop();
        view->recycle(curr);
    }
}


/* Assign a slice from right to left, replacing the existing nodes with new ones. */
template <template <typename> class ViewType, typename NodeType>
void _replace_slice_backward(
    ViewType<NodeType>* view,
    size_t begin,
    Py_ssize_t slice_length,
    Py_ssize_t seq_length,
    size_t abs_step,
    PyObject* sequence,
    bool reverse
) {
    using Node = typename ViewType<NodeType>::Node;

    // get first node in slice by iterating from tail
    Node* prev;
    Node* curr = view->tail;
    Node* next = NULL;
    for (size_t i = 0; i < begin; i++) {
        next = curr;
        curr = (Node*)curr->prev;
    }

    // remember beginning of slice so we can reset later
    Node* source = next;
    std::queue<Node*> removed = new std::queue<Node*>();

    // loop 1: unlink nodes in slice
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    Py_ssize_t last_iter = slice_length - 1;
    for (Py_ssize_t i = 0; i < slice_length; i++) {
        // unlink node
        prev = (Node*)curr->prev;
        view->unlink(prev, curr, next);
        removed.push(curr);  // push to recovery queue

        // advance node according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = (Node*)curr->prev;
            }
        }
    }

    // loop 2: insert new nodes from sequence
    next = source;  // reset to beginning of slice
    Py_ssize_t last_idx = seq_length - 1;
    for (Py_ssize_t seq_idx = 0; seq_idx < seq_length; seq_idx++) {
        // get item from sequence
        PyObject* item;
        if (reverse) {  // reverse sequence as we add nodes
            item = PySequence_Fast_GET_ITEM(sequence, last_idx - seq_idx);
        } else {
            item = PySequence_Fast_GET_ITEM(sequence, seq_idx);
        }

        // get previous node in slice
        if (next == NULL) {  // slice originates from tail of list
            prev = view->tail;  // points to new tail (tail->prev)
        } else {
            prev = (Node*)next->prev;
        }

        // allocate a new node and link it to the list
        curr == _insert_node(view, item, prev, next);
        if (curr == NULL) {
            _undo_set_slice_backward(  // replace original nodes
                view,
                removed,
                seq_idx,
                source,
                abs_step
            );
            return;
        }

        // advance node according to step size
        if (seq_idx < last_idx) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                next = curr;
                curr = (Node*)curr->prev;
            }
        }
    }

    // loop 3: deallocate removed nodes
    for (size_t i = 0; i < removed.size(); i++) {
        curr = removed.front();
        removed.pop();
        view->recycle(curr);
    }
}


/* Attempt to allocate a new node and insert it into the slice. */
template <template <typename> class ViewType, typename NodeType, typename Node>
inline Node* _insert_node(
    ViewType<NodeType>* view,
    PyObject* item,
    Node* prev,
    Node* next
) {
    // allocate a new node
    Node* curr = view->node(item);
    if (curr == NULL) {  // error during init()
        return NULL;
    }

    // link to list
    view->link(prev, curr, next);
    if (PyErr_Occurred()) {  // item violates hash invariant
        view->recycle(curr);  // clean up orphaned node
        return NULL;
    }

    return curr;
}


/* Undo a slice assignment. */
template <template <typename> class ViewType, typename NodeType, typename Node>
void _undo_set_slice_forward(
    ViewType<NodeType>* view,
    Node* source,
    Py_ssize_t n_staged,
    size_t abs_step,
    std::queue<Node*> original
) {
    Node* prev = source;
    Node* curr;
    Node* next;
    if (prev == NULL) {  // slice originates from head of list
        curr = view->head;  // points to new head (head->next)
    } else {
        curr = (Node*)prev->next;
    }

    // loop 3: unlink and deallocate nodes that have already been added to slice
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    Py_ssize_t last_iter = n_staged - 1;
    for (Py_ssize_t i = 0; i < n_staged; i++) {
        // unlink node
        next = (Node*)curr->next;
        view->unlink(prev, curr, next);
        view->recycle(curr);  // free node

        // advance node according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = (Node*)curr->next;
            }
        }
    }

    // loop 4: reinsert original nodes
    prev = source;
    size_t last_queued = original.size() - 1;
    for (size_t i = 0; i < original.size(); i++) {
        // get next node from queue
        curr = original.front();
        original.pop();

        // link to list
        if (prev == NULL) {  // slice originates from head of list
            next = view->head;  // points to new head (head->next)
        } else {
            next = (Node*)prev->next;
        }
        view->link(prev, curr, next);  // NOTE: cannot cause error

        // advance node according to step size
        if (i < last_queued) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                prev = curr;
                curr = (Node*)curr->next;
            }
        }
    }
}


/* Undo a slice assignment. */
template <template <typename> class ViewType, typename NodeType, typename Node>
void _undo_set_slice_backward(
    ViewType<NodeType>* view,
    Node* source,
    Py_ssize_t n_staged,
    size_t abs_step,
    std::queue<Node*> original
) {
    Node* prev;
    Node* curr;
    Node* next = source;
    if (next == NULL) {  // slice originates from tail of list
        curr = view->tail;  // points to new tail (tail->prev)
    } else {
        curr = (Node*)next->prev;
    }

    // loop 3: unlink and deallocate nodes that have already been added to slice
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    Py_ssize_t last_iter = n_staged - 1;
    for (Py_ssize_t i = 0; i < n_staged; i++) {
        // unlink node
        prev = (Node*)curr->prev;
        view->unlink(prev, curr, next);
        view->recycle(curr);  // free node

        // advance node according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                next = curr;
                curr = (Node*)curr->prev;
            }
        }
    }

    // loop 4: reinsert original nodes
    next = source;
    size_t last_queued = original.size() - 1;
    for (size_t i = 0; i < original.size(); i++) {
        // get next node from queue
        curr = original.front();
        original.pop();

        // link to list
        if (next == NULL) {  // slice originates from tail of list
            prev = view->tail;  // points to new tail (tail->prev)
        } else {
            prev = (Node*)next->prev;
        }
        view->link(prev, curr, next);  // NOTE: cannot cause error

        // advance node according to step size
        if (i < last_queued) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                next = curr;
                curr = (Node*)curr->prev;
            }
        }
    }
}


#endif // SET_SLICE_H include guard
