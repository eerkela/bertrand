// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_INSERT_H
#define BERTRAND_STRUCTS_ALGORITHMS_INSERT_H

#include <cstddef>  // size_t
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/bounds.h"  // walk()
#include "../core/node.h"  // is_doubly_linked<>
#include "../core/view.h"  // views


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    /* Insert an item into a linked list, set, or dictionary at the given index. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    void insert(ViewType<NodeType, Allocator>* view, size_t index, PyObject* item) {
        using Node = typename ViewType<NodeType, Allocator>::Node;

        // If the index is closer to the tail and the list is doubly-linked,
        // then we can iterate from the tail to save time.
        if constexpr (is_doubly_linked<Node>::value) {
            if (index > view->size / 2) {  // backward traversal
                Node* next = nullptr;
                Node* prev = view->tail;
                for (size_t i = view->size - 1; i > index; i--) {
                    next = prev;
                    prev = static_cast<Node*>(prev->prev);
                }

                // insert node between neighbors
                _insert_between(view, prev, next, item);
                return;
            }
        }

        // Otherwise, iterate from the head
        Node* prev = nullptr;
        Node* next = view->head;
        for (size_t i = 0; i < index; i++) {
            prev = next;
            next = static_cast<Node*>(next->next);
        }

        // insert node between neighbors
        _insert_between(view, prev, next, item);
    }

    /* Insert an item into a linked set or dictionary relative to a given sentinel
    value. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    void insert_relative(
        ViewType<NodeType, Allocator>* view,
        PyObject* item,
        PyObject* sentinel,
        Py_ssize_t offset
    ) {
        using Node = typename ViewType<NodeType, Allocator>::Node;

        // search for sentinel
        Node* node = view->search(sentinel);
        if (node == nullptr) {  // sentinel not found
            PyErr_Format(PyExc_KeyError, "%R is not contained in the set", sentinel);
            return;
        }

        // walk according to offset
        std::pair<Node*, Node*> bounds = walk(view, node, offset, true);

        // insert node between left and right boundaries
        _insert_between(view, bounds.first, bounds.second, item);
    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Attempt to insert a node between the left and right neighbors. */
template <
    template <typename, template <typename> class> class ViewType,
    typename NodeType,
    template <typename> class Allocator,
    typename Node
>
void _insert_between(
    ViewType<NodeType, Allocator>* view,
    Node* left,
    Node* right,
    PyObject* item
) {
    // allocate a new node
    Node* curr = view->node(item);
    if (curr == nullptr) {
        return;  // propagate error
    }

    // insert node between neighbors
    view->link(left, curr, right);
    if (PyErr_Occurred()) {
        view->recycle(curr);  // clean up staged node before propagating
    }
}



#endif // BERTRAND_STRUCTS_ALGORITHMS_INSERT_H include guard
