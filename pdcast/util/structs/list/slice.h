
// include guard prevents multiple inclusion
#ifndef SLICE_H
#define SLICE_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <utility>  // for std::pair
#include <view.h>  // for views
#include <index.h>  // for index(), normalize_index()

//////////////////////
////    PUBLIC    ////
//////////////////////


namespace SinglyLinked {

}


namespace DoublyLinked {

    /*Extract a slice from a linked list.*/
    template <template<typename> class ViewType, typename NodeType>
    ViewType<NodeType> get_slice(size_t start, size_t stop, ssize_t step) {
        ViewType<NodeType>* slice = new ViewType<NodeType>();
        std::pair<size_t, size_t> index;

        // determine direction of traversal to avoid backtracking
        index = get_slice_direction(start, stop, step);
        bool descending = (step < 0);
        size_t abs_step = (size_t)abs(step);

        // get first node in slice
        NodeType* curr = node_at_index(index.first);
        NodeType* copy;

        // copy all nodes in slice
        if (index.first <= index.second) {  // forward traversal
            while (curr != NULL && index.first <= index.second) {
                // TODO: catch bad_alloc
                copy = NodeType::copy(slice->freelist, curr);
                if (descending) {
                    slice->link(NULL, copy, slice->head);
                } else {
                    slice->link(slice->tail, copy, NULL);
                }

                // jump according to step size
                index.first += abs_step;
                for (size_t i = 0; i < abs_step; i++) {
                    curr = curr->next;
                    if (curr == NULL) {
                        break;
                    }
                }
            }
        } else {  // backward traversal
            while (curr != NULL && index.first >= index.second) {
                // TODO: catch bad_alloc
                copy = NodeType::copy(slice->freelist, curr);
                if (descending) {
                    slice->link(slice->tail, copy, NULL);
                } else {
                    slice->link(NULL, copy, slice->head);
                }

                // jump according to step size
                index.first -= abs_step;
                for (size_t i = 0; i < abs_step; i++) {
                    curr = curr->prev;
                    if (curr == NULL) {
                        break;
                    }
                }
            }
        }

        return slice;
    }

    /*Set a slice within a linked list.*/
    int set_slice(size_t start, size_t stop, ssize_t step, PyObject* iterator) {
        size_t abs_step = (size_t)abs(step);
        std::pair<size_t, size_t> index;

        // determine direction of traversal to avoid backtracking
        index = get_slice_direction(start, stop, step);

        // get first node in slice
        NodeType* curr = node_at_index(index.first);

        // NOTE: we assume that the iterator is properly reversed if we are
        // traversing the slice opposite to `step`

        // assign to slice
        if (index.first <= index.second) {
            while (curr != NULL and index.first <= index.second) {
                // C API equivalent of next(iterator)
                PyObject* item = PyIter_Next(iterator);
                if (item == NULL) { // end of iterator or error
                    if (PyErr_Occurred()) {
                        Py_DECREF(item);
                        return -1;  // raise exception
                    }
                    break;
                }

                // assign to node (INCREF is handled by PyIter_Next())
                Py_DECREF(curr->value);
                curr->value = item;

                // jump according to step size
                index.first += abs_step;
                for (size_t i = 0; i < abs_step; i++) {
                    curr = curr->next;
                    if (curr == NULL) {
                        break;
                    }
                }
            }
        } else {
            while (curr != NULL and index.first >= index.second) {
                // C API equivalent of next(iterator)
                PyObject* item = PyIter_Next(iterator);
                if (item == NULL) { // end of iterator or error
                    if (PyErr_Occurred()) {
                        Py_DECREF(item);
                        return -1;  // raise exception
                    }
                    break;
                }

                // assign to node (INCREF is handled by PyIter_Next())
                Py_DECREF(curr->value);
                curr->value = item;

                // jump according to step size
                index.first -= abs_step;
                for (size_t i = 0; i < abs_step; i++) {
                    curr = curr->prev;
                    if (curr == NULL) {
                        break;
                    }
                }
            }
        }

        return 0;  // return 0 on success
    }

    /*Delete a slice within a linked list.*/
    void delete_slice(size_t start, size_t stop, ssize_t step) {
        std::pair<size_t, size_t> index;

        // determine direction of traversal to avoid backtracking
        index = get_slice_direction(start, stop, step);
        size_t abs_step = (size_t)abs(step);
        size_t small_step = abs_step - 1;  // we jump by 1 whenever we delete a node

        // get first node in slice
        NodeType* curr = node_at_index(index.first);
        NodeType* temp;

        // delete all nodes in slice
        if (index.first <= index.second) {  // forward traversal
            while (curr != NULL && index.first <= index.second) {
                temp = curr->next;
                unlink(curr->prev, curr, curr->next);
                deallocate(curr);
                curr = temp;

                // jump according to step size
                index.first += abs_step;
                for (size_t i = 0; i < small_step; i++) {
                    curr = curr->next;
                    if (curr == NULL) {
                        break;
                    }
                }
            }
        } else {  // backward traversal
            while (curr != NULL && index.first >= index.second) {
                temp = curr->prev;
                unlink(curr->prev, curr, curr->next);
                deallocate(curr);
                curr = temp;

                // jump according to step size
                index.first -= abs_step;
                for (size_t i = 0; i < small_step; i++) {
                    curr = curr->prev;
                    if (curr == NULL) {
                        break;
                    }
                }
            }
        }
    }


}


///////////////////////
////    PRIVATE    ////
///////////////////////


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
    return std::make_pair(begin, end);
}


#endif // SLICE_H include guard
