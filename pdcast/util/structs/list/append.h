// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_APPEND_H
#define BERTRAND_STRUCTS_ALGORITHMS_APPEND_H

#include <Python.h>  // for CPython API
#include "node.h"  // for nodes
#include "view.h"  // for views


// append() for sets and dicts should mimic set.add() and dict.__setitem__(),
// respectively.  If the item is already contained in the set or dict, then
// we just silently return.  Errors are only thrown if the input is invalid,
// i.e. not hashable or not a tuple of length 2 in the case of dictionaries,
// or if a memory allocation error occurs.


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Add an item to the end of a list, set, or dictionary. */
template <
    template <typename, template <typename> class> class ViewType,
    typename NodeType,
    template <typename> class Allocator
>
inline void append(
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

    if constexpr (is_setlike<ViewType, NodeType, Allocator>::value) {
        // check if item is contained in hash table
        if (view->search(node) != nullptr) {  // item already exists
            view->recycle(node);
            return;  // do nothing
        }
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


/* Add a key-value pair to the end of a dictionary. */
template <typename NodeType, template <typename> class Allocator>
inline void append(
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


///////////////////////
////    ALIASES    ////
///////////////////////


// NOTE: Cython doesn't play well with heavily templated functions, so we need
// to explicitly instantiate the specializations we need.  Maybe in a future
// release we won't have to do this:


// list.append()
template void append(
    ListView<SingleNode, DirectAllocator>* view,
    PyObject* item,
    bool left
);
template void append(
    ListView<SingleNode, FreeListAllocator>* view,
    PyObject* item,
    bool left
);
template void append(
    ListView<SingleNode, PreAllocator>* view,
    PyObject* item,
    bool left
);
template void append(
    ListView<DoubleNode, DirectAllocator>* view,
    PyObject* item,
    bool left
);
template void append(
    ListView<DoubleNode, FreeListAllocator>* view,
    PyObject* item,
    bool left
);
template void append(
    ListView<DoubleNode, PreAllocator>* view,
    PyObject* item,
    bool left
);


// set.add/append()
template void append(
    SetView<SingleNode, DirectAllocator>* view,
    PyObject* item,
    bool left
);
template void append(
    SetView<SingleNode, FreeListAllocator>* view,
    PyObject* item,
    bool left
);
template void append(
    SetView<SingleNode, PreAllocator>* view,
    PyObject* item,
    bool left
);
template void append(
    SetView<DoubleNode, DirectAllocator>* view,
    PyObject* item,
    bool left
);
template void append(
    SetView<DoubleNode, FreeListAllocator>* view,
    PyObject* item,
    bool left
);
template void append(
    SetView<DoubleNode, PreAllocator>* view,
    PyObject* item,
    bool left
);

// dict.append()
template void append(
    DictView<SingleNode, DirectAllocator>* view,
    PyObject* item,
    bool left
);
template void append(
    DictView<SingleNode, DirectAllocator>* view,
    PyObject* item,
    PyObject* mapped,
    bool left
);
template void append(
    DictView<SingleNode, FreeListAllocator>* view,
    PyObject* item,
    bool left
);
template void append(
    DictView<SingleNode, FreeListAllocator>* view,
    PyObject* item,
    PyObject* mapped,
    bool left
);
template void append(
    DictView<SingleNode, PreAllocator>* view,
    PyObject* item,
    bool left
);
template void append(
    DictView<SingleNode, PreAllocator>* view,
    PyObject* item,
    PyObject* mapped,
    bool left
);
template void append(
    DictView<DoubleNode, DirectAllocator>* view,
    PyObject* item,
    bool left
);
template void append(
    DictView<DoubleNode, DirectAllocator>* view,
    PyObject* item,
    PyObject* mapped,
    bool left
);
template void append(
    DictView<DoubleNode, FreeListAllocator>* view,
    PyObject* item,
    bool left
);
template void append(
    DictView<DoubleNode, FreeListAllocator>* view,
    PyObject* item,
    PyObject* mapped,
    bool left
);
template void append(
    DictView<DoubleNode, PreAllocator>* view,
    PyObject* item,
    bool left
);
template void append(
    DictView<DoubleNode, PreAllocator>* view,
    PyObject* item,
    PyObject* mapped,
    bool left
);


#endif // BERTRAND_STRUCTS_ALGORITHMS_APPEND_H
