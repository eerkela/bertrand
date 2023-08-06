
// include guard prevents multiple inclusion
#ifndef SLICE_H
#define SLICE_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <utility>  // for std::pair
#include <view.h>  // for views


/////////////////////////
////    GET SLICE    ////
/////////////////////////


/* Extract a slice from a singly-linked list. */
template <template <typename> class ViewType, typename NodeType>
inline ViewType<NodeType> get_slice_single(
    ViewType<NodeType>* view,
    size_t start,
    size_t stop,
    ssize_t step
) {
    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds = _get_slice_direction_single(
        start,
        stop,
        step,
        view->size
    );

    // iterate forward from head
    return _get_slice_forward(
        view,
        view->head,
        bounds.first,
        bounds.second,
        (size_t)abs(step),
        (step < 0)
    );
}


/* Extract a slice from a doubly-linked list. */
template <template <typename> class ViewType, typename NodeType>
inline ViewType<NodeType> get_slice_double(
    ViewType<NodeType>* view,
    size_t start,
    size_t stop,
    ssize_t step
) {
    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds = _get_slice_direction_double(
        start,
        stop,
        step,
        view->size
    );

    // iterate from nearest end of list
    if (bounds.first <= bounds.second) {  // forward traversal
        return _get_slice_forward(
            view,
            view->head,
            bounds.first,
            bounds.second,
            (size_t)abs(step),
            (step < 0)
        );
    }

    // backward traversal
    return _get_slice_backward(
        view,
        view->tail,
        bounds.first,
        bounds.second,
        (size_t)abs(step),
        (step > 0)
    );
}


/* Extract a slice from left to right. */
template <template <typename> class ViewType, typename T, typename U>
ViewType<T>* _get_slice_forward(
    ViewType<T>* view,
    U* head,
    size_t begin,
    size_t end,
    size_t abs_step,
    bool reverse
) {
    // create a new view to hold the slice
    ViewType<T>* slice = new ViewType<T>();
    if (slice == NULL) {  // MemoryError()
        PyErr_NoMemory();
        return NULL;
    }

    // get first node in slice by iterating from head
    U* curr = head;
    for (size_t i = 0; i < begin; i++) {
        curr = (U*)curr->next;
    }

    // copy nodes from original view
    U* copy;
    for (size_t i = begin, i <= end; i += abs_step) {
        copy = slice->copy(curr);
        if (copy == NULL) {  // MemoryError()
            PyErr_NoMemory();
            delete slice;
            return NULL;
        }

        // link to slice
        if (reverse) {  // reverse slice as we add nodes
            slice->link(NULL, copy, slice->head);
        } else {
            slice->link(slice->tail, copy, NULL);
        }
        if (PyErr_Occurred()) {  // error during link()
            delete slice;
            return NULL;
        }

        // advance node according to step size
        if (i != end) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                curr = (U*)curr->next;
            }
        }
    }

    // return new view
    return slice;
}


/* Extract a slice from right to left. */
template <template <typename> class ViewType, typename T, typename U>
ViewType<T>* _get_slice_backward(
    ViewType<T>* view,
    U* tail,
    size_t begin,
    size_t end,
    size_t abs_step,
    bool reverse
) {
    // create a new view to hold the slice
    ViewType<T>* slice = new ViewType<T>();
    if (slice == NULL) {  // MemoryError()
        PyErr_NoMemory();
        return NULL;
    }

    // get first node in slice by iterating from tail
    U* curr = tail;
    for (size_t i = view->size - 1; i > begin; i--) {
        curr = (U*)curr->prev;
    }

    // copy nodes from original view
    U* copy;
    for (size_t i = begin, i >= end; i -= abs_step) {
        copy = slice->copy(curr);
        if (copy == NULL) {  // MemoryError()
            PyErr_NoMemory();
            delete slice;
            return NULL;
        }

        // link to slice
        if (reverse) {  // reverse slice as we add nodes
            slice->link(slice->tail, copy, NULL);
        } else {  // reverse
            slice->link(NULL, copy, slice->head);
        }
        if (PyErr_Occurred()) {  // error during link()
            delete slice;
            return NULL;
        }

        // advance node according to step size
        if (i != end) {  // don't jump on final iteration
            for (size_t j = 0; j < abs_step; j++) {
                curr = (U*)curr->prev;
            }
        }
    }

    // return new view
    return slice;
}


/////////////////////////
////    SET SLICE    ////
/////////////////////////


/* Set a slice within a list. */
template <typename NodeType>
int set_slice_single(
    ListView<NodeType>* view,
    size_t start,
    size_t stop,
    ssize_t step,
    PyObject* items
) {
    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds = _get_slice_direction_single(
        start,
        stop,
        step,
        view->size
    );
    size_t abs_step = (size_t)abs(step);
    Py_ssize_t slice_length = (Py_ssize_t)bounds.second - (Py_ssize_t)bounds.first;
    slice_length = abs(slice_length) / (Py_ssize_t)abs_step;

    // Convert iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(iterable);
    if (sequence == NULL) {  // TypeError()
        return NULL;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence) - 1;

    // NOTE: Since ListViews don't employ a hash table, we are free to
    // directly overwrite the values in the slice.  This avoids the need to
    // allocate new nodes and link them to the list manually.
    if (seq_length == slice_length) {  // overwrite existing nodes
        _overwrite_slice_forward(
            view,
            view->head,
            bounds.first,
            bounds.second,
            abs_step,
            sequence,
            (step < 0)
        );
    } else {  // replace existing nodes with new ones
        _replace_slice_forward(
            view,
            view->head,
            bounds.first,
            bounds.second,
            abs_step,
            sequence,
            (step < 0)
        );
    }

    // release sequence
    Py_DECREF(sequence);
}


/* Set a slice within a set. */
template <typename NodeType>
int set_slice_single(
    SetView<NodeType>* view,
    size_t start,
    size_t stop,
    ssize_t step,
    PyObject* items
) {
    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds = _get_slice_direction_single(
        start,
        stop,
        step,
        view->size
    );
    size_t abs_step = (size_t)abs(step);
    Py_ssize_t slice_length = (Py_ssize_t)bounds.second - (Py_ssize_t)bounds.first;
    slice_length = abs(slice_length) / (Py_ssize_t)abs_step;

    // Convert iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(iterable);
    if (sequence == NULL) {  // TypeError()
        return NULL;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence) - 1;

    // NOTE: Since ListViews don't employ a hash table, we are free to
    // directly overwrite the values in the slice.  This avoids the need to
    // allocate new nodes and link them to the list manually.
    _replace_slice_forward(
        view,
        view->head,
        bounds.first,
        bounds.second,
        abs_step,
        sequence,
        (step < 0)
    );

    // release sequence
    Py_DECREF(sequence);
}


/* Set a slice within a dictionary. */
template <typename NodeType>
int set_slice_single(
    DictView<NodeType>* view,
    size_t start,
    size_t stop,
    ssize_t step,
    PyObject* items
) {
    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds = _get_slice_direction_single(
        start,
        stop,
        step,
        view->size
    );
    size_t abs_step = (size_t)abs(step);
    Py_ssize_t slice_length = (Py_ssize_t)bounds.second - (Py_ssize_t)bounds.first;
    slice_length = abs(slice_length) / (Py_ssize_t)abs_step;

    // Convert iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(iterable);
    if (sequence == NULL) {  // TypeError()
        return NULL;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence) - 1;

    // NOTE: Since ListViews don't employ a hash table, we are free to
    // directly overwrite the values in the slice.  This avoids the need to
    // allocate new nodes and link them to the list manually.
    _replace_slice_forward(
        view,
        view->head,
        bounds.first,
        bounds.second,
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
    size_t start,
    size_t stop,
    ssize_t step,
    PyObject* items
) {
    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds = _get_slice_direction_double(
        start,
        stop,
        step,
        view->size
    );
    size_t abs_step = (size_t)abs(step);
    Py_ssize_t slice_length = (Py_ssize_t)bounds.second - (Py_ssize_t)bounds.first;
    slice_length = abs(slice_length) / (Py_ssize_t)abs_step;

    // Convert iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(iterable);
    if (sequence == NULL) {  // TypeError()
        return NULL;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence) - 1;

    // NOTE: Since ListViews don't employ a hash table, we are free to
    // directly overwrite the values in the slice.  This avoids the need to
    // allocate new nodes and link them to the list manually.

    // iterate from nearest end of list
    if (bounds.first <= bounds.second) {  // forward traversal
        if (seq_length == slice_length) {  // overwrite existing nodes
            _overwrite_slice_forward(
                view,
                view->head,
                bounds.first,
                bounds.second,
                abs_step,
                sequence,
                (step < 0)
            );
        } else {  // replace existing nodes with new ones
            _replace_slice_forward(
                view,
                view->head,
                bounds.first,
                bounds.second,
                abs_step,
                sequence,
                (step < 0)
            );
        }
    } else {  // backward traversal
        if (seq_length == slice_length) {  // overwrite existing nodes
            _overwrite_slice_backward(
                view,
                view->head,
                bounds.first,
                bounds.second,
                abs_step,
                sequence,
                (step < 0)
            );
        } else {  // replace existing nodes with new ones
            _replace_slice_backward(
                view,
                view->head,
                bounds.first,
                bounds.second,
                abs_step,
                sequence,
                (step < 0)
            );
        }
    }

    // release sequence
    Py_DECREF(sequence);
}


/* Set a slice within a set. */
template <typename NodeType>
void set_slice_double(
    SetView<NodeType>* view,
    size_t start,
    size_t stop,
    ssize_t step,
    PyObject* items
) {
    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds = _get_slice_direction_double(
        start,
        stop,
        step,
        view->size
    );
    size_t abs_step = (size_t)abs(step);
    Py_ssize_t slice_length = (Py_ssize_t)bounds.second - (Py_ssize_t)bounds.first;
    slice_length = abs(slice_length) / (Py_ssize_t)abs_step;

    // Convert iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(iterable);
    if (sequence == NULL) {  // TypeError()
        return NULL;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence) - 1;

    // NOTE: Since ListViews don't employ a hash table, we are free to
    // directly overwrite the values in the slice.  This avoids the need to
    // allocate new nodes and link them to the list manually.

    // iterate from nearest end of list
    if (bounds.first <= bounds.second) {  // forward traversal
        _replace_slice_forward(
            view,
            view->head,
            bounds.first,
            bounds.second,
            abs_step,
            sequence,
            (step < 0)
        );
    } else {  // backward traversal
        _replace_slice_backward(
            view,
            view->head,
            bounds.first,
            bounds.second,
            abs_step,
            sequence,
            (step < 0)
        );
    }

    // release sequence
    Py_DECREF(sequence);
}


/* Set a slice within a dictionary. */
template <typename NodeType>
void set_slice_double(
    DictView<NodeType>* view,
    size_t start,
    size_t stop,
    ssize_t step,
    PyObject* items
) {
    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds = _get_slice_direction_double(
        start,
        stop,
        step,
        view->size
    );
    size_t abs_step = (size_t)abs(step);
    Py_ssize_t slice_length = (Py_ssize_t)bounds.second - (Py_ssize_t)bounds.first;
    slice_length = abs(slice_length) / (Py_ssize_t)abs_step;

    // Convert iterable into a reversible sequence
    PyObject* sequence = PySequence_Fast(iterable);
    if (sequence == NULL) {  // TypeError()
        return NULL;
    }
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence) - 1;

    // NOTE: Since ListViews don't employ a hash table, we are free to
    // directly overwrite the values in the slice.  This avoids the need to
    // allocate new nodes and link them to the list manually.

    // iterate from nearest end of list
    if (bounds.first <= bounds.second) {  // forward traversal
        _replace_slice_forward(
            view,
            view->head,
            bounds.first,
            bounds.second,
            abs_step,
            sequence,
            (step < 0)
        );
    } else {  // backward traversal
        _replace_slice_backward(
            view,
            view->head,
            bounds.first,
            bounds.second,
            abs_step,
            sequence,
            (step < 0)
        );
    }

    // release sequence
    Py_DECREF(sequence);
}


/* Assign a slice from left to right, directly overwriting exisiting values. */
template <template <typename> class ViewType, typename T, typename U>
void _overwrite_slice_forward(
    ViewType<T>* view,
    U* head,
    size_t begin,
    size_t end,
    size_t abs_step,
    PyObject* sequence,
    bool reverse
) {
    // NOTE: this method should only be used in ListViews where the size of
    // iterable is the same as the length of the slice.  This allows us to
    // avoid NULL checks during slice assignment, and guarantees that we won't
    // encounter an error partway through the process.
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence) - 1;

    // get first node in slice by iterating from head
    U* curr = head;
    for (size_t i = 0; i < begin; i++) {
        curr = (U*)curr->next;
    }

    // iterate through sequence
    PyObject* item;
    for (Py_ssize_t seq_index = 0; seq_index <= seq_length; seq_index++) {
        // get item from sequence
        if (reverse) {  // iterate from right to left
            item = PySequence_Fast_GET_ITEM(sequence, seq_length - seq_idx);
        } else {  // iterate from left to right
            item = PySequence_Fast_GET_ITEM(sequence, seq_idx);
        }

        // overwrite node's current value
        Py_DECREF(curr->value);
        Py_INCREF(item);  // Fast_GET_ITEM() returns a borrowed reference
        curr->value = item;

        // advance node according to step size
        if (seq_index != seq_length) {  // don't jump on final iteration
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
    size_t end,
    size_t abs_step,
    PyObject* sequence,
    bool reverse
) {
    // NOTE: this method should only be used in ListViews where the size of
    // iterable is the same as the length of the slice.  This allows us to
    // avoid NULL checks during slice assignment, and guarantees that we won't
    // encounter an error partway through the process.
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence) - 1;

    // get first node in slice by iterating from tail
    U* curr = tail;
    for (size_t i = view->size - 1; i > begin; i--) {
        curr = (U*)curr->prev;
    }

    // iterate through sequence
    PyObject* item;
    for (Py_ssize_t seq_index = 0; seq_index <= seq_length; seq_index++) {
        // get item from sequence
        if (reverse) {  // iterate from right to left
            item = PySequence_Fast_GET_ITEM(sequence, seq_length - seq_idx);
        } else {  // iterate from left to right
            item = PySequence_Fast_GET_ITEM(sequence, seq_idx);
        }

        // overwrite node's current value
        Py_DECREF(curr->value);
        Py_INCREF(item);  // Fast_GET_ITEM() returns a borrowed reference
        curr->value = item;

        // advance node according to step size
        if (seq_index != seq_length) {  // don't jump on final iteration
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
    size_t end,
    size_t abs_step,
    PyObject* sequence,
    bool reverse
) {
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence) - 1;

    // get first node in slice by iterating from head
    U* prev;
    U* curr = head;
    for (size_t i = 0; i < begin; i++) {
        prev = curr;
        curr = (U*)curr->next;
    }

    // remember beginning of slice so we can reset later
    U* first_prev = prev;
    U* first = curr;
    std::queue<U*> removed = new std::queue<U*>();

    // loop 1: unlink nodes in slice
    U* next;
    size_t small_step = abs_step - 1;  // we implicitly jump by 1 when we remove a node
    for (size_t i = begin; i <= end; i += abs_step) {
        // unlink node
        next = (U*)curr->next;
        view->unlink(prev, curr, next);  // also removes from hash table, if applicable
        removed.push(curr);  // push to recovery queue

        // advance node according to step size
        if (i != end) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = (U*)curr->next;
            }
        }
    }

    // loop 2: insert new nodes from sequence
    PyObject* item;
    prev = first_prev;  // reset to beginning of slice
    for (Py_ssize_t seq_index = 0; seq_index <= seq_length; seq_index++) {
        // get item from sequence
        if (reverse) {  // reverse sequence as we add nodes
            item = PySequence_Fast_GET_ITEM(sequence, seq_length - seq_idx);
        } else {
            item = PySequence_Fast_GET_ITEM(sequence, seq_idx);
        }

        // allocate a new node
        curr = view->allocate(item);
        if (curr == NULL) {  // MemoryError() or TypeError() during hash()
            PyErr_NoMemory();
            _undo_set_slice_forward(  // replace original nodes
                view,
                removed,
                seq_index,
                first_prev,
                abs_step
            );
            return;
        }

        // link to list
        if (prev == NULL) {  // slice includes original head of list
            next = view->head;  // points to new head (head->next)
        } else {
            next = (U*)prev->next;
        }
        view->link(prev, curr, next);
        if (PyErr_Occurred()) {  // error during link()
            _undo_set_slice_forward(  // replace original nodes
                view,
                removed,
                seq_index,
                first_prev,
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
    U* head,
    size_t begin,
    size_t end,
    size_t abs_step,
    PyObject* sequence,
    bool reverse
) {
    Py_ssize_t seq_length = PySequence_Fast_GET_SIZE(sequence) - 1;

    // get first node in slice by iterating from head
    U* next;
    U* curr = tail;
    for (size_t i = 0; i < begin; i++) {
        next = curr;
        curr = (U*)curr->prev;
    }

    // remember beginning of slice so we can reset later
    U* first_next = next;
    U* first = curr;
    std::queue<U*> removed = new std::queue<U*>();

    // loop 1: unlink nodes in slice
    U* prev;
    size_t small_step = abs_step - 1;  // we implicitly jump by 1 when we remove a node
    for (size_t i = begin; i <= end; i += abs_step) {
        // unlink node
        prev = (U*)curr->prev;
        view->unlink(prev, curr, next);  // also removes from hash table, if applicable
        removed.push(curr);  // push to recovery queue

        // advance node according to step size
        if (i != end) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = (U*)curr->prev;
            }
        }
    }

    // loop 2: insert new nodes from sequence
    PyObject* item;
    next = first_next;  // reset to beginning of slice
    for (Py_ssize_t seq_index = 0; seq_index <= seq_length; seq_index++) {
        // get item from sequence
        if (reverse) {  // reverse sequence as we add nodes
            item = PySequence_Fast_GET_ITEM(sequence, seq_length - seq_idx);
        } else {
            item = PySequence_Fast_GET_ITEM(sequence, seq_idx);
        }

        // allocate a new node
        curr = view->allocate(item);
        if (curr == NULL) {  // MemoryError() or TypeError() during hash()
            PyErr_NoMemory();
            _undo_set_slice_backward(  // replace original nodes
                view,
                removed,
                seq_index,
                first_prev,
                abs_step
            );
            return;
        }

        // link to list
        if (next == NULL) {  // slice includes original head of list
            prev = view->tail;  // points to new tail (tail->prev)
        } else {
            prev = (U*)next->prev;
        }
        view->link(prev, curr, next);
        if (PyErr_Occurred()) {  // error during link()
            _undo_set_slice_backward(  // replace original nodes
                view,
                removed,
                seq_index,
                first_prev,
                abs_step
            );
            return;
        }

        // advance node according to step size
        for (size_t j = 0; j < abs_step; j++) {
            next = curr;
            curr = (U*)curr->prev;
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


////////////////////////////
////    DELETE SLICE    ////
////////////////////////////


/*Delete a slice within a linked list.*/
template <template <typename> class ViewType, typename NodeType>
void delete_slice_single(
    ViewType<NodeType>* view,
    size_t start,
    size_t stop,
    ssize_t step
) {
    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds = _get_slice_direction_single(
        start,
        stop,
        step,
        view->size
    );

    // iterate forward from head
    _drop_slice_forward(
        view,
        view->head,
        bounds.first,
        bounds.second,
        (size_t)abs(step)
    );
}


/*Delete a slice within a linked list.*/
template <template <typename> class ViewType, typename NodeType>
void delete_slice_double(
    ViewType<NodeType>* view,
    size_t start,
    size_t stop,
    ssize_t step
) {
    // determine direction of traversal to avoid backtracking
    std::pair<size_t, size_t> bounds = _get_slice_direction_double(
        start,
        stop,
        step,
        view->size
    );

    // iterate from nearest end of list
    if (bounds.first <= bounds.second) {  // forward traversal
        _drop_slice_forward(
            view,
            view->head,
            bounds.first,
            bounds.second,
            (size_t)abs(step)
        );
    } else {
        // backward traversal
        _drop_slice_backward(
            view,
            view->tail,
            bounds.first,
            bounds.second,
            (size_t)abs(step)
        );
    }
}


/* Remove a slice from left to right. */
template <template <typename> class ViewType, typename T, typename U>
void _drop_slice_forward(
    ViewType<T>* view,
    U* head,
    size_t begin,
    size_t end,
    size_t abs_step
) {
    // skip to start index
    U* prev = NULL;
    U* curr = head;
    for (size_t i = 0; i < begin; i++) {
        prev = curr;
        curr = (U*)curr->next;
    }

    // delete all nodes in slice
    U* next;
    size_t small_step = abs_step - 1;  // we implicitly jump by 1 when we remove a node
    for (size_t i = begin; i <= end; i += abs_step) {
        // unlink and deallocate node
        next = (U*)curr->next;
        view->unlink(prev, curr, next);
        deallocate(curr);
        curr = next;

        // advance node according to step size
        if (i != end) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = (U*)curr->next;
            }
        }
    }
}


/* Remove a slice from right to left. */
template <templace <typename> class ViewType, typename T, typename U>
void _drop_slice_backward(
    ViewType<T>* view,
    U* curr,
    size_t begin,
    size_t end,
    size_t abs_step
) {
    // skip to start index
    U* next = NULL;
    U* curr = head;
    for (size_t i = view->size - 1; i > begin; i--) {
        next = curr;
        curr = (U*)curr->prev;
    }

    // delete all nodes in slice
    U* prev;
    size_t small_step = abs_step - 1;  // we implicitly jump by 1 when we remove a node
    for (size_t i = begin; i >= end; i -= abs_step) {
        // unlink and deallocate node
        prev = (U*)curr->prev;
        view->unlink(prev, curr, next);
        deallocate(curr);
        curr = prev;

        // advance node according to step size
        if (i != end) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                next = curr;
                curr = (U*)curr->prev;
            }
        }
    }
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Get the direction in which to traverse a singly-linked slice that minimizes
total iterations and avoids backtracking. */
inline std::pair<size_t, size_t> _get_slice_direction_single(
    size_t start,
    size_t stop,
    ssize_t step,
    size_t size
) {
    size_t begin, end;

    // NOTE: because the list is singly-linked, we always have to iterate
    // from the head of the list.  If the step size is negative, this means
    // we'll be iterating over the slice in reverse order.
    if (step > 0) {
        begin = start;
        end = stop;
    } else {  // iterate over slice in reverse
        begin = stop;
        end = start;
    }

    // return as pair
    return std::make_pair(begin, end);
}


/* Get the direction in which to traverse a doubly-linked slice that minimizes
total iterations and avoids backtracking. */
inline std::pair<size_t, size_t> _get_slice_direction_double(
    size_t start,
    size_t stop,
    ssize_t step,
    size_t size
) {
    size_t begin, end;

    // NOTE: we choose the direction of traversal based on which of
    // `start`/`stop` is closer to its respective end of the list.  This is
    // determined in part by the sign of the `step`, but may not always
    // match it.  We can, for instance, iterate over a slice in reverse
    // order to avoid backtracking.  In this case, the signs of `step` and
    // `end - begin` may be anti-correlated.
    if ((step > 0 && start <= size - stop) || (step < 0 && size - start <= stop)) {
        begin = start;
        end = stop;
    } else {  // iterate over slice in reverse
        begin = stop;
        end = start;
    }

    // return as pair
    return std::make_pair(begin, end);
}


/* Undo a slice assignment. */
template <templace <typename> class ViewType, typename T, typename U>
void _undo_set_slice_forward(
    ViewType<T>* view,
    std::queue<U*> removed,
    Py_ssize_t n_staged,
    U* first_prev,
    size_t abs_step
) {
    U* prev = first_prev;
    U* curr;
    if (prev == NULL) {  // slice includes original head of list
        curr = view->head;  // points to new head (head->next)
    } else {
        curr = (U*)prev->next;
    }

    // loop 3: unlink and deallocate nodes that have already been added to slice
    U* next;
    size_t small_step = abs_step - 1;  // we implicitly jump by 1 when we remove a node
    for (Py_ssize_t i = 0; i < n_staged; i++) {
        // unlink node
        next = (U*)curr->next;
        view->unlink(prev, curr, next);  // also removes from hash table, if applicable
        view->deallocate(curr);

        // advance node according to step size
        if (i != n_staged) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                prev = curr;
                curr = (U*)curr->next;
            }
        }
    }

    // loop 4: reinsert original nodes
    prev = first_prev;
    for (size_t i = 0; i < removed.size(); i++) {
        curr = removed.pop();  // get next node from queue

        // link to list
        if (prev == NULL) {  // slice includes original head of list
            next = view->head;  // points to new head (head->next)
        } else {
            next = (U*)prev->next;
        }
        view->link(prev, curr, next);  // NOTE: cannot cause error

        // advance node according to step size
        for (size_t j = 0; j < abs_step; j++) {
            prev = curr;
            curr = (U*)curr->next;
            if (curr == NULL) {  // exhaust the sequence if we hit the end of the list
                break;
            }
        }
    }
}


/* Undo a slice assignment. */
template <templace <typename> class ViewType, typename T, typename U>
void _undo_set_slice_backward(
    ViewType<T>* view,
    std::queue<U*> removed,
    Py_ssize_t n_staged,
    U* first_next,
    size_t abs_step
) {
    U* next = first_next;
    U* curr;
    if (prev == NULL) {  // slice includes original head of list
        curr = view->tail;  // points to new head (tail->prev)
    } else {
        curr = (U*)next->prev;
    }

    // loop 3: unlink and deallocate nodes that have already been added to slice
    U* prev;
    size_t small_step = abs_step - 1;  // we implicitly jump by 1 when we remove a node
    for (Py_ssize_t i = 0; i < n_staged; i++) {
        // unlink node
        prev = (U*)curr->prev;
        view->unlink(prev, curr, next);  // also removes from hash table, if applicable
        view->deallocate(curr);

        // advance node according to step size
        if (i != n_staged) {  // don't jump on final iteration
            for (size_t j = 0; j < small_step; j++) {
                next = curr;
                curr = (U*)curr->prev;
            }
        }
    }

    // loop 4: reinsert original nodes
    next = first_next;
    for (size_t i = 0; i < removed.size(); i++) {
        curr = removed.pop();  // get next node from queue

        // link to list
        if (prev == NULL) {  // slice includes original head of list
            prev = view->tail;  // points to new tail (tail->prev)
        } else {
            prev = (U*)next->prev;
        }
        view->link(prev, curr, next);  // NOTE: cannot cause error

        // advance node according to step size
        for (size_t j = 0; j < abs_step; j++) {
            next = curr;
            curr = (U*)curr->prev;
            if (curr == NULL) {  // exhaust the sequence if we hit the end of the list
                break;
            }
        }
    }
}


#endif // SLICE_H include guard
