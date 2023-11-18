// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_SORT_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_SORT_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_SORT_H

#include <cstddef>  // size_t
#include <utility>  // std::pair
#include "../../util/python.h"  // lt()
#include "../core/node.h"  // Keyed<>
#include "../core/view.h"  // ListView, ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Sort a linked list, set, or dictionary in-place. */
template <typename SortPolicy, typename View, typename Func>
auto sort(View& view, Func key, bool reverse)
    -> std::enable_if_t<ViewTraits<View>::listlike, void>
{
    using Node = typename View::Node;
    using KeyNode = Keyed<Node, Func>;

    // trivial case: empty view
    if (view.size() == 0) {
        return;
    }

    // if no key function is given, sort the view directly
    if (key == nullptr) {
        SortPolicy::execute(view, reverse);
        return;
    }

    // otherwise, decorate each node with the computed key function
    ListView<KeyNode> decorated(view.size(), false, nullptr);  // exact size
    for (auto it = view.begin(), end = view.end(); it != end; ++it) {
        KeyNode* node = decorated.node(it.curr(), key);
        decorated.link(decorated.tail(), node, nullptr);
    }

    // sort the decorated view
    SortPolicy::execute(decorated, reverse);

    // undecorate and reflect changes in original view
    Node* new_head = nullptr;
    Node* new_tail = nullptr;
    for (auto it = decorated.begin(), end = decorated.end(); it != end; ) {
        Node* unwrapped = it.curr()->node();

        // link to sorted view
        if (new_head == nullptr) {
            new_head = unwrapped;
        } else {
            Node::link(new_tail, unwrapped, nullptr);
        }
        new_tail = unwrapped;

        // NOTE: we recycle the decorated nodes as we go to avoid a second loop
        decorated.recycle(it.drop());  // implicitly advances iterator
    }

    // update head/tail of sorted view
    view.head(new_head);
    view.tail(new_tail);
}


////////////////////////
////    POLICIES    ////
////////////////////////


/* An iterative merge sort algorithm with error recovery. */
class MergeSort {
protected:

    /* Walk along the list by the specified number of nodes. */
    template <typename Node>
    inline static Node* walk(Node* curr, size_t length) {
        if (curr == nullptr) return nullptr;  // nothing left to traverse
        for (size_t i = 0; i < length; i++) {
            if (curr->next() == nullptr) break;  // reached end of list
            curr = curr->next();
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

        // NOTE: we merge sublists by comparing the head of each sublist and appending
        // the smaller of the two elements to the merged result, repeating until one of
        // the sublists has been exhausted, giving a sorted list of size `length * 2`.
        while (left.first != nullptr && right.first != nullptr) {
            bool comp = util::lt(left.first->value(), right.first->value());

            // append smaller of two candidates to the merged list
            if (reverse ^ comp) {  // [not] left < right
                Node::join(curr, left.first);
                left.first = left.first->next();
            } else {
                Node::join(curr, right.first);
                right.first = right.first->next();
            }
            curr = curr->next();
        }

        // NOTE: at this point, one of the sublists has been exhausted, so we can
        // safely append the remaining nodes.
        Node* tail;
        if (left.first != nullptr) {
            Node::join(curr, left.first);
            tail = left.second;
        } else {
            Node::join(curr, right.first);
            tail = right.second;
        }

        // unlink temporary node from final list and return proper head and tail
        curr = temp->next();
        Node::split(temp, curr);
        return std::make_pair(curr, tail);
    }

    /* Undo the split() step to recover a valid view in case of an error. */
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

    /* Sort a view in-place using an iterative merge sort algorithm. */
    template <typename View>
    static void execute(View& view, bool reverse) {
        using Node = typename View::Node;

        // NOTE: we use a series of pairs to keep track of the head and tail of each
        // sublist.  `unsorted` keeps track of the nodes that still need to be sorted,
        // while `sorted` does the same for those that have already been processed.
        // The `left`, `right`, and `merged` pairs track the sublists that are used in
        // each iteration of the merge loop.
        std::pair<Node*, Node*> unsorted = std::make_pair(view.head(), view.tail());
        std::pair<Node*, Node*> sorted = std::make_pair(nullptr, nullptr);
        std::pair<Node*, Node*> left = std::make_pair(nullptr, nullptr);
        std::pair<Node*, Node*> right = std::make_pair(nullptr, nullptr);
        std::pair<Node*, Node*> merged;

        // NOTE: as a refresher, the general merge sort algorithm is as follows:
        //  1) divide the list into sublists of length 1 (bottom-up)
        //  2) merge adjacent sublists into sorted mixtures with twice the length
        //  3) repeat step 2 until the entire list is sorted
        size_t length = 1;  // length of sublists for current iteration
        while (length <= view.size()) {
            // reset head and tail of sorted list
            sorted.first = nullptr;
            sorted.second = nullptr;

            // divide and conquer
            while (unsorted.first != nullptr) {
                // split the list into two sublists of size `length`
                left.first = unsorted.first;
                left.second = walk(left.first, length - 1);
                right.first = left.second->next();
                right.second = walk(right.first, length - 1);
                if (right.second == nullptr) {  // right sublist is empty
                    unsorted.first = nullptr;  // terminate the loop
                } else {
                    unsorted.first = right.second->next();
                }

                // unlink the sublists from the original list
                Node::split(sorted.second, left.first);  // sorted <-/-> left
                Node::split(left.second, right.first);  // left <-/-> right
                Node::split(right.second, unsorted.first);  // right <-/-> unsorted

                // merge the left and right sublists in sorted order
                try {
                    merged = merge(left, right, view.temp(), reverse);
                } catch (...) {
                    // undo the splits to recover a coherent list
                    merged = recover(sorted, left, right, unsorted);
                    view.head(merged.first);  // view is partially sorted, but valid
                    view.tail(merged.second);
                    throw;  // propagate
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

        // update view parameters in-place
        view.head(sorted.first);
        view.tail(sorted.second);
    }

};


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_SORT_H
