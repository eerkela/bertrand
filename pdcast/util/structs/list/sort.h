
// include guard prevents multiple inclusion
#ifndef SORT_H
#define SORT_H

#include <cstddef>  // for size_t
#include <queue>  // for std::queue
#include <Python.h>  // for CPython API
#include <node.h>  // for nodes
#include <view.h>  // for views


//////////////////////
////    PUBLIC    ////
//////////////////////


// NOTE: this algorithm works applies equally to singly- and doubly-linked
// lists, so we can use the same implementation for both.  It also works for
// sets and dictionaries, since we can just generate a temporary list view
// into the set or dictionary and sort that instead.  This also allows our
// `Keyed<>` nodes to be as light as possible, minimizing overhead.


/* Sort a generic view in-place. */
template <template <typename> class ViewType, typename NodeType>
void sort(ViewType<NodeType>* view, PyObject* key_func, bool reverse) {
    using Node = typename ViewType<NodeType>::Node;

    // create a temporary ListView into the given view
    ListView<Node>* list_view;
    try {
        list_view = new ListView<Node>();
    } catch (std::bad_alloc& ba) {
        PyErr_NoMemory();
        return;
    }
    list_view->head = view->head;
    list_view->tail = view->tail;
    list_view->size = view->size;

    // sort the viewed list
    sort(list_view, key_func, reverse);  // updates the original view in-place

    // free the temporary ListView
    list_view->head = NULL;  // avoids calling destructor on nodes
    list_view->tail = NULL;
    list_view->size = 0;
    delete list_view;
}


/* Sort a ListView in-place. */
template <typename NodeType>
void sort(ListView<NodeType>* view, PyObject* key_func, bool reverse) {
    using Node = typename ListView<NodeType>::Node;

    // trivial case: empty list
    if (view->size == 0) {
        return;
    }

    // if no key function is given, sort the list in-place
    if (key_func == NULL) {
        _merge_sort(view, reverse);
        return;
    }

    // decorate the list with precomputed keys
    ListView<Keyed<Node>>* key_view = _decorate(view, key_func);
    if (key_view == NULL) {
        return;  // propagate
    }

    // sort the decorated list in-place
    _merge_sort(key_view, reverse);
    if (PyErr_Occurred()) {  // error during `<` comparison
        delete key_view;  // free the decorated list
        return;  // propagate without modifying the original list
    }

    // rearrange the list to reflect the changes from the sort operation
    std::pair<Node*, Node*> sorted = _undecorate(key_view);  // frees key_view

    // update view parameters to match the sorted list
    view->head = sorted.first;
    view->tail = sorted.second;
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Decorate a linked list with the specified key function. */
template <typename Node>
ListView<Keyed<Node>>* _decorate(ListView<Node>* view, PyObject* key_func) {
    // initialize an empty ListView to hold the decorated list
    ListView<Keyed<Node>>* decorated = new ListView<Keyed<Node>>();
    if (decorated == NULL) {
        PyErr_NoMemory();
        return NULL;
    }

    // iterate through the list and decorate each node with the precomputed key
    Node* node = view->head;
    while (node != NULL) {
        // CPython API equivalent of `key_func(node.value)`
        PyObject* key_value;
        key_value = PyObject_CallFunctionObjArgs(key_func, node->value, NULL);
        if (key_value == NULL) {  // error during key_func()
            delete decorated;  // free the decorated list
            return NULL;
        }

        // initialize a new keyed decorator
        Keyed<Node>* keyed = decorated->node(key_value, node);
        if (keyed == NULL) {
            Py_DECREF(key_value);  // release the key value
            delete decorated;  // free the decorated list
            return NULL;  // propagate the error
        }

        // link the decorator to the decorated list
        decorated->link(decorated->tail, keyed, NULL);

        // advance to the next node
        node = (Node*)node->next;
    }

    // return decorated list
    return decorated;
}


/* Rearrange a linked list to reflect the changes from a keyed sort operation. */
template <typename Node>
std::pair<Node*, Node*> _undecorate(ListView<Keyed<Node>>* view) {
    // allocate a pair to hold the head and tail of the undecorated list
    std::pair<Node*, Node*> sorted = std::make_pair(nullptr, nullptr);

    // NOTE: we rearrange the nodes in the undecorated list to match their
    // positions in the decorated equivalent.  This is done in-place, and we
    // free the decorators as we go in order to avoid a second iteration.
    Keyed<Node>* keyed_prev = NULL;
    Keyed<Node>* keyed = view->head;
    while (keyed != NULL) {
        Node* wrapped = keyed->node;
        Keyed<Node>* keyed_next = (Keyed<Node>*)keyed->next;

        // link the wrapped node to the undecorated list
        if (sorted.first == NULL) {
            sorted.first = wrapped;
        } else {
            Node::link(sorted.second, wrapped, NULL);
        }
        sorted.second = wrapped;  // set tail of undecorated list

        // advance to next node
        view->unlink(keyed_prev, keyed, keyed_next);
        view->recycle(keyed);
        keyed = keyed_next;
    }

    // nodes have already been deleted, we're just freeing the view structure
    // itself.
    delete view;

    // return head/tail of undecorated list
    return sorted;
}


/* Sort a linked list in-place using an iterative merge sort algorithm. */
template <typename Node>
void _merge_sort(ListView<Node>* view, bool reverse) {
    if (DEBUG) {
        printf("    -> malloc: temp node\n");
    }

    // NOTE: we need a temporary node to act as the head of the merged sublists.
    // If we allocate it here, we can pass it to `_merge()` as an argument and
    // reuse it for every sublist.  This avoids an extra malloc/free cycle in
    // each iteration.
    Node* temp = (Node*)malloc(sizeof(Node));
    if (temp == NULL) {
        PyErr_NoMemory();
        return;
    }

    // NOTE: we use a series of pairs to keep track of the head and tail of
    // each sublist used in the sort algorithm.  `unsorted` keeps track of the
    // nodes that still need to be processed, while `sorted` does the same for
    // those that have already been sorted.  The `left`, `right`, and `merged`
    // pairs are used to keep track of the sublists that are used in each
    // iteration of the merge loop.
    std::pair<Node*, Node*> unsorted = std::make_pair(view->head, view->tail);
    std::pair<Node*, Node*> sorted = std::make_pair(nullptr, nullptr);
    std::pair<Node*, Node*> left = std::make_pair(nullptr, nullptr);
    std::pair<Node*, Node*> right = std::make_pair(nullptr, nullptr);
    std::pair<Node*, Node*> merged;

    // NOTE: as a refresher, the general merge sort algorithm is as follows:
    //  1) divide the list into sublists of length 1 (bottom-up)
    //  2) merge adjacent sublists into sorted mixtures with twice the length
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
            left.second = _walk(left.first, length - 1);
            right.first = (Node*)left.second->next;  // may be NULL
            right.second = _walk(right.first, length - 1);
            if (right.second == NULL) {  // right sublist is empty
                unsorted.first = NULL;  // terminate the loop
            } else {
                unsorted.first = (Node*)right.second->next;
            }

            // unlink the sublists from the original list
            Node::split(sorted.second, left.first);  // sorted <-/-> left
            Node::split(left.second, right.first);  // left <-/-> right
            Node::split(right.second, unsorted.first);  // right <-/-> unsorted

            // merge the left and right sublists in sorted order
            merged = _merge(left, right, temp, reverse);
            if (PyErr_Occurred()) {  // error during `<` comparison
                // undo the splits to recover a coherent list
                merged = _recover(sorted, left, right, unsorted);
                view->head = merged.first;  // view is partially sorted, but free()able
                view->tail = merged.second;
                if (DEBUG) {
                    printf("    -> free: temp node\n");
                }
                free(temp);  // clean up temporary node
                return;  // propagate the error
            }

            // link combined sublist to sorted
            if (sorted.first == NULL) {
                sorted.first = merged.first;
            } else {  // link the merged sublist to the previous one
                Node::join(sorted.second, merged.first);
            }
            sorted.second = merged.second;  // update tail of sorted list
        }

        // partially-sorted list becomes new unsorted list for next iteration
        unsorted.first = sorted.first;
        unsorted.second = sorted.second;
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
template <typename Node>
inline Node* _walk(Node* curr, size_t length) {
    // if we're at the end of the list, there's nothing left to traverse
    if (curr == NULL) {
        return NULL;
    }

    // walk forward `length` nodes from `curr`
    for (size_t i = 0; i < length; i++) {
        if (curr->next == NULL) {  // list terminates before `length`
            break;
        }
        curr = (Node*)curr->next;
    }
    return curr;
}


/* Merge two sublists in sorted order. */
template <typename Node>
std::pair<Node*, Node*> _merge(
    std::pair<Node*, Node*> left,
    std::pair<Node*, Node*> right,
    Node* temp,
    bool reverse
) {
    Node* curr = temp;  // temporary head of merged list

    // NOTE: the way we merge sublists is by comparing the head of each sublist
    // and appending the smaller of the two to the merged result.  We repeat
    // this process until one of the sublists has been exhausted, giving us a
    // sorted list of size `length * 2`.
    while (left.first != NULL && right.first != NULL) {
        // CPython API equivalent of `left.value < right.value`
        int comp = PyObject_RichCompareBool(left.first->value, right.first->value, Py_LT);
        if (comp == -1) {
            return std::make_pair(nullptr, nullptr);  // propagate the error
        }

        // append the smaller of the two candidates to the merged list
        if (comp ^ reverse) {  // [not] left < right
            Node::join(curr, left.first);  // push from left sublist
            left.first = (Node*)left.first->next;  // advance left
        } else {
            Node::join(curr, right.first);  // push from right sublist
            right.first = (Node*)right.first->next;  // advance right
        }

        // advance to next comparison
        curr = (Node*)curr->next;
    }

    // NOTE: at this point, one of the sublists has been exhausted, so we can
    // safely append the remaining nodes to the merged result.
    Node* tail;
    if (left.first != NULL) {
        Node::join(curr, left.first);  // link remaining nodes
        tail = left.second;  // update tail of merged list
    } else {
        Node::join(curr, right.first);  // link remaining nodes
        tail = right.second;  // update tail of merged list
    }

    // unlink temporary head from list and return the proper head and tail
    curr = (Node*)temp->next;
    Node::split(temp, curr);  // `temp` can be reused
    return std::make_pair(curr, tail);
}


/* Undo the split step in _merge_sort() to recover a coherent list in case of error. */
template <typename Node>
std::pair<Node*, Node*> _recover(
    std::pair<Node*, Node*> sorted,
    std::pair<Node*, Node*> left,
    std::pair<Node*, Node*> right,
    std::pair<Node*, Node*> unsorted
) {
    // link each sublist into a single, partially-sorted list
    Node::join(sorted.second, left.first);  // sorted tail <-> left head
    Node::join(left.second, right.first);  // left tail <-> right head
    Node::join(right.second, unsorted.first);  // right tail <-> unsorted head

    // return the head and tail of the recovered list
    return std::make_pair(sorted.first, unsorted.second);
}


////////////////////////
////    WRAPPERS    ////
////////////////////////


// NOTE: Cython doesn't play well with nested templates, so we need to
// explicitly instantiate specializations for each combination of node/view
// type.  This is a bit of a pain, put it's the only way to get Cython to
// properly recognize the functions.

// Maybe in a future release we won't have to do this:


template void sort(ListView<SingleNode>* view, PyObject* key_func, bool reverse);
template void sort(SetView<SingleNode>* view, PyObject* key_func, bool reverse);
template void sort(DictView<SingleNode>* view, PyObject* key_func, bool reverse);
template void sort(ListView<DoubleNode>* view, PyObject* key_func, bool reverse);
template void sort(SetView<DoubleNode>* view, PyObject* key_func, bool reverse);
template void sort(DictView<DoubleNode>* view, PyObject* key_func, bool reverse);


#endif // SORT_H include guard
