// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_ADD_H
#define BERTRAND_STRUCTS_ALGORITHMS_ADD_H

#include <Python.h>  // CPython API
#include "../core/view.h"  // views
#include "insert.h"  // _insert_relative()


namespace Ops {

    /* Add an item to the end of a linked set or dictionary. */
    template <typename View>
    void add(View* view, PyObject* item, bool left) {
        using Node = typename View::Node;

        // allocate a new node
        Node* node = view->node(item);
        if (node == nullptr) {
            return;  // propagate error
        }

        // check if item is contained in hash table
        Node* existing = view->search(node);
        if (existing != nullptr) {  // item already exists
            if constexpr (has_mapped<Node>::value) {
                // update mapped value
                Py_DECREF(existing->mapped);
                Py_INCREF(node->mapped);
                existing->mapped = node->mapped;
            }
            view->recycle(node);
            return;  // do nothing
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
    void add(
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

        // check if item is contained in hash table
        Node* existing = view->search(node);
        if (view->search(node) != nullptr) {  // item already exists
            // update mapped value
            Py_DECREF(existing->mapped);
            Py_INCREF(node->mapped);
            existing->mapped = node->mapped;
            view->recycle(node);
            return;
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

    /* Add an item to a linked set or dictionary relative to a given sentinel
    value if it is not already present. */
    template <typename View>
    inline void add_relative(
        View* view,
        PyObject* item,
        PyObject* sentinel,
        Py_ssize_t offset
    ) {
        _insert_relative(view, item, sentinel, offset, false);  // suppress errors
    }

}


#endif // BERTRAND_STRUCTS_ALGORITHMS_ADD_H
