// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_APPEND_H
#define BERTRAND_STRUCTS_ALGORITHMS_APPEND_H

#include <Python.h>  // CPython API
#include "../core/view.h"  // views


namespace Ops {

    /* Add an item to the end of a linked list, set, or dictionary. */
    template <typename View>
    void append(View* view, PyObject* item, bool left) {
        using Node = typename View::Node;

        // allocate a new node
        Node* node = view->node(item);
        if (node == nullptr) {
            return;  // propagate error
        }

        // link to beginning/end of list
        if (left) {
            view->link(nullptr, node, view->head);
        } else {
            view->link(view->tail, node, nullptr);
        }
        if (PyErr_Occurred()) {
            view->recycle(node);  // clean up allocated node
        }
    }

    /* Add a key-value pair to the end of a linked dictionary. */
    template <typename NodeType, template <typename> class Allocator>
    void append(
        DictView<NodeType, Allocator>* view,
        PyObject* key,
        PyObject* value,
        bool left
    ) {
        using Node = typename DictView<NodeType, Allocator>::Node;

        // allocate a new node (use 2-argument init())
        Node* node = view->node(key, value);
        if (node == nullptr) {
            return;  // propagate error
        }

        // link to beginning/end of list
        if (left) {
            view->link(nullptr, node, view->head);
        } else {
            view->link(view->tail, node, nullptr);
        }
        if (PyErr_Occurred()) {
            view->recycle(node);  // clean up allocated node
        }
    }

}


#endif // BERTRAND_STRUCTS_ALGORITHMS_APPEND_H
