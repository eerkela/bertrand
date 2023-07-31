// include guard prevents multiple inclusion
#ifndef INDEX_H
#define INDEX_H


#include <limits>
#include <Python.h>
#include <unordered_set>



// TODO: rename this file to ops.h.  It implements a bunch of mixin classes
// that are used to implement the various public methods of the list.

// IntegerIndex/MapIndex are two mixins that control the behavior of __getitem__()
// __setitem__() and __delitem__().




// TODO: all of these go in a separate object that receives a ListView in
// its constructor.  This handles all the indexing operations for the
// eventual LinkedList.  These are used to implement the __getitem__,
// __setitem__, and __delitem__ methods of the LinkedList class.

// ListIndex
// ListSort <- implements .sort() and is templated just the same as ListView
// and ListIndex

// MergeSort[ListView, DoubleNode]

// These are going to have to implement a second templated type for the
// return value of get_slice().  They can't always assume the result is
// going to be a ListView, because it might be a DictView or HashView

// ListIndex[ListView, DoubleNode]  <- normal doubly-linked list lookup
// SingleIndex[ListView, DoubleNode]  <- singly-linked list lookup with DoubleNodes
// TupleIndex[ListView, DoubleNode]  <- make a list immutable
// SetIndex[SetView, DictNode]  <- allow numeric indexing of dicts
// DictIndex[DictView, DictNode]  <- ordinary dict lookup


////////////////////
////    BASE    ////
////////////////////


const size_t MAX_SIZE_T = std::numeric_limits<size_t>::max();


template <typename ViewType, typename NodeType>
class ListOps {
private:
    ViewType<NodeType>* view;

    /*Get the direction to traverse a slice to minimize iterations and avoid
    backtracking.*/
    inline std::pair<size_t, size_t> get_slice_direction(
        size_t start,
        size_t stop,
        ssize_t step
    ) {
        size_t distance_from_head, distance_from_tail, index, end_index;

        if (step > 0) { // slice is ascending
            distance_from_head = start;
            distance_from_tail = view->size - stop;

            // traverse from nearest end
            if (distance_from_head <= distance_from_tail) {
                index = start;
                end_index = stop;
            } else {  // iterate over slice in reverse
                index = stop;
                end_index = start;
            }

        } else {  // slice is descending
            distance_from_head = stop;
            distance_from_tail = view->size - start;

            // traverse from nearest end
            if (distance_from_tail <= distance_from_head) {
                index = start;
                end_index = stop;
            } else {  // iterate over slice in reverse
                index = stop;
                end_index = start;
            }

        }

        return std::make_pair(index, end_index);
    }

    /*Get a node at a given index.*/
    NodeType* node_at_index(size_t index) {
        NodeType* curr;

        // iterate from nearest end
        if (index <= view->size / 2) {
            curr = view->head;
            for (size_t i = 0; i < index; i++) {
                curr = curr->next;
            }
        } else {
            curr = view->tail;
            for (size_t i = view->size - 1; i > index; i--) {
                curr = curr->prev;
            }
        }

        return curr;
    }

public:
    ListOps(ViewType<NodeType>* view) {
        this->view = view;
    }


    // TupleOps<T> takes in a ListView<T> in its constructor.  This


    // TODO: all of these should be implemented in the Cython wrappers
    // directly.  The ListView is just a stripped down core of the list, whose
    // public interface is completely defined by Cython.  This way, the
    // ListViews can be interchangeable, and the Cython wrappers can specialize
    // them as needed.

    // You could, for instance, create a SingleView around a list of
    // DoubleNodes


    // /*Append an item to the end of the list.*/
    // inline void append(PyObject* item) {
    //     link(tail, allocate(item), NULL);
    // }

    // /*Append an item to the beginning of the list.*/
    // inline void appendleft(PyObject* item) {
    //     link(NULL, allocate(item), head);
    // }

    // /*Extend the list with a sequence of items.*/
    // void extend(PyObject* iterable) {
    //     ListView<T>* staged = stage(iterable);
    //     if (staged == NULL) {
    //         return;  // raise exception
    //     }

    //     // trivial case: empty iterable
    //     if (staged->head == NULL) {
    //         return;
    //     }

    //     // link the staged nodes to the list
    //     T::link(tail, staged->head, staged->head->next);
    //     if (head == NULL) {
    //         head = staged->head;
    //     }
    //     tail = staged->tail;
    //     size += staged->size;
    // }

    // /*Extend the list to the left.*/
    // void extendleft(PyObject* iterable) {
    //     ListView<T>* staged = stage(iterable, true);  // reverse order
    //     if (staged == NULL) {
    //         return;  // raise exception
    //     }

    //     // trivial case: empty iterable
    //     if (staged->head == NULL) {
    //         return;
    //     }

    //     // link the staged nodes to the list
    //     if (head == NULL) {
    //         T::link(staged->tail, head, NULL);
    //     } else {
    //         T::link(staged->tail, head, head->next);
    //     }
    //     head = staged->head;
    //     if (tail == NULL) {
    //         tail = staged->tail;
    //     }
    //     size += staged->size;
    // }

    // // TODO: check if index bounds are correct.  Do they include the last item
    // // in the list?

    // /*Get the index of an item within the list.*/
    // size_t index(PyObject* item, long long start = 0, long long stop = -1) {
    //     T* curr = head;
    //     size_t i = 0;
    //     size_t norm_start = normalize_index(start);
    //     size_t norm_stop = normalize_index(stop);

    //     // skip to start index
    //     for (i; i < norm_start; i++) {
    //         if (curr == NULL) {
    //             throw std::out_of_range("list index out of range");
    //         }
    //         curr = curr->next;
    //     }

    //     int comp;

    //     // search until we hit stop index
    //     while (curr != NULL && i < norm_stop) {
    //         // C API equivalent of the == operator
    //         comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
    //         if (comp == -1) {  // comparison raised an exception
    //             return MAX_SIZE_T;
    //         } else if (comp == 1) {  // found a match
    //             return index;
    //         }
    
    //         // advance to next node
    //         curr = curr->next;
    //         i++;
    //     }

    //     // item not found
    //     PyObject* python_repr = PyObject_Repr(item);
    //     const char* c_repr = PyUnicode_AsUTF8(python_repr);
    //     Py_DECREF(python_repr);
    //     PyErr_Format(PyExc_ValueError, "%s is not in list", c_repr);
    //     return MAX_SIZE_T;
    // }

    // /*Count the number of occurrences of an item within the list.*/
    // size_t count(PyObject* item, long long start = 0, long long stop = -1) {
    //     T* curr = head;
    //     size_t i = 0;
    //     size_t observed = 0;
    //     size_t norm_start = normalize_index(start);
    //     size_t norm_stop = normalize_index(stop);

    //     // skip to start index
    //     for (i; i < norm_start; i++) {
    //         if (curr == NULL) {
    //             return observed;
    //         }
    //         curr = curr->next;
    //     }

    //     int comp;

    //     // search until we hit stop index
    //     while (curr != NULL && i < norm_stop) {
    //         // C API equivalent of the == operator
    //         comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
    //         if (comp == -1) {  // comparison raised an exception
    //             return MAX_SIZE_T;
    //         } else if (comp == 1) {  // found a match
    //             count++;
    //         }
    
    //         // advance to next node
    //         curr = curr->next;
    //         i++;
    //     }

    //     return observed;
    // }

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

    // /*Check if the list contains a given item.*/
    // inline int contains(PyObject* item) {
    //     T* curr = head;
    //     int comp;

    //     // search until we hit stop index
    //     while (curr != NULL) {
    //         // C API equivalent of the == operator
    //         comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
    //         if (comp == -1) {  // comparison raised an exception
    //             return -1;
    //         } else if (comp == 1) {  // found a match
    //             return 1;
    //         }
    
    //         // advance to next node
    //         curr = curr->next;
    //     }

    //     return 0;
    // }


    /*Insert an item at the given index.*/    
    void insert(PyObject* item, long long index) {
        size_t norm_index = normalize_index(index);
        NodeType* node = view->allocate(item);
        NodeType* curr

        // TODO: always insert before the current node?
        // -> this decision depends on sign of index

        // iterate from nearest end
        if (norm_index <= view->size / 2) {  // forward traversal
            curr = view->head;
            for (size_t i = 0; i < norm_index; i++) {
                curr = curr->next;
            }
            view->link(curr->prev, node, curr);  // link before current node
        } else {  // backward traversal
            curr = view->tail;
            for (size_t i = view->size - 1; i > norm_index; i--) {  // TODO: check loop bounds are correct
                curr = curr->prev;
            }
            view->link(curr, node, curr->next);  // link after current node
        }
    }

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

    /*Extract a slice from a linked list.*/
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

};



// TODO: SingleIndex, DoubleIndex, ImmutableIndex, SetIndex, DictIndex















#endif // INDEX_H include guard
