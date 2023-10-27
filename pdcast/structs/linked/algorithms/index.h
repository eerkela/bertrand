// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_INDEX_H
#define BERTRAND_STRUCTS_ALGORITHMS_INDEX_H

#include <cstddef>  // size_t
#include <sstream>  // std::ostringstream
#include <stdexcept>  // std::invalid_argument
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "position.h"  // normalize_index()
#include "../../util/repr.h"  // repr()


namespace bertrand {
namespace structs {
namespace linked {
namespace algorithms {


namespace list {

    /* Get the index of an item within a linked list. */
    template <typename View, typename T>
    size_t index(const View& view, PyObject* item, T start, T stop) {
        using Node = typename View::Node;

        // trivial case: empty list
        if (view.size() == 0) {
            std::ostringstream msg;
            msg << util::repr(item) << " is not in list";
            throw std::invalid_argument(msg.str());
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
                auto it = view.rbegin();
                size_t idx = view.size() - 1;
                while (idx >= norm_stop) {
                    ++it;
                    --idx;
                }

                // search until we hit start index
                bool found = false;
                size_t last_observed;
                while (idx >= norm_start) {
                    const Node* node = it.curr();
                    if (node->eq(item)) {
                        found = true;
                        last_observed = idx;
                    }
                    ++it;
                    --idx;
                }
                if (found) {
                    return last_observed;
                }

                // item not found
                std::ostringstream msg;
                msg << util::repr(item) << " is not in list";
                throw std::invalid_argument(msg.str());
            }
        }

        // otherwise, we iterate forward from the head
        auto it = view.begin();
        size_t idx = 0;
        while (idx < norm_start) {
            ++it;
            ++idx;
        }

        // search until we hit item or stop index
        while (idx < norm_stop) {
            const Node* node = it.curr();
            if (node->eq(item)) {
                return idx;
            }
            ++it;
            ++idx;
        }

        // item not found
        std::ostringstream msg;
        msg << util::repr(item) << " is not in list";
        throw std::invalid_argument(msg.str());
    }

}


// namespace set {

//     /* Get the index of an item within a linked set or dictionary. */
//     template <typename View, typename T>
//     size_t index(View& view, PyObject* item, T start, T stop) {
//         using Node = typename View::Node;

//         // allow python-style negative indexing + boundschecking
//         std::pair<size_t, size_t> bounds = normalize_bounds(
//             start, stop, view.size, true
//         );

//         // search for item in hash table
//         Node* node = view.search(item);
//         if (node == nullptr) {
//             PyErr_Format(PyExc_ValueError, "%R is not in the set", item);
//             return MAX_SIZE_T;
//         }

//         // skip to start index
//         Node* curr = view.head;
//         size_t idx;
//         for (idx = 0; idx < bounds.first; idx++) {
//             if (curr == node) {  // item exists, but comes before range
//                 PyErr_Format(PyExc_ValueError, "%R is not in the set", item);
//                 return MAX_SIZE_T;
//             }
//             curr = static_cast<Node*>(curr->next);
//         }

//         // iterate until we hit match or stop index
//         while (curr != node && idx < bounds.second) {
//             curr = static_cast<Node*>(curr->next);
//             idx++;
//         }
//         if (curr == node) {
//             return idx;
//         }

//         // item exists, but comes after range
//         PyErr_Format(PyExc_ValueError, "%R is not in the set", item);
//         return MAX_SIZE_T;
//     }

//     /* Get the linear distance between two values in a linked set or dictionary. */
//     template <typename View>
//     Py_ssize_t distance(View& view, PyObject* item1, PyObject* item2) {
//         using Node = typename View::Node;

//         // search for nodes in hash table
//         Node* node1 = view.search(item1);
//         if (node1 == nullptr) {
//             PyErr_Format(PyExc_KeyError, "%R is not in the set", item1);
//             return 0;
//         }
//         Node* node2 = view.search(item2);
//         if (node2 == nullptr) {
//             PyErr_Format(PyExc_KeyError, "%R is not in the set", item2);
//             return 0;
//         }

//         // check for no-op
//         if (node1 == node2) {
//             return 0;  // do nothing
//         }

//         // get indices of both nodes
//         Py_ssize_t idx = 0;
//         Py_ssize_t index1 = -1;
//         Py_ssize_t index2 = -1;
//         Node* curr = view.head;
//         while (true) {
//             if (curr == node1) {
//                 index1 = idx;
//                 if (index2 != -1) {
//                     break;  // both nodes found
//                 }
//             } else if (curr == node2) {
//                 index2 = idx;
//                 if (index1 != -1) {
//                     break;  // both nodes found
//                 }
//             }
//             curr = static_cast<Node*>(curr->next);
//             idx++;
//         }

//         // return difference between indices
//         return index2 - index1;
//     }

// }


}  // namespace algorithms
}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_INDEX_H include guard
