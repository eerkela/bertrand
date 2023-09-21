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


//////////////////////
////    PUBLIC    ////
//////////////////////


/* A wrapper around a SortPolicy that handles casting to ListView and
decorating/undecorating according to key functions.  All the SortPolicy has to
implement is the actual sorting algorithm itself. */
template <typename SortPolicy>
class SortFunc {
private:

    /* Apply a key function to a list, decorating it with the computed result. */
    template <typename Node, template <typename> class Allocator>
    static ListView<Keyed<Node>, FixedAllocator> decorate(
        ListView<Node, Allocator>& view,
        PyObject* key
    ) {
        // initialize an empty ListView to hold the decorated list
        ListView<Keyed<Node>, FixedAllocator> decorated(view.size, nullptr);

        // decorate each node in list with precomputed key
        for (Node* node : view) {
            PyObject* key_val = PyObject_CallFunctionObjArgs(key, node->value, nullptr);
            if (key_val == nullptr) {
                throw std::runtime_error("error during key function call");
            }

            // initialize a new keyed decorator
            Keyed<Node>* keyed = decorated.node(key_val, node);
            if (keyed == nullptr) {
                Py_DECREF(key_val);
                throw std::runtime_error("error during decorator allocation");
            }

            // link the keyed node to the decorated list
            decorated.link(decorated.tail, keyed, nullptr);
        }

        return decorated;
    }

    /* Rearrange the underlying list in-place to reflect changes from a keyed sort. */
    template <typename Node, template <typename> class Allocator>
    static void undecorate(
        ListView<Keyed<Node>, FixedAllocator>& decorated,
        ListView<Node, Allocator>& view
    ) {
        Node* new_head = nullptr;
        Node* new_tail = nullptr;

        // NOTE: we recycle the decorators as we go in order to avoid a second loop
        for (auto iter = decorated.iter(); iter != iter.end(); ++iter) {
            Node* unwrapped = (*iter)->node;

            // link the wrapped node to the undecorated list
            if (new_head == nullptr) {
                new_head = unwrapped;
            } else {
                Node::link(new_tail, unwrapped, nullptr);  // low-level link()
            }
            new_tail = unwrapped;

            // remove and recycle the decorator
            decorated.recycle(iter.remove());
        }

        // update head/tail of sorted list
        view.head = new_head;
        view.tail = new_tail;
    }

    /* Execute the sorting algorithm. */
    template <typename Node, template <typename> class Allocator>
    static void execute(ListView<Node, Allocator>& view, PyObject* key, bool reverse) {
        // if no key function is given, sort the list in-place
        if (key == nullptr) {
            SortPolicy::sort(view, reverse);
            return;
        }

        // apply key function to each node in list
        ListView<Keyed<Node>, FixedAllocator> decorated = decorate(view, key);

        // sort decorated list
        SortPolicy::sort(decorated, reverse);
        if (PyErr_Occurred()) {
            return;  // propagate without modifying original list
        }

        // rearrange the original list to reflect the sorted order
        undecorate(decorated, view);
    }

public:

    /* Invoke the functor, decorating and sorting the view in-place. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename Node,
        template <typename> class Allocator
    >
    static void sort(ViewType<Node, Allocator>& view, PyObject* key, bool reverse) {
        using View = ViewType<Node, Allocator>;
        using List = ListView<Node, Allocator>;

        // trivial case: empty view
        if (view.size == 0) {
            return;
        }

        // if the view is already a ListView, then we can sort it directly
        if constexpr (std::is_same_v<View, List>) {
            execute(view, key, reverse);
            return;
        }

        // otherwise, we create a temporary ListView into the view and sort that
        // instead.
        List list_view(view.max_size);
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

};


////////////////////////
////    POLICIES    ////
////////////////////////


// TODO: allocate temporary node on the view itself.


/* An iterative merge sort algorithm with error recovery. */
class MergeSort {
protected:

    /* Walk along the list by the specified number of nodes. */
    template <typename Node>
    inline static Node* walk(Node* curr, size_t length) {
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
    template <typename Node>
    static std::pair<Node*, Node*> merge(
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
            int comp = PyObject_RichCompareBool(
                left.first->value, right.first->value, Py_LT
            );
            if (comp == -1) {
                throw std::runtime_error("error during `<` comparison");
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
        return std::make_pair(curr, tail);
    }

    /* Undo the split() step to recover a valid list in case of an error. */
    template <typename Node>
    inline static std::pair<Node*, Node*> recover(
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

    /* Sort a linked list in-place using an iterative merge sort algorithm. */
    template <typename Node, template <typename> class Allocator>
    static void sort(ListView<Node, Allocator>& view, bool reverse) {
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
                try {
                    merged = merge(left, right, temp, reverse);
                } catch (...) {
                    // undo the splits to recover a coherent list
                    merged = recover(sorted, left, right, unsorted);
                    view.head = merged.first;  // view is partially sorted, but valid
                    view.tail = merged.second;
                    if constexpr (DEBUG) {
                        printf("    -> free: temp node\n");
                    }
                    free(temp);  // clean up temporary node
                    throw;  // propagate the error
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

};


#endif  // BERTRAND_STRUCTS_CORE_SORT_H
