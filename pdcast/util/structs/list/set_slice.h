
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


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Set a slice within a list. */
template <typename NodeType>
void set_slice_single(
    ListView<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
) {
    size_t abs_step = (size_t)abs(step);

    // unpack iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(items, "can only assign an iterable");
    if (sequence == NULL) {  // TypeError(): items do not support sequence protocol
        return;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence);

    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds;
    try {
        bounds = _get_slice_direction_single(start, stop, step, view->size);
    } catch (const std::invalid_argument&) {  // invalid slice
        if (step == 1) {  // Python allows inserting slices in this case only
            _replace_slice_forward(
                view,
                view->head,
                (size_t)start,  // start inserting nodes at start index
                0,              // nothing to replace
                seq_length,     // insert all items
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
                view->head,
                bounds.first,
                slice_length,  // replace the nodes that are currently there
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
        view->head,
        bounds.first,
        seq_length,
        abs_step,
        sequence,
        (step < 0)
    );

    // release sequence
    Py_DECREF(sequence);
}


/* Set a slice within a set. */
template <typename NodeType>
void set_slice_single(
    SetView<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
) {
    size_t abs_step = (size_t)abs(step);

    // unpack iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(items, "can only assign an iterable");
    if (sequence == NULL) {  // TypeError(): items do not support sequence protocol
        return;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence);

    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds;
    try {
        bounds = _get_slice_direction_single(start, stop, step, view->size);
    } catch (const std::invalid_argument&) {  // invalid slice
        if (step == 1) {  // Python allows inserting slices in this case only
            _replace_slice_forward(
                view,
                view->head,
                (size_t)start,  // start inserting nodes at start index
                0,              // nothing to replace
                seq_length,     // insert all items
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
    if (slice_length != seq_length && step != 1) {
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
        view->head,
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


/* Set a slice within a dictionary. */
template <typename NodeType>
void set_slice_single(
    DictView<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
) {
    size_t abs_step = (size_t)abs(step);

    // unpack iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(items, "can only assign an iterable");
    if (sequence == NULL) {  // TypeError(): items do not support sequence protocol
        return;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence);

    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds;
    try {
        bounds = _get_slice_direction_single(start, stop, step, view->size);
    } catch (const std::invalid_argument&) {  // invalid slice
        if (step == 1) {  // Python allows inserting slices in this case only
            _replace_slice_forward(
                view,
                view->head,
                (size_t)start,  // start inserting nodes at start index
                0,              // nothing to replace
                seq_length,     // insert all items
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
    if (slice_length != seq_length && step != 1) {
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
        view->head,
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


/* Set a slice within a list. */
template <typename NodeType>
void set_slice_double(
    ListView<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
) {
    size_t abs_step = (size_t)abs(step);

    // unpack iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(items, "can only assign an iterable");
    if (sequence == NULL) {  // TypeError(): items do not support sequence protocol
        return;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence);

    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds;
    try {
        bounds = _get_slice_direction_double(start, stop, step, view->size);
    } catch (const std::invalid_argument&) {  // invalid slice
        if (step == 1) {  // Python allows inserting slices in this case only
            if ((size_t)start <= view->size / 2) {  // closer to head
                _replace_slice_forward(
                    view,
                    view->head,
                    (size_t)start,  // start inserting nodes at start index
                    0,              // nothing to replace
                    seq_length,     // insert all items
                    abs_step,
                    sequence,
                    (step < 0)
                );
            } else {  // closer to tail
                _replace_slice_backward(
                    view,
                    view->tail,
                    (size_t)start,  // start inserting nodes at start index
                    0,              // nothing to replace
                    seq_length,     // insert all items
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
                    bounds.first,   // start inserting nodes at start index
                    slice_length,   // replace the nodes that are currently there
                    seq_length,     // continue inserting the remaining items
                    abs_step,
                    sequence,
                    (step < 0)
                );
            } else {  // closer to tail
                _replace_slice_backward(
                    view,
                    view->tail,
                    bounds.first,   // start inserting nodes at start index
                    slice_length,   // replace the nodes that are currently there
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
            view->head,
            bounds.first,
            seq_length,
            abs_step,
            sequence,
            (step < 0)
        );
    } else {  // backward traversal
        _overwrite_slice_backward(
            view,
            view->head,
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


/* Set a slice within a set. */
template <typename NodeType>
void set_slice_double(
    SetView<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
) {
    size_t abs_step = (size_t)abs(step);

    // unpack iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(items, "can only assign an iterable");
    if (sequence == NULL) {  // TypeError(): items do not support sequence protocol
        return;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence);

    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds;
    try {
        bounds = _get_slice_direction_single(start, stop, step, view->size);
    } catch (const std::invalid_argument&) {  // invalid slice
        if (step == 1) {  // Python allows inserting slices in this case only
            if ((size_t)start <= view->size / 2) {  // closer to head
                _replace_slice_forward(
                    view,
                    view->head,
                    (size_t)start,  // start inserting nodes at start index
                    0,              // nothing to replace
                    seq_length,     // insert all items
                    abs_step,
                    sequence,
                    (step < 0)
                );
            } else {  // closer to tail
                _replace_slice_backward(
                    view,
                    view->tail,
                    (size_t)start,  // start inserting nodes at start index
                    0,              // nothing to replace
                    seq_length,     // insert all items
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
    if (slice_length != seq_length && step != 1) {
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
            view->head,
            bounds.first,   // start inserting nodes at start index
            slice_length,   // replace the nodes that are currently there
            seq_length,     // continue inserting the remaining items
            abs_step,
            sequence,
            (step < 0)
        );
    } else {  // backward traversal
        _replace_slice_backward(
            view,
            view->tail,
            bounds.first,   // start inserting nodes at start index
            slice_length,   // replace the nodes that are currently there
            seq_length,     // continue inserting the remaining items
            abs_step,
            sequence,
            (step > 0)
        );
    }

    // release sequence
    Py_DECREF(sequence);
}


/* Set a slice within a dictionary. */
template <typename NodeType>
void set_slice_double(
    DictView<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step,
    PyObject* items
) {
    size_t abs_step = (size_t)abs(step);

    // unpack iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(items, "can only assign an iterable");
    if (sequence == NULL) {  // TypeError(): items do not support sequence protocol
        return;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence);

    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds;
    try {
        bounds = _get_slice_direction_single(start, stop, step, view->size);
    } catch (const std::invalid_argument&) {  // invalid slice
        if (step == 1) {  // Python allows inserting slices in this case only
            if ((size_t)start <= view->size / 2) {  // closer to head
                _replace_slice_forward(
                    view,
                    view->head,
                    (size_t)start,  // start inserting nodes at start index
                    0,              // nothing to replace
                    seq_length,     // insert all items
                    abs_step,
                    sequence,
                    (step < 0)
                );
            } else {  // closer to tail
                _replace_slice_backward(
                    view,
                    view->tail,
                    (size_t)start,  // start inserting nodes at start index
                    0,              // nothing to replace
                    seq_length,     // insert all items
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
    if (slice_length != seq_length && step != 1) {
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
            view->head,
            bounds.first,   // start inserting nodes at start index
            slice_length,   // replace the nodes that are currently there
            seq_length,     // continue inserting the remaining items
            abs_step,
            sequence,
            (step < 0)
        );
    } else {  // backward traversal
        _replace_slice_backward(
            view,
            view->tail,
            bounds.first,   // start inserting nodes at start index
            slice_length,   // replace the nodes that are currently there
            seq_length,     // continue inserting the remaining items
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
template <template <typename> class ViewType, typename T, typename U>
void _overwrite_slice_forward(
    ViewType<T>* view,
    U* head,
    size_t begin,
    Py_ssize_t seq_length,
    size_t abs_step,
    PyObject* sequence,
    bool reverse
) {
    // NOTE: this method should only be used in ListViews where the size of
    // iterable is the same as the length of the slice.  This guarantees that
    // we won't encounter an errors partway through the process.

    // get first node in slice by iterating from head
    U* curr = head;
    for (size_t i = 0; i < begin; i++) {
        curr = (U*)curr->next;
    }

    // iterate through sequence
    PyObject* item;
    Py_ssize_t last_idx = seq_length - 1;
    for (Py_ssize_t seq_idx = 0; seq_idx < seq_length; seq_idx++) {
        // get item from sequence
        if (reverse) {  // iterate from right to left
            item = PySequence_Fast_GET_ITEM(sequence, last_idx - seq_idx);
        } else {  // iterate from left to right
            item = PySequence_Fast_GET_ITEM(sequence, seq_idx);
        }

        // overwrite node's current value
        Py_DECREF(curr->value);
        Py_INCREF(item);  // Fast_GET_ITEM() returns a borrowed reference
        curr->value = item;

        // advance node according to step size
        if (seq_idx < last_idx) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                curr = (U*)curr->next;
            }
        }
    }
}


/* Assign a slice from right to left, directly overwriting exisiting values. */
template <template <typename> class ViewType, typename T, typename U>
void _overwrite_slice_backward(
    ViewType<T>* view,
    U* tail,
    size_t begin,
    size_t seq_length,
    size_t abs_step,
    PyObject* sequence,
    bool reverse
) {
    // NOTE: this method should only be used in ListViews where the size of
    // iterable is the same as the length of the slice.  This guarantees that
    // we won't encounter an errors partway through the process.

    // get first node in slice by iterating from tail
    U* curr = tail;
    for (size_t i = view->size - 1; i > begin; i--) {
        curr = (U*)curr->prev;
    }

    // iterate through sequence
    PyObject* item;
    Py_ssize_t last_idx = seq_length - 1;
    for (Py_ssize_t seq_idx = 0; seq_idx < seq_length; seq_idx++) {
        // get item from sequence
        if (reverse) {  // iterate from right to left
            item = PySequence_Fast_GET_ITEM(sequence, last_idx - seq_idx);
        } else {  // iterate from left to right
            item = PySequence_Fast_GET_ITEM(sequence, seq_idx);
        }

        // overwrite node's current value
        Py_DECREF(curr->value);
        Py_INCREF(item);  // Fast_GET_ITEM() returns a borrowed reference
        curr->value = item;

        // advance node according to step size
        if (seq_idx < last_idx) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                curr = (U*)curr->prev;
            }
        }
    }
}


/* Assign a slice from left to right, replacing the existing nodes with new ones. */
template <template <typename> class ViewType, typename T, typename U>
void _replace_slice_forward(
    ViewType<T>* view,
    U* head,
    size_t begin,
    Py_ssize_t slice_length,
    Py_ssize_t seq_length,
    size_t abs_step,
    PyObject* sequence,
    bool reverse
) {
    // get first node in slice by iterating from head
    U* prev = NULL;
    U* curr = head;
    for (size_t i = 0; i < begin; i++) {
        prev = curr;
        curr = (U*)curr->next;
    }

    // remember beginning of slice so we can reset later
    U* source = prev;
    U* first = curr;
    std::queue<U*> removed = new std::queue<U*>();

    // loop 1: unlink nodes in slice
    U* next;
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    Py_ssize_t last_iter = slice_length - 1;
    for (Py_ssize_t i = 0; i < slice_length; i ++) {
        // unlink node
        next = (U*)curr->next;
        view->unlink(prev, curr, next);  // also removes from hash table, if applicable
        removed.push(curr);  // push to recovery queue

        // advance node according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = (U*)curr->next;
            }
        }
    }

    // loop 2: insert new nodes from sequence
    PyObject* item;
    prev = source;  // reset to beginning of slice
    Py_ssize_t last_idx = seq_length - 1;
    for (Py_ssize_t seq_idx = 0; seq_idx < seq_length; seq_idx++) {
        // get item from sequence
        if (reverse) {  // reverse sequence as we add nodes
            item = PySequence_Fast_GET_ITEM(sequence, last_idx - seq_idx);
        } else {
            item = PySequence_Fast_GET_ITEM(sequence, seq_idx);
        }

        // get next node in slice
        if (prev == NULL) {  // slice includes original head of list
            next = view->head;  // points to new head (head->next)
        } else {
            next = (U*)prev->next;
        }

        // allocate a new node and link it to the list
        curr == _insert_node(view, item, prev, next);
        if (curr == NULL) {
            _undo_set_slice_forward(  // replace original nodes
                view,
                removed,
                seq_idx,
                source,
                abs_step
            );
            return;
        }

        // advance node according to step size
        for (size_t j = 0; j < abs_step; j++) {
            prev = curr;
            curr = (U*)curr->next;
            if (curr == NULL) {  // exhaust the sequence if we hit the end of the list
                break;
            }
        }
    }

    // loop 3: deallocate removed nodes
    for (size_t i = 0; i < removed.size(); i++) {
        view->deallocate(removed.pop());
    }
}


/* Assign a slice from right to left, replacing the existing nodes with new ones. */
template <template <typename> class ViewType, typename T, typename U>
void _replace_slice_backward(
    ViewType<T>* view,
    U* tail,
    size_t begin,
    Py_ssize_t slice_length,
    Py_ssize_t seq_length,
    size_t abs_step,
    PyObject* sequence,
    bool reverse
) {
    // get first node in slice by iterating from tail
    U* next = NULL;
    U* curr = tail;
    for (size_t i = 0; i < begin; i++) {
        next = curr;
        curr = (U*)curr->prev;
    }

    // remember beginning of slice so we can reset later
    U* source = next;
    U* first = curr;
    std::queue<U*> removed = new std::queue<U*>();

    // loop 1: unlink nodes in slice
    U* prev;
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    Py_ssize_t last_iter = slice_length - 1;
    for (Py_ssize_t i = 0; i < slice_length; i++) {
        // unlink node
        prev = (U*)curr->prev;
        view->unlink(prev, curr, next);  // also removes from hash table, if applicable
        removed.push(curr);  // push to recovery queue

        // advance node according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = (U*)curr->prev;
            }
        }
    }

    // loop 2: insert new nodes from sequence
    PyObject* item;
    next = source;  // reset to beginning of slice
    Py_ssize_t last_idx = seq_length - 1;
    for (Py_ssize_t seq_idx = 0; seq_idx < seq_length; seq_idx++) {
        // get item from sequence
        if (reverse) {  // reverse sequence as we add nodes
            item = PySequence_Fast_GET_ITEM(sequence, last_idx - seq_idx);
        } else {
            item = PySequence_Fast_GET_ITEM(sequence, seq_idx);
        }

        // get previous node in slice
        if (next == NULL) {  // slice includes original tail of list
            prev = view->tail;  // points to new tail (tail->prev)
        } else {
            prev = (U*)next->prev;
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
                curr = (U*)curr->prev;
            }
        }
    }

    // loop 3: deallocate removed nodes
    for (size_t i = 0; i < removed.size(); i++) {
        view->deallocate(removed.pop());
    }
}


/* Attempt to allocate a new node and insert it into the slice. */
template <template <typename> class ViewType, typename T, typename U>
inline U* _insert_node(ViewType<T>* view, PyObject* item, U* prev, U* next) {
    U* curr;

    // allocate a new node
    try {
        curr = view->allocate(item);
    } catch (const std::bad_alloc&) {  // MemoryError()
        PyErr_NoMemory();
        return NULL;
    }
    if (curr == NULL) {  // TypeError() during hash() / tuple unpacking
        return NULL;
    }

    // link to list
    try {
        view->link(prev, curr, next);
    } catch (const std::bad_alloc&) {  // MemoryError() during resize()
        view->deallocate(curr);  // clean up orphaned node
        PyErr_NoMemory();
        return NULL;
    }
    if (PyErr_Occurred()) {  // item violates hash invariant
        view->deallocate(curr);  // clean up orphaned node
        return NULL;
    }

    return curr;
}


/* Undo a slice assignment. */
template <template <typename> class ViewType, typename T, typename U>
void _undo_set_slice_forward(
    ViewType<T>* view,
    std::queue<U*> removed,
    Py_ssize_t n_staged,
    U* source,
    size_t abs_step
) {
    U* prev = source;
    U* curr;
    if (prev == NULL) {  // slice originates from head of list
        curr = view->head;  // points to new head (head->next)
    } else {
        curr = (U*)prev->next;
    }

    // loop 3: unlink and deallocate nodes that have already been added to slice
    U* next;
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    Py_ssize_t last_iter = n_staged - 1;
    for (Py_ssize_t i = 0; i < n_staged; i++) {
        // unlink node
        next = (U*)curr->next;
        view->unlink(prev, curr, next);  // also removes from hash table, if applicable
        view->deallocate(curr);

        // advance node according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = (U*)curr->next;
            }
        }
    }

    // loop 4: reinsert original nodes
    prev = source;
    size_t last_queued = removed.size() - 1;
    for (size_t i = 0; i < removed.size(); i++) {
        curr = removed.pop();  // get next node from queue

        // link to list
        if (prev == NULL) {  // slice originates from head of list
            next = view->head;  // points to new head (head->next)
        } else {
            next = (U*)prev->next;
        }
        view->link(prev, curr, next);  // NOTE: cannot cause error

        // advance node according to step size
        if (i < last_queued) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                prev = curr;
                curr = (U*)curr->next;
            }
        }
    }
}


/* Undo a slice assignment. */
template <template <typename> class ViewType, typename T, typename U>
void _undo_set_slice_backward(
    ViewType<T>* view,
    std::queue<U*> removed,
    Py_ssize_t n_staged,
    U* source,
    size_t abs_step
) {
    U* next = source;
    U* curr;
    if (next == NULL) {  // slice originates from tail of list
        curr = view->tail;  // points to new tail (tail->prev)
    } else {
        curr = (U*)next->prev;
    }

    // loop 3: unlink and deallocate nodes that have already been added to slice
    U* prev;
    size_t small_step = abs_step - 1;  // we jump by 1 whenever we remove a node
    Py_ssize_t last_iter = n_staged - 1;
    for (Py_ssize_t i = 0; i < n_staged; i++) {
        // unlink node
        prev = (U*)curr->prev;
        view->unlink(prev, curr, next);  // also removes from hash table, if applicable
        view->deallocate(curr);

        // advance node according to step size
        if (i < last_iter) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                next = curr;
                curr = (U*)curr->prev;
            }
        }
    }

    // loop 4: reinsert original nodes
    next = source;
    size_t last_queued = removed.size() - 1;
    for (size_t i = 0; i < removed.size(); i++) {
        curr = removed.pop();  // get next node from queue

        // link to list
        if (next == NULL) {  // slice originates from tail of list
            prev = view->tail;  // points to new tail (tail->prev)
        } else {
            prev = (U*)next->prev;
        }
        view->link(prev, curr, next);  // NOTE: cannot cause error

        // advance node according to step size
        if (i < last_queued) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                next = curr;
                curr = (U*)curr->prev;
            }
        }
    }
}


#endif // SET_SLICE_H include guard
