// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_APPEND_H
#define BERTRAND_STRUCTS_ALGORITHMS_APPEND_H

#include <Python.h>  // CPython API
#include "../core/view.h"  // views


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    /* Add an item to the end of a linked set or dictionary. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    void append(
        ViewType<NodeType, Allocator>* view,
        PyObject* item,
        bool left
    ) {
        using Node = typename ViewType<NodeType, Allocator>::Node;

        // allocate a new node
        Node* node = view->node(item);
        if (node == nullptr) {  // Error during node initialization
            return;
        }

        // check if item is contained in hash table
        if (view->search(node) != nullptr) {  // item already exists
            view->recycle(node);
            return;  // do nothing
        }

        // link to beginning/end of list
        if (left) {
            view->link(nullptr, node, view->head);
        } else {
            view->link(view->tail, node, nullptr);
        }
        if (PyErr_Occurred()) {  // Error during link
            view->recycle(node);
        }
    }

    /* Add an item to the end of a linked list. */
    template <typename NodeType, template <typename> class Allocator>
    void append(
        ListView<NodeType, Allocator>* view,
        PyObject* item,
        bool left
    ) {
        using Node = typename ListView<NodeType, Allocator>::Node;

        // allocate a new node
        Node* node = view->node(item);
        if (node == nullptr) {  // Error during node initialization
            return;
        }

        // link to beginning/end of list
        if (left) {
            view->link(nullptr, node, view->head);
        } else {
            view->link(view->tail, node, nullptr);
        }
        if (PyErr_Occurred()) {  // Error during link
            view->recycle(node);
        }
    }

    /* Add a key-value pair to the end of a linked dictionary. */
    template <typename NodeType, template <typename> class Allocator>
    void append(
        DictView<NodeType, Allocator>* view,
        PyObject* item,
        PyObject* mapped,
        bool left
    ) {
        using Node = typename DictView<NodeType, Allocator>::Node;

        // allocate a new node
        Node* node = view->node(item, mapped);  // use 2-argument init()
        if (node == nullptr) {  // Error during node initialization
            return;
        }

        // check if item is contained in hash table
        if (view->search(node) != nullptr) {  // item already exists
            view->recycle(node);
            return;  // do nothing
        }

        // link to beginning/end of list
        if (left) {
            view->link(nullptr, node, view->head);
        } else {
            view->link(view->tail, node, nullptr);
        }
        view->link(view->tail, node, nullptr);
        if (PyErr_Occurred()) {  // Error during link
            view->recycle(node);
        }
    }

}


#endif // BERTRAND_STRUCTS_ALGORITHMS_APPEND_H
