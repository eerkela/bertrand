// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_COUNT_H
#define BERTRAND_STRUCTS_ALGORITHMS_COUNT_H

#include <cstddef>  // size_t
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "position.h"  // normalize_index()


namespace bertrand {
namespace structs {
namespace algorithms {


namespace list {

    /* Count the number of occurrences of an item within a linked list. */
    template <typename View, typename T>
    size_t count(View& view, PyObject* item, T start, T stop) {
        using Node = typename View::Node;

        // trivial case: empty list
        if (view.size() == 0) {
            return 0;
        }

        // normalize start/stop indices
        size_t norm_start = normalize_index(start, view.size(), true);
        size_t norm_stop = normalize_index(stop, view.size(), true);
        if (norm_start > norm_stop) {
            throw std::invalid_argument(
                "start index cannot be greater than stop index"
            );
        }

        // if list is doubly-linked and stop is closer to tail than start is to head,
        // then we iterate backward from the tail
        if constexpr (Node::doubly_linked) {
            if ((view.size() - 1 - norm_stop) < norm_start) {
                // get backwards iterator to stop index
                auto iter = view.iter.reverse();
                size_t idx = view.size() - 1;
                while (idx > norm_stop) {
                    ++iter;
                    --idx;
                }

                // search until we hit start index
                size_t count = 0;
                while (idx >= norm_start) {
                    Node* node = *iter;
                    count += node->eq(item);  // branchless
                    ++iter;
                    --idx;
                }
                return count;
            }
        }

        // otherwise, we iterate forward from the head
        auto iter = view.begin();
        size_t idx = 0;
        while (idx < norm_start) {
            ++iter;
            ++idx;
        }

        // search until we hit item or stop index
        size_t count = 0;
        while (idx < norm_stop) {
            Node* node = *iter;
            count += node->eq(item);  // branchless
            ++iter;
            ++idx;
        }
        return count;
    }

}  // namespace list


namespace set {

    /* Count the number of occurrences of an item within a linked set or dictionary. */
    template <typename View, typename T>
    size_t count(View& view, PyObject* item, T start, T stop) {
        using Node = typename View::Node;

        // allow Python-style negative indexing + boundschecking
        std::pair<size_t, size_t> bounds = normalize_bounds(
            start, stop, view.size, true
        );

        // check if item is contained in hash table
        Node* node = view.search(item);
        if (node == nullptr) {
            return 0;
        }

        // if range includes all items, return true
        if (bounds.first == 0 && bounds.second == view.size - 1) {
            return 1;
        }

        // else, find index of item
        Node* curr = view.head;
        size_t idx = 0;
        while (curr != node && idx < bounds.second) {
            curr = static_cast<Node*>(curr->next);
            idx++;
        }

        // check if index is in range
        if (idx >= bounds.first && idx < bounds.second) {
            return 1;
        }
        return 0;
    }

}  // namespace set


}  // namespace algorithms
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_COUNT_H include guard
