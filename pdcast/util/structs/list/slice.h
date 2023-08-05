
// include guard prevents multiple inclusion
#ifndef SLICE_H
#define SLICE_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <utility>  // for std::pair
#include <view.h>  // for views


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace SinglyLinked {

    /* Get the direction in which to traverse a slice that minimizes iterations
    and avoids backtracking. */
    inline std::pair<size_t, size_t> get_slice_direction(
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

    /* Extract a slice from a singly-linked list. */
    template <template <typename> class ViewType, typename NodeType>
    inline ViewType<NodeType> get_slice(
        ViewType<NodeType>* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) {
        // determine direction of traversal to avoid backtracking
        std::pair<size_t, size_t> index = SinglyLinked::get_slice_direction(
            start,
            stop,
            step,
            view->size
        );

        // iterate forward from head
        return _get_slice_forward(
            view,
            view->head,
            index.first,
            index.second,
            (size_t)abs(step),
            (step < 0)
        );
    }

    /* Set a slice within a list. */
    template <typename NodeType>
    int set_slice(
        ListView<NodeType>* view,
        size_t start,
        size_t stop,
        ssize_t step,
        PyObject* items
    ) {
        // determine direction of traversal to avoid backtracking
        std::pair<size_t, size_t> index = SinglyLinked::get_slice_direction(
            start,
            stop,
            step,
            view->size
        );

        // NOTE: Since ListViews don't employ a hash table, we are free to
        // directly overwrite the values in the slice.  This avoids the need to
        // allocate new nodes and link them to the list manually.

        // iterate forward from head
        return _overwrite_slice_forward(
            view,
            view->head,
            index.first,
            index.second,
            (size_t)abs(step),
            items
        );
    }





    /*Delete a slice within a linked list.*/
    template <template <typename> class ViewType, typename NodeType>
    void delete_slice(
        ViewType<NodeType>* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) {
        // determine direction of traversal to avoid backtracking
        std::pair<size_t, size_t> index = DoublyLinked::get_slice_direction(
            start,
            stop,
            step,
            view->size
        );

        // iterate forward from head
        return _drop_slice_forward(
            view,
            view->head,
            index.first,
            index.second,
            (size_t)abs(step)
        );
    }

}


namespace DoublyLinked {

    /* Get the direction in which to traverse a slice that minimizes iterations
    and avoids backtracking. */
    inline std::pair<size_t, size_t> get_slice_direction(
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

    /* Extract a slice from a doubly-linked list. */
    template <template <typename> class ViewType, typename NodeType>
    inline ViewType<NodeType> get_slice(
        ViewType<NodeType>* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) {
        // determine direction of traversal to avoid backtracking
        std::pair<size_t, size_t> index = DoublyLinked::get_slice_direction(
            start,
            stop,
            step,
            view->size
        );

        // iterate from nearest end of list
        if (index.first <= index.second) {  // forward traversal
            return _get_slice_forward(
                view,
                view->head,
                index.first,
                index.second,
                (size_t)abs(step),
                (step < 0)
            );
        }

        // backward traversal
        return _get_slice_backward(
            view,
            view->tail,
            index.first,
            index.second,
            (size_t)abs(step),
            (step < 0)
        );
    }

    /* Set a slice within a list. */
    template <typename NodeType>
    void set_slice(
        ListView<NodeType>* view,
        size_t start,
        size_t stop,
        ssize_t step,
        PyObject* items
    ) {
        // determine direction of traversal to avoid backtracking
        std::pair<size_t, size_t> index = DoublyLinked::get_slice_direction(
            start,
            stop,
            step,
            view->size
        );

        // NOTE: Since ListViews don't employ a hash table, we are free to
        // directly overwrite the values in the slice.  This avoids the need to
        // allocate new nodes and link them to the list manually.

        // iterate from nearest end of list
        if (index.first <= index.second) {  // forward traversal
            return _overwrite_slice_forward(
                view,
                view->head,
                index.first,
                index.second,
                (size_t)abs(step),
                items
            );
        }

        // backward traversal
        return _overwrite_slice_backward(
            view,
            view->tail,
            index.first,
            index.second,
            (size_t)abs(step),
            items
        );
    }



    // TODO: sets and dicts are considerably more complicated, since we need to
    // manage the existing hash table and maintain uniqueness of keys.  We have
    // to be able to rewind the assignment if an error occurs.

    // process basically goes like this:
    //  1)  Iterate over slice and unlink existing nodes.  This removes them
    //      from the hash table.
    //  2)  Iterate over items and insert them into the list.  This checks for
    //      duplicates and raises an error if one is found, and because of the
    //      revious step, we will disregard the slice that we're replacing.

    //  If an error occurs, we need to restore the original slice.  This is
    //  where things get tricky.  We need to be able to store the original
    //  nodes that we unlinked in step 1.  When we add them back to the list,
    //  we first have to remove the existing values and repeat the process from
    //  step 2.  This prevents errors while reversing the operation.
    //  -> this might just be a recursive call to _set_slice_forward/backward().
    //  The iterable is just a queue of the original nodes, which are not
    //  deallocated until the end of the process.  The indices are just the
    //  indices we've traverse so far.

    // No matter what we do, we're going to have to store the removed nodes in
    // an auxiliary data structure, and deallocate them in a separate loop.



    /*Delete a slice within a linked list.*/
    template <template <typename> class ViewType, typename NodeType>
    void delete_slice(
        ViewType<NodeType>* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) {
        // determine direction of traversal to avoid backtracking
        std::pair<size_t, size_t> index = DoublyLinked::get_slice_direction(
            start,
            stop,
            step,
            view->size
        );

        // iterate from nearest end of list
        if (index.first <= index.second) {  // forward traversal
            return _drop_slice_forward(
                view,
                view->head,
                index.first,
                index.second,
                (size_t)abs(step)
            );
        }

        // backward traversal
        return _get_slice_backward(
            view,
            view->tail,
            index.first,
            index.second,
            (size_t)abs(step)
        );
    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


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
    while (curr != NULL && begin <= end) {
        copy = U::copy(slice->freelist, curr);
        if (copy == NULL) {  // MemoryError()
            PyErr_NoMemory();
            delete slice;
            return NULL;
        }

        // link to slice
        if (reverse) {  // reverse
            slice->link(NULL, copy, slice->head);
        } else {
            slice->link(slice->tail, copy, NULL);
        }
        if (PyErr_Occurred()) {  // error during link()
            delete slice;
            return NULL;
        }

        // jump according to step size
        begin += abs_step;
        for (size_t i = 0; i < abs_step; i++) {
            curr = (U*)curr->next;
            if (curr == NULL) {
                break;
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
    while (curr != NULL && begin >= end) {
        copy = U::copy(slice->freelist, curr);
        if (copy == NULL) {  // MemoryError()
            PyErr_NoMemory();
            delete slice;
            return NULL;
        }

        // link to slice
        if (descending) {
            slice->link(slice->tail, copy, NULL);
        } else {  // reverse
            slice->link(NULL, copy, slice->head);
        }
        if (PyErr_Occurred()) {  // error during link()
            delete slice;
            return NULL;
        }

        // jump according to step size
        begin -= abs_step;
        for (size_t i = 0; i < abs_step; i++) {
            curr = (U*)curr->prev;
            if (curr == NULL) {
                break;
            }
        }
    }

    // return new view
    return slice;
}


/* Assign a slice from left to right, directly overwriting exisiting values. */
template <template <typename> class ViewType, typename T, typename U>
void _overwrite_slice_forward(
    ViewType<T>* view,
    U* head,
    size_t begin,
    size_t end,
    size_t abs_step,
    PyObject* iterable
) {
    // CPython API equivalent of `iter(iterable)`
    PyObject* iterator = PyObject_GetIter(iterable);
    if (iterator == NULL) {  // TypeError()
        return NULL;
    }

    // get first node in slice by iterating from head
    U* curr = head;
    for (size_t i = 0; i < begin; i++) {
        curr = (U*)curr->next;
    }

    // overwrite values in slice
    PyObject* item;
    while (curr != NULL and begin <= end) {
        // C API equivalent of next(iterator)
        item = PyIter_Next(iterator);
        if (item == NULL) { // end of iterator or error
            break;
        }

        // assign to node (INCREF is handled by PyIter_Next())
        Py_DECREF(curr->value);
        curr->value = item;

        // jump according to step size
        begin += abs_step;
        for (size_t i = 0; i < abs_step; i++) {
            curr = (U*)curr->next;
            if (curr == NULL) {
                break;
            }
        }
    }

    // release iterator
    Py_DECREF(iterator);
}


/* Assign a slice from right to left, directly overwriting exisiting values. */
template <template <typename> class ViewType, typename T, typename U>
void _overwrite_slice_backward(
    ViewType<T>* view,
    U* tail,
    size_t begin,
    size_t end,
    size_t abs_step,
    PyObject* iterable
) {
    // CPython API equivalent of `iter(iterable)`
    PyObject* iterator = PyObject_GetIter(iterable);
    if (iterator == NULL) {  // TypeError()
        return NULL;
    }

    // get first node in slice by iterating from tail
    U* curr = tail;
    for (size_t i = view->size - 1; i > begin; i--) {
        curr = (U*)curr->prev;
    }

    // overwrite values in slice
    PyObject* item;
    while (curr != NULL and begin >= end) {
        // C API equivalent of next(iterator)
        PyObject* item = PyIter_Next(iterator);
        if (item == NULL) { // end of iterator or error
            break;
        }

        // assign to node (INCREF is handled by PyIter_Next())
        Py_DECREF(curr->value);
        curr->value = item;

        // jump according to step size
        begin -= abs_step;
        for (size_t i = 0; i < abs_step; i++) {
            curr = (U*)curr->prev;
            if (curr == NULL) {
                break;
            }
        }
    }

    // release iterator
    Py_DECREF(iterator);
}


/* Assign a slice from left to right, replacing the existing nodes with new ones. */



/* Assign a slice from right to left, replacing the existing nodes with new ones. */




/* Remove a slice from left to right. */
template <template <typename> class ViewType, typename T, typename U>
void _drop_slice_forward(
    ViewType<T>* view,
    U* head,
    size_t begin,
    size_t end,
    size_t abs_step
) {
    // NOTE: we implicitly advance by 1 whenever we delete a node.
    size_t small_step = abs_step - 1;

    // skip to start index
    U* prev = NULL;
    U* curr = head;
    for (size_t i = 0; i < begin; i++) {
        prev = curr;
        curr = (U*)curr->next;
    }

    // delete all nodes in slice
    U* next;
    while (curr != NULL && begin <= end) {
        next = (U*)curr->next;
        view->unlink(prev, curr, next);
        deallocate(curr);
        curr = next;

        // jump according to step size
        begin += abs_step;
        for (size_t i = 0; i < small_step; i++) {
            prev = curr;
            curr = (U*)curr->next;
            if (curr == NULL) {
                break;
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
    // NOTE: we implicitly advance by 1 whenever we delete a node.
    size_t small_step = abs_step - 1;

    // skip to start index
    U* next = NULL;
    U* curr = head;
    for (size_t i = view->size - 1; i > begin; i--) {
        next = curr;
        curr = (U*)curr->prev;
    }

    // delete all nodes in slice
    U* prev;
    while (curr != NULL && begin >= end) {
        prev = (U*)curr->prev;
        view->unlink(prev, curr, next);
        deallocate(curr);
        curr = prev;

        // jump according to step size
        begin -= abs_step;
        for (size_t i = 0; i < small_step; i++) {
            next = curr;
            curr = (U*)curr->prev;
            if (curr == NULL) {
                break;
            }
        }
    }
}


#endif // SLICE_H include guard
