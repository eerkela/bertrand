// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_SORT_H
#define BERTRAND_STRUCTS_CORE_SORT_H

#include <cstddef>  // size_t
#include <queue>  // std::queue
#include <optional>  // std::optional
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/node.h"  // Keyed<>
#include "../core/view.h"  // views


////////////////////
////    BASE    ////
////////////////////


/* Base class for all sort() functors. */
template <typename ViewType>
class SortPolicy {
protected:
    using View = ViewType;
    using Node = typename View::Node;

    template <typename T>
    using AllocatorType = typename View::template AllocatorType<T>;

    View& view;

    /* Apply a key function to a list, decorating it with the computed result. */
    std::optional<ListView<Keyed<Node>, FixedAllocator>> decorate(PyObject* key) {
        // initialize an empty ListView to hold the decorated list
        ListView<Keyed<Node>, FixedAllocator> decorated(view.size);  // preallocated

        // decorate each node in list with precomputed key
        for (Node* node : view) {
            PyObject* key_val = PyObject_CallFunctionObjArgs(key, node->value, nullptr);
            if (key_val == nullptr) {
                return std::nullopt;
            }

            // initialize a new keyed decorator
            Keyed<Node>* keyed = decorated.node(key_val, node);
            if (keyed == nullptr) {
                Py_DECREF(key_val);
                return std::nullopt;
            }

            // link the keyed node to the decorated list
            decorated.link(decorated.tail, keyed, nullptr);
        }

        return std::make_optional(std::move(decorated));
    }

    /* Rearrange the underlying list in-place to reflect changes from a keyed sort. */
    void undecorate(ListView<Keyed<Node>, AllocatorType>& decorated) {
        Node* new_head = nullptr;
        Node* new_tail = nullptr;

        // NOTE: we recycle the decorators as we go in order to avoid a second loop
        for (auto iter = decorated.iter(); iter != iter.end(); ++iter) {
            Node* wrapped = (*iter)->node;

            // link the wrapped node to the undecorated list
            if (new_head == nullptr) {
                new_head = wrapped;
            } else {
                Node::link(new_tail, wrapped, nullptr);  // low-level link()
            }
            new_tail = wrapped;

            // remove and recycle the decorator
            decorated.recycle(iter.remove());
        }

        // update head/tail of sorted list
        view.head = new_head;
        view.tail = new_tail;
    }

public:

    SortPolicy(View& view) : view(view) {}

    /* Invoke the functor, sorting the underlying view in place. */
    void operator()(PyObject* key = nullptr, bool reverse = false) {
        // trivial case: empty view
        if (view.size == 0) {
            return;
        }

        // if the view is a subclass of ListView, we can sort it directly
        if constexpr (std::is_same_v<View, ListView<Node, AllocatorType>>) {
            execute(view, key, reverse);
            return;
        }

        // otherwise, we create a temporary ListView into the view and sort that
        // instead.
        ListView<Node, AllocatorType> list_view(view.max_size);
        list_view.head = view.head;
        list_view.tail = view.tail;
        list_view.size = view.size;

        // sort the viewed list in place
        execute(list_view, key, reverse);

        // free the temporary ListView
        list_view.head = nullptr;  // avoids calling destructor on nodes
        list_view.tail = nullptr;
        list_view.size = 0;
    }

    /* Execute the sorting algorithm for a particular view.  All SortPolicies must
    implement this method. */
    void execute(ListView<Node, AllocatorType>& view, PyObject* key, bool reverse);

};


////////////////////////
////    POLICIES    ////
////////////////////////


/* An iterative merge sort algorithm with error recovery. */
template <typename ViewType>
class MergeSort : public SortPolicy<ViewType> {
protected:
    using Base = SortPolicy<ViewType>;
    using View = typename Base::View;
    using Node = typename Base::Node;

    template <typename T>
    using AllocatorType = typename Base::template AllocatorType<T>;

    /* Sort a linked list in-place using an iterative merge sort algorithm. */
    void merge_sort(ListView<Node, AllocatorType>& view, bool reverse) {
        // NOTE: we need a temporary node to act as the head of the merged sublists.
        // If we allocate it here, we can pass it to `merge()` as an argument and
        // reuse it for every sublist.  This avoids an extra malloc/free cycle in
        // each iteration.
        if constexpr (DEBUG) {
            printf("    -> malloc: temp node\n");
        }
        Node* temp = static_cast<Node*>(malloc(sizeof(Node)));
        if (temp == nullptr) {
            PyErr_NoMemory();
            return;
        }

        // NOTE: we use a series of pairs to keep track of the head and tail of
        // each sublist used in the sort algorithm.  `unsorted` keeps track of the
        // nodes that still need to be processed, while `sorted` does the same for
        // those that have already been sorted.  The `left`, `right`, and `merged`
        // pairs are used to keep track of the sublists that are used in each
        // iteration of the merge loop.
        std::pair<Node*, Node*> unsorted = std::make_pair(view.head, view.tail);
        std::pair<Node*, Node*> sorted = std::make_pair(nullptr, nullptr);
        std::pair<Node*, Node*> left = std::make_pair(nullptr, nullptr);
        std::pair<Node*, Node*> right = std::make_pair(nullptr, nullptr);
        std::pair<Node*, Node*> merged;

        // NOTE: as a refresher, the general merge sort algorithm is as follows:
        //  1) divide the list into sublists of length 1 (bottom-up)
        //  2) merge adjacent sublists into sorted mixtures with twice the length
        //  3) repeat step 2 until the entire list is sorted
        size_t length = 1;  // length of sublists for current iteration
        while (length <= view.size) {
            // reset head and tail of sorted list
            sorted.first = nullptr;
            sorted.second = nullptr;

            // divide and conquer
            while (unsorted.first != nullptr) {
                // split the list into two sublists of size `length`
                left.first = unsorted.first;
                left.second = walk(left.first, length - 1);
                right.first = static_cast<Node*>(left.second->next);  // may be NULL
                right.second = walk(right.first, length - 1);
                if (right.second == nullptr) {  // right sublist is empty
                    unsorted.first = nullptr;  // terminate the loop
                } else {
                    unsorted.first = static_cast<Node*>(right.second->next);
                }

                // unlink the sublists from the original list
                Node::split(sorted.second, left.first);  // sorted <-/-> left
                Node::split(left.second, right.first);  // left <-/-> right
                Node::split(right.second, unsorted.first);  // right <-/-> unsorted

                // merge the left and right sublists in sorted order
                merged = merge(left, right, temp, reverse);
                if (!merged.has_value()) {  // error during `<` comparison
                    // undo the splits to recover a coherent list
                    std::pair<Node*, Node*> r = recover(sorted, left, right, unsorted);
                    view.head = r.first;  // view is partially sorted, but valid
                    view.tail = r.second;
                    if constexpr (DEBUG) {
                        printf("    -> free: temp node\n");
                    }
                    free(temp);  // clean up temporary node
                    return;  // propagate the error
                }

                // link merged sublist to sorted
                if (sorted.first == nullptr) {
                    sorted.first = merged.first;
                } else {
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
        if constexpr (DEBUG) {
            printf("    -> free: temp node\n");
        }
        free(temp);

        // update view parameters in-place
        view.head = sorted.first;
        view.tail = sorted.second;
    }

    /* Walk along the list by the specified number of nodes. */
    inline Node* walk(Node* curr, size_t length) {
        // if we're at the end of the list, there's nothing left to traverse
        if (curr == nullptr) {
            return nullptr;
        }

        // walk forward `length` nodes from `curr`
        for (size_t i = 0; i < length; i++) {
            if (curr->next == nullptr) {  // list terminates before `length`
                break;
            }
            curr = static_cast<Node*>(curr->next);
        }
        return curr;
    }

    /* Merge two sublists in sorted order. */
    std::optional<std::pair<Node*, Node*>> merge(
        std::pair<Node*, Node*> left,
        std::pair<Node*, Node*> right,
        Node* temp,
        bool reverse
    ) {
        Node* curr = temp;  // temporary head of merged list

        // NOTE: the way we merge sublists is by comparing the head of each sublist
        // and appending the smaller of the two elements to the merged result.  We
        // repeat this process until one of the sublists has been exhausted, giving us
        // a sorted list of size `length * 2`.
        while (left.first != nullptr && right.first != nullptr) {
            int comp = PyObject_RichCompareBool(left.first->value, right.first->value, Py_LT);
            if (comp == -1) {
                return std::nullopt;
            }

            // append the smaller of the two candidates to the merged list
            if (comp ^ reverse) {  // [not] left < right
                Node::join(curr, left.first);
                left.first = static_cast<Node*>(left.first->next);
            } else {
                Node::join(curr, right.first);
                right.first = static_cast<Node*>(right.first->next);
            }
            curr = static_cast<Node*>(curr->next);
        }

        // NOTE: at this point, one of the sublists has been exhausted, so we can
        // safely append the remaining nodes to the merged result.
        Node* tail;
        if (left.first != nullptr) {
            Node::join(curr, left.first);
            tail = left.second;
        } else {
            Node::join(curr, right.first);
            tail = right.second;
        }

        // unlink temporary head from list and return the proper head and tail
        curr = static_cast<Node*>(temp->next);
        Node::split(temp, curr);  // `temp` can be reused
        return std::make_optional(std::make_pair(curr, tail));
    }

    /* Undo the split() step to recover a valid list in case of an error. */
    std::pair<Node*, Node*> recover(
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

public:

    /* Inherit constructors from base class */
    using SortPolicy<ViewType>::SortPolicy;

    /* Execute the sorting algorithm. */
    void execute(ListView<Node, AllocatorType>& view, PyObject* key, bool reverse) {
        // if no key function is given, sort the list in-place
        if (key == nullptr) {
            merge_sort(view, reverse);
            return;
        }

        // otherwise, decorate the list with precomputed keys and sort that instead
        using Decorated = ListView<Keyed<Node>, FixedAllocator>;
        std::optional<Decorated> decorated = this->decorate(key);
        if (!decorated.has_value()) {
            return;  // propagate
        }

        merge_sort(decorated.value(), reverse);
        if (PyErr_Occurred()) {  // error during `<` comparison
            return;  // propagate without modifying the original list
        }

        this->undecorate(decorated.value());
    }

};


#endif  // BERTRAND_STRUCTS_CORE_SORT_H
