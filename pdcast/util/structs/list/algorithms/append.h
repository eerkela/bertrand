// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_APPEND_H
#define BERTRAND_STRUCTS_ALGORITHMS_APPEND_H

#include <Python.h>  // CPython API
#include "../core/view.h"  // views


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    ////////////////////////
    ////    APPEND()    ////
    ////////////////////////

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
        _link_to_end(view, node, left);
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
        _link_to_end(view, node, left);
    }

    /////////////////////
    ////    ADD()    ////
    /////////////////////

    // NOTE: add() is identical to append() except that it does not raise an
    // error if the item is already contained in a set or dictionary.

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
        _link_to_end(view, node, left);
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
        _link_to_end(view, node, left);
    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Attempt to link a node to the beginning/end of a list. */
template <typename View, typename Node>
inline void _link_to_end(View* view, Node* node, bool left) {
    // link to beginning/end of list
    if (left) {
        view->link(nullptr, node, view->head);
    } else {
        view->link(view->tail, node, nullptr);
    }

    // check for error
    if (PyErr_Occurred()) {
        view->recycle(node);  // clean up allocated node
    }
}


#endif // BERTRAND_STRUCTS_ALGORITHMS_APPEND_H
