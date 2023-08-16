
// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_INSERT_H
#define BERTRAND_STRUCTS_ALGORITHMS_INSERT_H

#include <cstddef>  // size_t
#include <Python.h>  // CPython API
#include "../core/node.h"  // is_doubly_linked<>
#include "../core/view.h"  // views


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    /* Insert an item into a singly-linked list, set, or dictionary at the given index. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    void insert(ViewType<NodeType, Allocator>* view, size_t index, PyObject* item) {
        using Node = typename ViewType<NodeType, Allocator>::Node;

        // allocate a new node
        Node* node = view->node(item);
        if (node == nullptr) {
            return;
        }

        // NOTE: if the index is closer to the tail and the list is doubly-linked,
        // we can iterate from the tail to save time.
        if constexpr (is_doubly_linked<Node>::value) {
            if (index > view->size / 2) {
                // iterate from tail to find junction
                Node* next = nullptr;
                Node* curr = view->tail;
                for (size_t i = view->size - 1; i > index; i--) {
                    next = curr;
                    curr = static_cast<Node*>(curr->prev);
                }

                // insert node
                view->link(curr, node, next);
                if (PyErr_Occurred()) {
                    view->recycle(node);  // clean up staged node
                    return;
                }
            }
        }

        // NOTE: otherwise, we iterate from the head
        Node* prev = nullptr;
        Node* curr = view->head;
        for (size_t i = 0; i < index; i++) {
            prev = curr;
            curr = static_cast<Node*>(curr->next);
        }

        // insert node
        view->link(prev, node, curr);
        if (PyErr_Occurred()) {
            view->recycle(node);  // clean up staged node
            return;
        }
    }

}


#endif // INSERT_H include guard
