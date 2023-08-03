
// include guard prevents multiple inclusion
#ifndef SORT_H
#define SORT_H


#include <cstddef>  // for size_t
#include <queue>  // for std::queue
#include <Python.h>  // for CPython API
#include <node.h>  // for node definitions, views, etc.


///////////////////////
////    STRUCTS    ////
///////////////////////


template <typename T>
struct Keyed : public T {
    // NOTE: since we mask the node's original value with the precomputed key,
    // we can use the exact same algorithm as we would for an undecorated list.
    T* node;

    /* Constructor. */
    inline static Keyed<T>* allocate(T* node, PyObject* key) {
        // CPython API equivalent of `key(node.value)`
        PyObject* key_value = PyObject_CallFunctionObjArgs(key, node->value, NULL);
        if (key_value == NULL) {
            return NULL;  // propagate the error
        }

        // allocate a new decorator
        Keyed<T>* keyed = (Keyed<T>*)malloc(sizeof(Keyed<T>));
        if (keyed == NULL) {
            Py_DECREF(key_value);  // release reference on precomputed key
            PyErr_NoMemory();
            return NULL;  // propagate the error
        }

        // initialize the keyed decorator
        T::initialize(keyed, key_value);
        keyed->node = node;
        return keyed;
    }

    /* Destructor. */
    inline static void deallocate(Keyed<T>* keyed) {
        Py_DECREF(keyed->value);  // release reference on precomputed key
        free(keyed);
    }

    /* Overloaded destructor for use from ListView. */
    inline static void deallocate(
        std::queue<Keyed<T>*>& freelist,
        Keyed<T>* keyed
    ) {
        deallocate(keyed);
    }

};


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Sort a ListView in-place. */
template <typename NodeType>
void sort(ListView<NodeType>* view, PyObject* key = NULL, bool reverse = false) {
    // trivial case: empty list
    if (view->size == 0) {
        return;
    }

    // if no key function is given, sort the list in-place
    if (key == NULL) {
        merge_sort(view, reverse);
        return;
    }

    // decorate the list with precomputed keys
    ListView<Keyed<NodeType>>* key_view = decorate(view, key);
    if (key_view == NULL) {
        return;  // propagate the error
    }

    // sort the decorated list in-place
    std::pair<Keyed<NodeType>*, Keyed<NodeType>*> key_sorted;
    merge_sort(key_view, reverse);
    if (PyErr_Occurred()) {  // error during comparison
        delete key_view;  // free the decorated list
        return;  // propagate
    }

    // undecorate the list and update the view in-place
    std::pair<NodeType*, NodeType*> sorted = undecorate(key_view);
    view->head = sorted.first;
    view->tail = sorted.second;
}


/* Sort a SetView in-place. */
template <typename NodeType>
void sort(SetView<NodeType>* view, PyObject* key = NULL, bool reverse = false) {
    // cast SetView to ListView
    ListView<NodeType>* list_view = new ListView<NodeType>();
    if (list_view == NULL) {
        PyErr_NoMemory();
        return;
    }
    list_view->head = view->head;
    list_view->tail = view->tail;
    list_view->size = view->size;

    // sort the ListView
    sort(list_view, key, reverse);  // updates SetView in-place

    // free the ListView
    free(list_view);  // avoids calling destructor on nodes
}


/* Sort a DictView in-place. */
template <typename NodeType>
void sort(DictView<NodeType>* view, PyObject* key = NULL, bool reverse = false) {
    // cast DictView to ListView
    ListView<NodeType>* list_view = new ListView<NodeType>();
    if (list_view == NULL) {
        PyErr_NoMemory();
        return;
    }
    list_view->head = view->head;
    list_view->tail = view->tail;
    list_view->size = view->size;

    // sort the ListView
    sort(list_view, key, reverse);  // updates DictView in-place

    // free the ListView
    free(list_view);  // avoids calling destructor on nodes
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Decorate a linked list with the specified key function. */
template <typename NodeType>
ListView<Keyed<NodeType>>* decorate(ListView<NodeType>* view, PyObject* key) {
    // initialize an empty ListView to hold the decorated list
    ListView<Keyed<NodeType>>* decorated = new ListView<Keyed<NodeType>>();
    if (decorated == NULL) {
        PyErr_NoMemory();
        return NULL;
    }

    NodeType* node = view->head;
    Keyed<NodeType>* keyed;

    // iterate through the list and decorate each node with the precomputed key
    while (node != NULL) {
        // initialize a new keyed decorator
        keyed = Keyed<NodeType>::allocate(node, key);
        if (keyed == NULL) {
            delete decorated;  // free the decorated list
            return NULL;  // propagate the error
        }

        // link the decorator to the decorated list
        decorated->link(decorated->tail, keyed, NULL);

        // advance to the next node
        node = (NodeType*)node->next;
    }

    // return decorated list
    return decorated;
}


/* Rearrange a linked list to reflect the changes from a keyed sort operation. */
template <typename NodeType>
std::pair<NodeType*, NodeType*> undecorate(ListView<Keyed<NodeType>>* view) {
    // allocate a pair to hold the head and tail of the undecorated list
    std::pair<NodeType*, NodeType*> sorted = std::make_pair(nullptr, nullptr);
    Keyed<NodeType>* keyed = view->head;
    Keyed<NodeType>* next_keyed;
    NodeType* node = NULL;

    // NOTE: we rearrange the nodes in the undecorated list to match their
    // positions in the decorated equivalent.  This is done in-place, and we
    // free the decorators as we go in order to avoid a second iteration.
    while (keyed != NULL) {
        node = keyed->node;
        next_keyed = (Keyed<NodeType>*)keyed->next;

        // link the underlying node to the undecorated list
        if (sorted.first == NULL) {
            sorted.first = node;
        } else {
            NodeType::link(sorted.second, node, NULL);
        }
        sorted.second = node;  // set tail of undecorated list

        // advance to next node
        Keyed<NodeType>::deallocate(keyed);  // free the keyed decorator
        keyed = next_keyed;
    }

    // free the decorated list
    view->head = NULL;
    view->tail = NULL;
    delete view;

    // return head/tail of undecorated list
    return sorted;
}


/* Sort a linked list in-place using an iterative merge sort algorithm. */
template <typename NodeType>
void merge_sort(ListView<NodeType>* view, bool reverse) {
    if (DEBUG) {
        printf("    -> malloc: temp node\n");
    }

    // NOTE: we need a temporary node to act as the head of the merged sublists.
    // If we allocate it here, we can pass it to `merge()` as an argument and
    // reuse it for every sublist.  This avoids an extra malloc/free cycle in
    // each iteration.
    NodeType* temp = (NodeType*)malloc(sizeof(NodeType));
    if (temp == NULL) {
        PyErr_NoMemory();
        return;
    }

    // NOTE: we use a series of pairs to keep track of the head and tail of
    // each sublist used in the sort algorithm.  `unsorted` keeps track of the
    // nodes that still need to be processed, while `sorted` does the same for
    // those that have already been sorted.  The `left`, `right`, and `merged`
    // pairs are used to keep track of the sublists that are used in each
    // iteration of the merge step.
    std::pair<NodeType*, NodeType*> unsorted = std::make_pair(view->head, view->tail);
    std::pair<NodeType*, NodeType*> sorted = std::make_pair(nullptr, nullptr);
    std::pair<NodeType*, NodeType*> left = std::make_pair(nullptr, nullptr);
    std::pair<NodeType*, NodeType*> right = std::make_pair(nullptr, nullptr);
    std::pair<NodeType*, NodeType*> merged;

    // NOTE: as a refresher, the general merge sort algorithm is as follows:
    //  1) divide the list into sublists of length 1 (bottom-up)
    //  2) merge adjacent sublists into sorted sublists with twice the length
    //  3) repeat step 2 until the entire list is sorted
    size_t length = 1;  // length of sublists for current iteration
    while (length <= view->size) {
        // reset head and tail of sorted list
        sorted.first = NULL;
        sorted.second = NULL;

        // divide and conquer
        while (unsorted.first != NULL) {
            // split the list into two sublists of size `length`
            left.first = unsorted.first;
            left.second = walk(left.first, length - 1);
            right.first = (NodeType*)left.second->next;  // may be NULL
            right.second = walk(right.first, length - 1);
            if (right.second == NULL) {
                unsorted.first = NULL;
            } else {
                unsorted.first = (NodeType*)right.second->next;
            }

            // unlink the sublists from the original list
            NodeType::split(sorted.second, left.first);  // sorted <-x-> left
            NodeType::split(left.second, right.first);  // left <-x-> right
            NodeType::split(right.second, unsorted.first);  // right <-x-> unsorted

            // merge the left and right sublists in sorted order
            merged = merge(left, right, temp, reverse);
            if (PyErr_Occurred()) {  // error during `<` comparison
                // undo the splits to recover a coherent list
                merged = recover(sorted, left, right, unsorted);
                view->head = merged.first;
                view->tail = merged.second;
                if (DEBUG) {
                    printf("    -> free: temp node\n");
                }
                free(temp);  // clean up temporary node
                return;  // propagate the error
            }

            // link merged sublist to sorted and update head and tail accordingly
            if (sorted.first == NULL) {
                sorted.first = merged.first;
            } else {  // link the merged sublist to the previous one
                NodeType::join(sorted.second, merged.first);
            }
            sorted.second = merged.second;
        }

        // prep for next iteration
        unsorted.first = sorted.first;  // record head of partially-sorted list
        unsorted.second = sorted.second;  // record tail of partially-sorted list
        length *= 2;  // double the length of each sublist
    }

    // clean up temporary node
    if (DEBUG) {
        printf("    -> free: temp node\n");
    }
    free(temp);

    // update view parameters in-place
    view->head = sorted.first;
    view->tail = sorted.second;
}


/* Walk along a linked list by the specified number of nodes. */
template <typename NodeType>
inline NodeType* walk(NodeType* curr, size_t length) {
    // if we're at the end of the list, there's nothing left to traverse
    if (curr == NULL) {
        return NULL;
    }

    // walk forward `length` nodes from `curr`
    for (size_t i = 0; i < length; i++) {
        if (curr->next == NULL) {  // list terminates before `length`
            break;
        }
        curr = (NodeType*)curr->next;
    }
    return curr;
}


/* Merge two sublists in sorted order. */
template <typename NodeType>
std::pair<NodeType*, NodeType*> merge(
    std::pair<NodeType*, NodeType*> left,
    std::pair<NodeType*, NodeType*> right,
    NodeType* temp,
    bool reverse
) {
    NodeType* curr = temp;  // temporary head of merged list
    int comp;

    // NOTE: the way we merge sublists is by comparing the head of each sublist
    // and appending the smaller of the two to the merged result.  We repeat
    // this process until one of the sublists has been exhausted, giving us a
    // sorted list of size `length * 2`.
    while (left.first != NULL && right.first != NULL) {
        // CPython API equivalent of `left.value < right.value`
        comp = PyObject_RichCompareBool(left.first->value, right.first->value, Py_LT);
        if (comp == -1) {
            return std::make_pair(nullptr, nullptr);  // propagate the error
        }

        // append the smaller of the two candidates to the merged list
        if (comp ^ reverse) {  // [not] left < right
            NodeType::join(curr, left.first);  // append left candidate to merged
            left.first = (NodeType*)left.first->next;  // advance left
        } else {
            NodeType::join(curr, right.first);  // append right candidate to merged
            right.first = (NodeType*)right.first->next;  // advance right
        }

        // advance merged tail to accepted candidate
        curr = (NodeType*)curr->next;
    }

    // NOTE: at this point, one of the sublists has been exhausted, so we can
    // safely append the remaining nodes to the merged result and update its
    // tail accordingly.
    NodeType* tail;
    if (left.first != NULL) {
        NodeType::join(curr, left.first);
        tail = left.second;
    } else {
        NodeType::join(curr, right.first);
        tail = right.second;
    }

    // unlink temporary head from list and return the proper head and tail of
    // the merged list
    curr = (NodeType*)temp->next;
    NodeType::split(temp, curr);  // `temp` can be reused in the next iteration
    return std::make_pair(curr, tail);
}


/* Undo the split step in merge_sort() to recover a coherent list in case of error. */
template <typename NodeType>
std::pair<NodeType*, NodeType*> recover(
    std::pair<NodeType*, NodeType*> sorted,
    std::pair<NodeType*, NodeType*> left,
    std::pair<NodeType*, NodeType*> right,
    std::pair<NodeType*, NodeType*> unsorted
) {
    // link each component into a single list
    NodeType::join(sorted.second, left.first);  // sorted tail <-> left head
    NodeType::join(left.second, right.first);  // left tail <-> right head
    NodeType::join(right.second, unsorted.first);  // right tail <-> unsorted head

    // return the proper head and tail of the recovered list
    return std::make_pair(sorted.first, unsorted.second);
}


#endif // SORT_H include guard
