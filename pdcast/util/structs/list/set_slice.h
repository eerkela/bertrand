
// include guard prevents multiple inclusion
#ifndef SET_SLICE_H
#define SET_SLICE_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <utility>  // for std::pair
#include "node.h"  // for nodes
#include "view.h"  // for views


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


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Set the value at a particular index of a linked list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
void set_index(ViewType<NodeType>* view, size_t index, PyObject* item) {
    using Node = typename ViewType<NodeType>::Node;
    Node* prev;
    Node* curr;
    Node* next;

    // allocate a new node
    Node* new_node = view->node(item);
    if (new_node == nullptr) {
        return;
    }

    // get neighboring nodes
    if constexpr (is_doubly_linked<Node>::value) {
        // NOTE: if the list is doubly-linked, then we can iterate from either
        // end to find the preceding node.
        if (index > view->size / 2) {  // backward traversal
            next = nullptr;
            curr = view->tail;
            for (size_t i = view->size - 1; i > index; i--) {
                next = curr;
                curr = static_cast<Node*>(curr->prev);
            }
            prev = static_cast<Node*>(curr->prev);

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
            return;
        }
    }

    // iterate forwards from head
    prev = nullptr;
    curr = view->head;
    for (size_t i = 0; i < index; i++) {
        prev = curr;
        curr = static_cast<Node*>(curr->next);
    }
    next = static_cast<Node*>(curr->next);

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


/* Overwrite the value at a particular index of a linked list. */
template <typename NodeType>
void set_index(ListView<NodeType>* view, size_t index, PyObject* item) {
    using Node = typename ListView<NodeType>::Node;
    Node* curr;
    PyObject* old_value;

    // NOTE: if the list is doubly linked and the index is closer to the tail,
    // we can iterate from the tail to save time.
    if constexpr (is_doubly_linked<Node>::value) {
        if (index > view->size / 2) {  // backward traversal
            curr = view->tail;
            for (size_t i = view->size - 1; i > index; i--) {
                curr = static_cast<Node*>(curr->prev);
            }

            // overwrite value (unrecoverable)
            old_value = curr->value;
            Py_INCREF(item);
            curr->value = item;
            Py_DECREF(old_value);
            return;
        }
    }

    // forward traversal
    curr = view->head;
    for (size_t i = 0; i < index; i++) {
        curr = static_cast<Node*>(curr->next);
    }

    // overwrite value (unrecoverable)
    old_value = curr->value;
    Py_INCREF(item);
    curr->value = item;
    Py_DECREF(old_value);
}


/* Set a slice within a linked list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
void set_slice(
    ViewType<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
) {
    size_t abs_step = (size_t)abs(step);

    // unpack iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(items, "can only assign an iterable");
    if (sequence == nullptr) {  // TypeError(): items do not support sequence protocol
        return;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence);

    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds = normalize_slice(view, start, stop, step);
    if (PyErr_Occurred()) {  // invalid slice
        if (step == 1) {  // Python allows inserting slices in this case only
            _replace_slice(
                view,
                (size_t)start,  // start inserting nodes at start index
                (size_t)stop,   // determines whether to traverse from head/tail
                abs_step,
                0,              // slice length
                seq_length,     // insert all items from sequence
                sequence,
                (start > stop)  // reverse if traversing from tail
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

    // replace nodes in slice and rewind if an error occurs
    _replace_slice(
        view,
        bounds.first,
        bounds.second,
        abs_step,
        slice_length,
        seq_length,
        sequence,
        (step < 0) ^ (bounds.first > bounds.second)
    );

    // release sequence
    Py_DECREF(sequence);
}


/* Overwrite a slice within a linked list. */
template <typename NodeType>
void set_slice(
    ListView<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
) {
    size_t abs_step = (size_t)abs(step);

    // unpack iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(items, "can only assign an iterable");
    if (sequence == nullptr) {  // TypeError(): items do not support sequence protocol
        return;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence);

    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds = normalize_slice(view, start, stop, step);
    if (PyErr_Occurred()) {  // invalid slice
        if (step == 1) {  // Python allows inserting slices in this case only
            _replace_slice(
                view,
                (size_t)start,  // start inserting nodes at start index
                (size_t)stop,   // determines whether to traverse from head/tail
                abs_step,
                0,              // slice length
                seq_length,     // insert all items from sequence
                sequence,
                (start > stop)  // reverse if traversing from tail
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
    bool length_match = (slice_length == seq_length);

    // NOTE: we can only overwrite the existing values if the following
    // conditions are met:
    //  1)  The view is a ListView (no hashing/uniqueness).
    //  2)  The slice length matches the sequence length (no insertions/removals).
    //  3)  The view is not specialized (no type checking)

    // If all of these conditions are met, then we are guaranteed not to
    // encounter any errors during the assignment, and can thus overwrite the
    // existing values directly.  This is much faster than allocating new nodes
    // and replacing the existing ones, but it is only safe in this narrow
    // context.

    if (length_match && view->get_specialization() == nullptr) {  // overwrite values
        _overwrite_slice(
            view,
            bounds.first,
            bounds.second,
            seq_length,
            abs_step,
            sequence,
            (step < 0)
        );
    } else if (length_match || step == 1) {  // replace nodes
        _replace_slice(
            view,
            bounds.first,
            bounds.second,
            abs_step,
            slice_length,
            seq_length,
            sequence,
            (step < 0) ^ (bounds.first > bounds.second)
        );
    } else {  // raise error
        PyErr_Format(
            PyExc_ValueError,
            "attempt to assign sequence of size %zd to extended slice of size %zd",
            seq_length,
            slice_length
        );
        return;  // propagate
    }

    // release sequence
    Py_DECREF(sequence);
}


/////////////////////////
////    OVERWRITE    ////
/////////////////////////


/* Overwrite a slice in a linked list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
void _overwrite_slice(
    ViewType<NodeType>* view,
    size_t begin,
    size_t end,
    size_t abs_step,
    Py_ssize_t slice_length,
    PyObject* sequence,
    bool reverse
) {
    using Node = typename ViewType<NodeType>::Node;

    // overwrite existing nodes with new ones and rewind if an error occurs
    if constexpr (is_doubly_linked<Node>::value) {
        if (begin > end) {  // backward traversal
            _overwrite_slice_backward(
                view,
                begin,
                abs_step,
                slice_length,
                sequence,
                reverse
            );
            return;
        }
    }

    // forward traversal
    _overwrite_slice_forward(
        view,
        begin,
        abs_step,
        slice_length,
        sequence,
        reverse
    );
}


/* Assign a slice from left to right, directly overwriting exisiting values. */
template <template <typename> class ViewType, typename NodeType>
void _overwrite_slice_forward(
    ViewType<NodeType>* view,
    size_t begin,
    size_t abs_step,
    Py_ssize_t seq_length,
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
        curr = static_cast<Node*>(curr->next);
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
        Py_DECREF(old);

        // advance node according to step size
        if (seq_idx < last_idx) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                curr = static_cast<Node*>(curr->next);
            }
        }
    }
}


/* Assign a slice from right to left, directly overwriting exisiting values. */
template <template <typename> class ViewType, typename NodeType>
void _overwrite_slice_backward(
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

    // get first node in slice by iterating from tail
    Node* curr = view->tail;
    for (size_t i = view->size - 1; i > begin; i--) {
        curr = static_cast<Node*>(curr->prev);
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
        Py_DECREF(old);

        // advance node according to step size
        if (seq_idx < last_idx) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                curr = static_cast<Node*>(curr->prev);
            }
        }
    }
}


///////////////////////
////    REPLACE    ////
///////////////////////


/* Replace a slice in a linked list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
void _replace_slice(
    ViewType<NodeType>* view,
    size_t begin,
    size_t end,
    size_t abs_step,
    Py_ssize_t slice_length,
    Py_ssize_t seq_length,
    PyObject* sequence,
    bool reverse
) {
    using Node = typename ViewType<NodeType>::Node;

    // replace existing nodes with new ones and rewind if an error occurs
    if constexpr (is_doubly_linked<Node>::value) {
        if (begin > end) {  // backward traversal
            _replace_slice_backward(
                view,
                begin,
                abs_step,
                slice_length,
                seq_length,
                sequence,
                reverse
            );
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
        reverse
    );
}


/* Assign a slice from left to right, replacing the existing nodes with new ones. */
template <template <typename> class ViewType, typename NodeType>
void _replace_slice_forward(
    ViewType<NodeType>* view,
    size_t begin,
    size_t abs_step,
    Py_ssize_t slice_length,
    Py_ssize_t seq_length,
    PyObject* sequence,
    bool reverse
) {
    using Node = typename ViewType<NodeType>::Node;

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
    std::queue<Node*> removed;

    // loop 1: unlink nodes in slice
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    Py_ssize_t last_iter = slice_length - 1;
    for (Py_ssize_t i = 0; i < slice_length; i ++) {
        // unlink node
        next = static_cast<Node*>(curr->next);
        view->unlink(prev, curr, next);
        removed.push(curr);  // push to recovery queue

        // advance node according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = static_cast<Node*>(curr->next);
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
        if (prev == nullptr) {  // slice originates from head of list
            next = view->head;  // points to new head (head->next)
        } else {
            next = static_cast<Node*>(prev->next);
        }

        // allocate a new node and link it to the list
        curr = _insert_node(view, item, prev, next);
        if (curr == nullptr) {
            _undo_set_slice_forward(view, source, seq_idx, abs_step, removed);
            return;
        }

        // advance node according to step size
        if (seq_idx < last_idx) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                prev = curr;
                curr = static_cast<Node*>(curr->next);
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
    size_t abs_step,
    Py_ssize_t slice_length,
    Py_ssize_t seq_length,
    PyObject* sequence,
    bool reverse
) {
    using Node = typename ViewType<NodeType>::Node;

    // get first node in slice by iterating from tail
    Node* prev;
    Node* curr = view->tail;
    Node* next = nullptr;
    for (size_t i = 0; i < begin; i++) {
        next = curr;
        curr = static_cast<Node*>(curr->prev);
    }

    // remember beginning of slice so we can reset later
    Node* source = next;
    std::queue<Node*> removed;

    // loop 1: unlink nodes in slice
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    Py_ssize_t last_iter = slice_length - 1;
    for (Py_ssize_t i = 0; i < slice_length; i++) {
        // unlink node
        prev = static_cast<Node*>(curr->prev);
        view->unlink(prev, curr, next);
        removed.push(curr);  // push to recovery queue

        // advance node according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = static_cast<Node*>(curr->prev);
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
                seq_idx,
                abs_step,
                removed
            );
            return;
        }

        // advance node according to step size
        if (seq_idx < last_idx) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                next = curr;
                curr = static_cast<Node*>(curr->prev);
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


////////////////////////
////    RECOVERY    ////
////////////////////////


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
    if (prev == nullptr) {  // slice originates from head of list
        curr = view->head;  // points to new head (head->next)
    } else {
        curr = static_cast<Node*>(prev->next);
    }

    // loop 3: unlink and deallocate nodes that have already been added to slice
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    Py_ssize_t last_iter = n_staged - 1;
    for (Py_ssize_t i = 0; i < n_staged; i++) {
        // unlink node
        next = static_cast<Node*>(curr->next);
        view->unlink(prev, curr, next);
        view->recycle(curr);  // free node

        // advance node according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = static_cast<Node*>(curr->next);
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
        if (prev == nullptr) {  // slice originates from head of list
            next = view->head;  // points to new head (head->next)
        } else {
            next = static_cast<Node*>(prev->next);
        }
        view->link(prev, curr, next);  // NOTE: cannot cause error

        // advance node according to step size
        if (i < last_queued) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                prev = curr;
                curr = static_cast<Node*>(curr->next);
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
    if (next == nullptr) {  // slice originates from tail of list
        curr = view->tail;  // points to new tail (tail->prev)
    } else {
        curr = static_cast<Node*>(next->prev);
    }

    // loop 3: unlink and deallocate nodes that have already been added to slice
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    Py_ssize_t last_iter = n_staged - 1;
    for (Py_ssize_t i = 0; i < n_staged; i++) {
        // unlink node
        prev = static_cast<Node*>(curr->prev);
        view->unlink(prev, curr, next);
        view->recycle(curr);  // free node

        // advance node according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                next = curr;
                curr = static_cast<Node*>(curr->prev);
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
        if (next == nullptr) {  // slice originates from tail of list
            prev = view->tail;  // points to new tail (tail->prev)
        } else {
            prev = static_cast<Node*>(next->prev);
        }
        view->link(prev, curr, next);  // NOTE: cannot cause error

        // advance node according to step size
        if (i < last_queued) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                next = curr;
                curr = static_cast<Node*>(curr->prev);
            }
        }
    }
}


////////////////////////
////    WRAPPERS    ////
////////////////////////


// NOTE: Cython doesn't play well with nested templates, so we need to
// explicitly instantiate specializations for each combination of node/view
// type.  This is a bit of a pain, put it's the only way to get Cython to
// properly recognize the functions.

// Maybe in a future release we won't have to do this:


template void set_index(ListView<SingleNode>* view, size_t index, PyObject* item);
template void set_index(SetView<SingleNode>* view, size_t index, PyObject* item);
template void set_index(DictView<SingleNode>* view, size_t index, PyObject* item);
template void set_index(ListView<DoubleNode>* view, size_t index, PyObject* item);
template void set_index(SetView<DoubleNode>* view, size_t index, PyObject* item);
template void set_index(DictView<DoubleNode>* view, size_t index, PyObject* item);
template void set_slice(
    ListView<SingleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
);
template void set_slice(
    SetView<SingleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
);
template void set_slice(
    DictView<SingleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
);
template void set_slice(
    ListView<DoubleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
);
template void set_slice(
    SetView<DoubleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
);
template void set_slice(
    DictView<DoubleNode>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
);


#endif // SET_SLICE_H include guard
