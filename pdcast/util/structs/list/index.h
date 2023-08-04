
// include guard prevents multiple inclusion
#ifndef INDEX_H
#define INDEX_H

#include <limits>  // for std::numeric_limits
#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <node.h>  // for node definitions, views, etc.


// TODO: might consider splitting all of these into separate files

// node.h       (node definitions)
// view.h       (views, incl. hash table + clear, copy, nbytes, search)
// append.h     (append, appendleft)
// extend.h     (extend, extendleft, extendafter, extendbefore)
// insert.h     (insert, insertbefore, insertafter)
// remove.h     (remove, discard)
// pop.h        (pop, popright, popleft)
// index.h      (index)
// count.h      (count)
// move.h       (rotate, reverse, move)
// sort.h       (sort)
// slice.h      (get_slice, set_slice, delete_slice)
// contains.h   (contains)

// might completely block off Cython access to memory allocation/link/unlink


//////////////////////
////    PUBLIC    ////
//////////////////////


const size_t MAX_SIZE_T = std::numeric_limits<size_t>::max();


/* Allow Python-style negative indexing with wraparound. */
inline size_t normalize_index(
    long long index,
    size_t size,
    bool truncate = false
) {
    // wraparound
    if (index < 0) {
        index += size;
    }

    // boundscheck
    if (index < 0 || index >= (long long)size) {
        if (truncate) {
            if (index < 0) {
                return 0;
            }
            return size - 1;
        }
        PyErr_SetString(IndexError, "list index out of range");
        return MAX_SIZE_T;
    }

    return (size_t)index;
}


// NOTE: we use namespaces to distinguish between algorithms that operate on
// singly-linked lists vs doubly-linked ones.  Each function produces identical
// results, but those in the `DoubleIndex` namespace contain specific
// optimizations related to the extra `prev` pointer at each node.


// TODO: implement pop()

// TODO: node_at_index() might be redundant.


/* Index operations for singly-linked lists. */
namespace SingleIndex {

    /* Get a node at a given index of a list. */
    template <typename NodeType>
    inline NodeType* node_at_index(ListView<NodeType>* view, size_t index) {
        NodeType* curr = view->head;
        for (size_t i = 0; i < index; i++) {
            curr = curr->next;
        }
        return curr;
    }

    /* Get a node at a given index of a set. */
    template <typename NodeType>
    inline Hashed<NodeType>* node_at_index(SetView<NodeType>* view, size_t index) {
        Hashed<NodeType>* curr = view->head;
        for (size_t i = 0; i < index; i++) {
            curr = (Hashed<NodeType>*)curr->next;
        }
        return curr;
    }

    /* Get a node at a given index of a dictionary. */
    template <typename NodeType>
    inline Mapped<NodeType>* node_at_index(DictView<NodeType>* view, size_t index) {
        Mapped<NodeType>* curr = view->head;
        for (size_t i = 0; i < index; i++) {
            curr = (Mapped<NodeType>*)curr->next;
        }
        return curr;
    }

    /* Get the index of an item within a list. */
    template <typename NodeType>
    inline size_t index(
        ListView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        // iterate forward from head
        return _get_index_single(view->head, view->size, item, start, stop);
    }

    /* Get the index of an item within a set. */
    template <typename NodeType>
    inline size_t index(
        SetView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        // check if item is in set
        if (view->search(item) == NULL) {
            PyErr_Format(PyExc_ValueError, "%R is not in set", item);
            return MAX_SIZE_T;
        }

        // iterate forward from head
        return _get_index_single(view->head, view->size, item, start, stop);
    }

    /* Get the index of a key within a dictionary. */
    template <typename NodeType>
    inline size_t index(
        DictView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        // check if item is in set
        if (view->search(item) == NULL) {
            PyErr_Format(PyExc_ValueError, "%R is not in set", item);
            return MAX_SIZE_T;
        }

        // iterate forward from head
        return _get_index_single(view->head, view->size, item, start, stop);
    }

    /* Count the number of occurrences of an item within a list. */
    template <typename NodeType>
    size_t count(
        ListView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        // normalize range
        size_t norm_start = normalize_index(start, view->size, true);
        size_t norm_stop = normalize_index(stop, view->size, true);
        if (norm_start > norm_stop) {
            PyErr_SetString(
                PyExc_ValueError,
                "start index must be less than or equal to stop index"
            );
            return MAX_SIZE_T;
        }

        // NOTE: we don't need to check for NULL due to normalize_index()

        // skip to start index
        NodeType* curr = view->head;
        size_t idx = 0;
        for (idx; idx < norm_start; idx++) {
            curr = curr->next;
        }

        // search until we hit stop index
        int comp;
        size_t observed = 0;
        while (idx < norm_stop) {
            // C API equivalent of the == operator
            comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
            if (comp == -1) {  // comparison raised an exception
                return MAX_SIZE_T;
            } else if (comp == 1) {  // found a match
                count++;
            }

            // advance to next node
            curr = curr->next;
            idx++;
        }

        return observed;
    }

    /* Count the number of occurrences of an item within a set. */
    template <typename NodeType>
    size_t count(
        SetView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        // check if item is in set
        if (view->search(item) == NULL) {
            return 0;
        }

        // normalize range
        size_t norm_start = normalize_index(start, view->size, true);
        size_t norm_stop = normalize_index(stop, view->size, true);
        if (norm_start > norm_stop) {
            PyErr_SetString(
                PyExc_ValueError,
                "start index must be less than or equal to stop index"
            );
            return MAX_SIZE_T;
        }

        // if range includes all items, return 1
        if (norm_start == 0 && norm_stop == view->size - 1) {
            return 1;
        }

        // find index of item
        size_t idx = index(view, item);
        if (idx >= start && idx < stop) {
            return 1;
        }
        return 0;
    }

    /* Count the number of occurrences of an item within a set. */
    template <typename NodeType>
    size_t count(
        DictView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        // check if item is in set
        if (view->search(item) == NULL) {
            return 0;
        }

        // normalize range
        size_t norm_start = normalize_index(start, view->size, true);
        size_t norm_stop = normalize_index(stop, view->size, true);
        if (norm_start > norm_stop) {
            PyErr_SetString(
                PyExc_ValueError,
                "start index must be less than or equal to stop index"
            );
            return MAX_SIZE_T;
        }

        // if range includes all items, return 1
        if (norm_start == 0 && norm_stop == view->size - 1) {
            return 1;
        }

        // else, find index of item
        size_t idx = index(view, item);
        if (idx >= start && idx < stop) {
            return 1;
        }
        return 0;
    }

    // TODO: specialize for sets and dicts to search item directly.
    // -> remember to cast curr to Hashed or Mapped whenever we advance to next/prev

    /*Insert an item at the given index.*/
    template <typename NodeType>
    void insert(ListView<NodeType>* PyObject* item, long long index) {
        // normalize index
        size_t norm_index = normalize_index(index, view->size, true);

        // allocate a new node
        NodeType* node = view->allocate(item);
        if (node == NULL) {  // MemoryError() or TypeError() during hash()
            return;
        }

        // iterate from head
        NodeType* curr = view->head;
        NodeType* prev = NULL;  // shadows curr
        for (size_t i = 0; i < norm_index; i++) {
            prev = curr;
            curr = curr->next;
        }

        // insert node
        view->link(prev, node, curr);
    }

    // TODO: specialize for sets and dicts to search for item before iterating.

}


/* Index operations for doubly-linked lists. */
namespace DoubleIndex {

    /* Get a node at a given index. */
    template <template<typename> class ViewType, typename NodeType>
    inline NodeType* node_at_index(ViewType<NodeType>* view, size_t index) {
        // if index is closer to head, use singly-linked version
        if (index <= view->size / 2) {
            return SingleIndex::node_at_index(view, index)
        }

        // else, start from tail
        NodeType* curr = view->tail;
        for (size_t i = view->size - 1; i > index; i--) {
            curr = curr->prev;
        }
        return curr;
    }

    /* Get the index of an item within the list. */
    template <template<typename> class ViewType, typename NodeType>
    size_t index(
        ViewType<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        size_t norm_start = normalize_index(start, view->size, true);
        size_t norm_stop = normalize_index(stop, view->size, true);
        if (norm_start > norm_stop) {
            PyErr_SetString(
                PyExc_ValueError,
                "start index must be less than or equal to stop index"
            );
            return MAX_SIZE_T;
        }

        // if starting index is closer to head, use singly-linked version
        if (norm_start <= view->size / 2) {
            return SingleIndex::index(view, item, start, stop);
        }

        // else, start from tail
        NodeType* curr = view->tail;
        size_t i = view->size - 1;
        for (i; i >= norm_stop; i--) {  // skip to stop index
            curr = curr->prev;
        }

        // search until we hit start index
        int comp;
        size_t last_observed;
        bool found = false;
        while (i >= norm_start) {
            // C API equivalent of the == operator
            comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
            if (comp == -1) {  // comparison raised an exception
                return MAX_SIZE_T;
            } else if (comp == 1) {  // found a match
                last_observed = i;
                found = true;
            }

            // advance to next node
            curr = curr->prev;
            i--;
        }

        // return first occurrence in range
        if (found) {
            return last_observed;
        }

        // item not found
        PyErr_Format(PyExc_ValueError, "%R is not in list", item);
        return MAX_SIZE_T;
    }

    /* Get the index of an item within the set. */
    template <typename NodeType>
    size_t index(
        SetView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        size_t norm_start = normalize_index(start, view->size, true);
        size_t norm_stop = normalize_index(stop, view->size, true);
        if (norm_start > norm_stop) {
            PyErr_SetString(
                PyExc_ValueError,
                "start index must be less than or equal to stop index"
            );
            return MAX_SIZE_T;
        }

        // check if item is in set
        Hashed<NodeType>* node = view->search(item);
        if (node == NULL) {
            PyErr_Format(PyExc_ValueError, "%R is not in set", item);
            return MAX_SIZE_T;
        }

        // count backwards to find index
        size_t idx = 0;
        while (node != NULL && idx < norm_stop) {
            node = node->prev;
            idx++;
        }

        // check if index is in range
        if (idx >= norm_start && idx < norm_stop) {
            return idx;
        }

        // item not found
        PyErr_Format(PyExc_ValueError, "%R is not in set", item);
        return MAX_SIZE_T;
    }

    /* Get the index of an item within the set. */
    template <typename NodeType>
    size_t index(
        DictView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        size_t norm_start = normalize_index(start, view->size, true);
        size_t norm_stop = normalize_index(stop, view->size, true);
        if (norm_start > norm_stop) {
            PyErr_SetString(
                PyExc_ValueError,
                "start index must be less than or equal to stop index"
            );
            return MAX_SIZE_T;
        }

        // check if item is in set
        Mapped<NodeType>* node = view->search(item);
        if (node == NULL) {
            PyErr_Format(PyExc_ValueError, "%R is not in set", item);
            return MAX_SIZE_T;
        }

        // count backwards to find index
        size_t idx = 0;
        while (node != NULL && idx < norm_stop) {
            node = node->prev;
            idx++;
        }

        // check if index is in range
        if (idx >= norm_start && idx < norm_stop) {
            return idx;
        }

        // item not found
        PyErr_Format(PyExc_ValueError, "%R is not in set", item);
        return MAX_SIZE_T;
    }

    // TODO: count() should search the slice backwards if start > size / 2

    /* Count the number of occurrences of an item within the list. */
    template <template <typename> class ViewType, typename NodeType>
    size_t count(
        ViewType<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        size_t norm_start = normalize_index(start, view->size, true);
        size_t norm_stop = normalize_index(stop, view->size, true);
        if (norm_start > norm_stop) {
            PyErr_SetString(
                PyExc_ValueError,
                "start index must be less than or equal to stop index"
            );
            return MAX_SIZE_T;
        }

        // if starting index is closer to head, use singly-linked version
        if (norm_start <= view->size / 2) {
            return SingleIndex::count(view, item, start, stop);
        }

        // else, start from tail
        NodeType* curr = view->tail;
        size_t i = view->size - 1;
        for (i; i >= norm_stop; i--) {  // skip to stop index
            curr = curr->prev;
        }

        // search until we hit start index
        int comp;
        size_t observed = 0;
        while (i >= norm_start) {
            // C API equivalent of the == operator
            comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
            if (comp == -1) {  // comparison raised an exception
                return MAX_SIZE_T;
            } else if (comp == 1) {  // found a match
                observed++;
            }

            // advance to next node
            curr = curr->prev;
            i--;
        }

        // return final count
        return observed;
    }

    /* Count the number of occurrences of an item within the set. */
    template <typename NodeType>
    size_t count(
        SetView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        // check if item is in set
        if (view->search(item) == NULL) {
            return 0;
        }

        size_t norm_start = normalize_index(start, view->size, true);
        size_t norm_stop = normalize_index(stop, view->size, true);
        if (norm_start > norm_stop) {
            PyErr_SetString(
                PyExc_ValueError,
                "start index must be less than or equal to stop index"
            );
            return MAX_SIZE_T;
        }

        // find index of item
        size_t idx = index(view, item);
        if (idx >= start && idx < stop) {
            return 1;
        }
        return 0;
    }

    /* Count the number of occurrences of a key within the dictionary. */
    template <typename NodeType>
    size_t count(
        DictView<NodeType>* view,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ) {
        size_t norm_start = normalize_index(start, view->size, true);
        size_t norm_stop = normalize_index(stop, view->size, true);
        if (norm_start > norm_stop) {
            PyErr_SetString(
                PyExc_ValueError,
                "start index must be less than or equal to stop index"
            );
            return MAX_SIZE_T;
        }

        // check if item is in set
        if (view->search(item) == NULL) {
            return 0;
        }

        // find index of item
        size_t idx = index(view, item);
        if (idx >= start && idx < stop) {
            return 1;
        }
        return 0;
    }

    /*Insert an item at the given index.*/
    template <template <typename> class ViewType, typename NodeType>
    void insert(ViewType<NodeType>* view, long long index, PyObject* item) {
        // normalize index
        size_t norm_index = normalize_index(index, view->size, true);

        // if index is closer to head, use singly-linked version
        if (norm_index <= view->size / 2) {
            return SingleIndex::insert(view, item, index);
        }

        // else, start from tail
        NodeType* node = view->allocate(item);
        if (node == NULL) {  // MemoryError() or TypeError() during hash()
            return;
        }

        // find node at index
        NodeType* curr = view->tail;
        NodeType* next = NULL;  // shadows curr
        for (size_t i = view->size - 1; i > norm_index; i--) {
            next = curr;
            curr = curr->prev;
        }

        // insert node
        view->link(curr, node, next);
    }





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


/* Helper for getting the index of an item within a singly-linked list, set, or
dictionary. */
template <typename NodeType>
size_t _get_index_single(
    NodeType* head,
    size_t size,
    PyObject* item,
    long long start,
    long long stop
) {
    // normalize range
    size_t norm_start = normalize_index(start, size, true);
    size_t norm_stop = normalize_index(stop, size, true);
    if (norm_start > norm_stop) {
        PyErr_SetString(
            PyExc_ValueError,
            "start index must be less than or equal to stop index"
        );
        return MAX_SIZE_T;
    }

    // NOTE: we don't need to check for NULL due to normalize_index()

    // skip to start index
    NodeType* curr = head;
    size_t idx = 0;
    for (idx; idx < norm_start; i++) {
        curr = (NodeType*)curr->next;
    }

    // search until we hit stop index
    int comp;
    while (idx < norm_stop) {
        // C API equivalent of the == operator
        comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
        if (comp == -1) {  // comparison raised an exception
            return MAX_SIZE_T;
        } else if (comp == 1) {  // found a match
            return idx;
        }

        // advance to next node
        curr = (NodeType*)curr->next;
        idx++;
    }

    // item not found
    PyErr_Format(PyExc_ValueError, "%R is not in list", item);
    return MAX_SIZE_T;

}


////////////////////
////    BASE    ////
////////////////////


const size_t MAX_SIZE_T = std::numeric_limits<size_t>::max();


template <typename ViewType, typename NodeType>
class ListOps {
private:
    ViewType<NodeType>* view;


public:
    ListOps(ViewType<NodeType>* view) {
        this->view = view;
    }


    // // TODO: check if index bounds are correct.  Do they include the last item
    // // in the list?

    // /*Remove an item from the list.*/
    // int remove(PyObject* item) {
    //     T* curr = head;
    //     T* prev = NULL;  // shadows curr
    //     int comp;

    //     // remove first occurrence of item
    //     while (curr != NULL) {
    //         // C API equivalent of the == operator
    //         comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
    //         if (comp == -1) {  // comparison raised an exception
    //             return -1;
    //         } else if (comp == 1) {  // found a match
    //             unlink(prev, curr, curr->next);
    //             deallocate(curr);
    //             return 0;
    //         }

    //         // advance to next node
    //         prev = curr
    //         curr = curr->next;
    //     }

    //     // item not found
    //     PyObject* python_repr = PyObject_Repr(item);
    //     const char* c_repr = PyUnicode_AsUTF8(python_repr);
    //     Py_DECREF(python_repr);
    //     PyErr_Format(PyExc_ValueError, "%s is not in list", c_repr);
    //     return -1;
    // }

    /*Pop an item at the given index and return its value.*/
    PyObject* pop(long long index = -1) {
        size_t norm_index = normalize_index(index);
        NodeType* curr = node_at_index(norm_index);

        // get return value
        PyObject* value = curr->value;
        Py_INCREF(value);  // have to INCREF because we DECREF in deallocate()

        // unlink and deallocate node
        view->unlink(curr->prev, curr, curr->next);
        view->deallocate(curr);
        return value;
    }

    /*Pop the first item in the list.*/
    PyObject* popleft() {
        if (view->head == NULL) {
            PyErr_SetString(PyExc_IndexError, "pop from empty list");
            return NULL;
        }

        // get return value
        PyObject* value = view->head->value;
        Py_INCREF(value);  // have to INCREF because we DECREF in deallocate()

        // unlink and deallocate node
        view->unlink(NULL, view->head, view->head->next);
        view->deallocate(view->head);
        return value;
    }

    /*Pop the last item in the list.*/
    PyObject* popright() {
        if (view->tail == NULL) {
            PyErr_SetString(PyExc_IndexError, "pop from empty list");
            return NULL;
        }

        // get return value
        PyObject* value = view->tail->value;
        Py_INCREF(value);  // have to INCREF because we DECREF in deallocate()

        // unlink and deallocate node
        view->unlink(view->tail->prev, view->tail, NULL);
        view->deallocate(view->tail);
        return value;
    }

    /*Rotate the list to the right by the specified number of steps.*/
    void rotate(long long steps = 1) {
        // trivial case: empty list
        if (view->head == NULL) {
            return;
        }

        NodeType* curr;
        size_t abs_steps = (size_t)abs(steps);

        // rotate the list
        if (steps > 0) {
            for (size_t i = 0; i < abs_steps; i++) {
                curr = view->tail;
                view->unlink(curr->prev, curr, NULL);
                view->link(NULL, curr, view->head);
            }
        } else {
            for (size_t i = 0; i < abs_steps; i++) {
                curr = view->head;
                view->unlink(NULL, curr, curr->next);
                view->link(view->tail, curr, NULL);
            }
        }
    }

    /*Reverse the list in-place.*/
    void reverse() {
        NodeType* curr = view->head;
        NodeType* temp;

        // swap prev and next pointers for each node
        while (curr != NULL) {
            temp = curr->next;
            curr->next = curr->prev;
            curr->prev = temp;
            curr = temp;
        }

        // swap head and tail pointers
        temp = view->head;
        view->head = view->tail;
        view->tail = temp;
    }

};



// TODO: SingleIndex, DoubleIndex, ImmutableIndex, SetIndex, DictIndex















#endif // INDEX_H include guard
