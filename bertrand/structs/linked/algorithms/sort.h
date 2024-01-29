#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_SORT_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_SORT_H

#include <cstddef>  // size_t
#include <utility>  // std::pair
#include "../../util/ops.h"  // lt()
#include "../core/node.h"  // Keyed<>
#include "../core/view.h"  // ListView, ViewTraits


namespace bertrand {
namespace linked {


    template <typename SortPolicy, typename View, typename Func>
    auto sort(View& view, Func key, bool reverse)
        -> std::enable_if_t<ViewTraits<View>::linked, void>
    {
        using Node = typename View::Node;
        using KeyNode = Keyed<Node, Func>;

        // if no key function is given, sort in-place
        if (key == nullptr) {
            SortPolicy::execute(view, reverse);
            return;
        }

        // otherwise, decorate each node with computed key
        using Decorated = ListView<KeyNode, Config::SINGLY_LINKED | Config::FIXED_SIZE>;
        Decorated decorated(view.size(), nullptr);
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            KeyNode* node = decorated.node(it.curr(), key);
            decorated.link(decorated.tail(), node, nullptr);
        }

        SortPolicy::execute(decorated, reverse);

        // undecorate and reflect changes in original view, recycling as we go
        Node* new_head = nullptr;
        Node* new_tail = nullptr;
        for (auto it = decorated.begin(), end = decorated.end(); it != end; ) {
            Node* unwrapped = it.curr()->node();
            if (new_head == nullptr) {
                new_head = unwrapped;
            }
            Node::join(new_tail, unwrapped);
            new_tail = unwrapped;
            decorated.recycle(it.drop());  // implicitly advances iterator
        }

        // update head and tail of original view
        Node::join(new_tail, nullptr);
        view.head(new_head);
        view.tail(new_tail);
    }


    //////////////////////////
    ////    ALGORITHMS    ////
    //////////////////////////


    /* An iterative merge sort algorithm with error recovery. */
    class MergeSort {
    protected:

        /* Walk along the list by the specified number of nodes. */
        template <typename Node>
        inline static Node* walk(Node* curr, size_t length) {
            if (curr == nullptr) {
                return nullptr;  // nothing left to traverse
            }
            for (size_t i = 0; i < length; i++) {
                if (curr->next() == nullptr) {
                    break;  // end of list
                }
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
            Node* L = left.first;
            Node* R = right.first;

            // build merged sublist by appending smaller of L and R
            while (L != nullptr && R != nullptr) {
                if (reverse ^ lt(L->value(), R->value())) {
                    Node::join(curr, L);
                    L = L->next();
                    left.first = L->next();
                } else {
                    Node::join(curr, R);
                    R = R->next();
                    right.first = R;
                }
                curr = curr->next();
            }

            // link remaining nodes
            Node* tail;
            if (L != nullptr) {
                Node::join(curr, L);
                tail = left.second;
            } else {
                Node::join(curr, R);
                tail = right.second;
            }

            // unlink temporary node and return proper head and tail
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
            Node::join(sorted.second, left.first);  // sorted tail <-> left head
            Node::join(left.second, right.first);  // left tail <-> right head
            Node::join(right.second, unsorted.first);  // right tail <-> unsorted head
            return std::make_pair(sorted.first, unsorted.second);
        }

    public:

        /* NOTE: as a refresher, the general merge sort algorithm is as follows:
        *  1) divide the list into sublists of length 1 (bottom-up)
        *  2) merge adjacent sublists into sorted mixtures with twice the length
        *  3) repeat step 2 until the entire list is sorted
        *
        * We use a series of pairs to keep track of the head and tail of each sublist.
        * `unsorted` tracks the nodes that still need to be sorted, while `sorted`
        * does the same for those that have already been processed.  `left`, `right`,
        * and `merged` pairs track the sublists that are used in each iteration of the
        * merge loop.
        */

        /* Sort a view in-place using an iterative merge sort algorithm. */
        template <typename View>
        static void execute(View& view, bool reverse) {
            using Node = typename View::Node;

            // check if view is already sorted
            bool is_sorted = true;
            for (auto it = view.begin(); it.next() != nullptr; ++it) {
                if (reverse) {
                    if (lt(*it, it.next()->value())) {
                        is_sorted = false;
                        break;
                    }
                } else {
                    if (lt(it.next()->value(), *it)) {
                        is_sorted = false;
                        break;
                    }
                }
            }
            if (is_sorted) {
                return;
            }

            std::pair<Node*, Node*> unsorted = std::make_pair(view.head(), view.tail());
            std::pair<Node*, Node*> sorted = std::make_pair(nullptr, nullptr);
            std::pair<Node*, Node*> left = std::make_pair(nullptr, nullptr);
            std::pair<Node*, Node*> right = std::make_pair(nullptr, nullptr);
            std::pair<Node*, Node*> merged;

            // iterate until the entire list is sorted
            size_t length = 1;
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

                    // unlink the sublists from original list
                    Node::split(sorted.second, left.first);
                    Node::split(left.second, right.first);
                    Node::split(right.second, unsorted.first);

                    // merge left and right sublists in sorted order
                    try {
                        merged = merge(left, right, view.temp(), reverse);
                    } catch (...) {
                        // undo splits to recover a coherent list
                        merged = recover(sorted, left, right, unsorted);
                        view.head(merged.first);  // view is partially sorted, but valid
                        view.tail(merged.second);
                        throw;
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

            // update final head/tail
            view.head(sorted.first);
            view.tail(sorted.second);
        }

    };


}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_SORT_H
