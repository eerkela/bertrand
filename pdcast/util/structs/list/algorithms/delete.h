// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_DELETE_SLICE_H
#define BERTRAND_STRUCTS_ALGORITHMS_DELETE_SLICE_H

#include <cstddef>  // size_t
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/bounds.h"  // normalize_slice()
#include "../core/node.h"  // has_prev<>
#include "../core/view.h"  // views


namespace Ops {

    /* Delete the value at a particular index of a linked list, set, or dictionary. */
    template <typename View, typename T>
    void delete_index(View& view, T index) {
        using Node = typename View::Node;

        // allow python-style negative indexing + boundschecking
        size_t idx = normalize_index(index, view.size, false);
        if (idx == MAX_SIZE_T && PyErr_Occurred()) {
            return;  // propagate error
        }

        // get neighbors at index
        std::tuple<Node*, Node*, Node*> bounds = neighbors(&view, view.head, idx);
        Node* prev = std::get<0>(bounds);
        Node* curr = std::get<1>(bounds);
        Node* next = std::get<2>(bounds);

        // unlink and deallocate node
        view.unlink(prev, curr, next);
        view.recycle(curr);
    }

}


namespace SliceOps {

    /* Delete a slice from a linked list, set, or dictionary. */
    template <typename SliceProxy>
    void drop(SliceProxy& slice) {
        using Node = typename SliceProxy::Node;

        // check for no-op
        if (slice.length() == 0) {
            return;
        }

        // recycle every node in slice
        for (auto iter = slice.begin(1), end = slice.end(); iter != end; ++iter) {
            Node* node = iter.remove();
            slice.view().recycle(node);
        }
    }

}


#endif // BERTRAND_STRUCTS_ALGORITHMS_DELETE_SLICE_H include guard
