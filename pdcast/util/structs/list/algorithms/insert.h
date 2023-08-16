
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
        Node* curr = view->node(item);
        if (curr == nullptr) {
            return;
        }

        // NOTE: if the index is closer to the tail and the list is doubly-linked,
        // we can iterate from the tail to save time.
        if constexpr (is_doubly_linked<Node>::value) {
            if (index > view->size / 2) {
                // iterate from tail to find junction
                Node* next = nullptr;
                Node* prev = view->tail;
                for (size_t i = view->size - 1; i > index; i--) {
                    next = prev;
                    prev = static_cast<Node*>(prev->prev);
                }

                // insert node
                view->link(prev, curr, next);
                if (PyErr_Occurred()) {
                    view->recycle(curr);  // clean up staged node
                }
                return;
            }
        }

        // NOTE: otherwise, we iterate from the head
        Node* prev = nullptr;
        Node* next = view->head;
        for (size_t i = 0; i < index; i++) {
            prev = next;
            next = static_cast<Node*>(next->next);
        }

        // insert node
        view->link(prev, curr, next);
        if (PyErr_Occurred()) {
            view->recycle(curr);  // clean up staged node
        }
    }

}


#endif // INSERT_H include guard
