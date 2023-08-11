
// include guard prevents multiple inclusion
#ifndef POP_H
#define POP_H

#include <cstddef>  // for size_t
#include <queue>  // for std::queue
#include <Python.h>  // for CPython API
#include "node.h"  // for nodes
#include "view.h"  // for views


// TODO: consider having pop() start from the front of the dictionary in order to
// make it O(1) for singly-linked lists rather than O(n).  Index just defaults
// to 0 rather than -1.


///////////////////
////    POP    ////
///////////////////


/* Pop an item from a linked list, set, or dictionary at the given index. */
template <template <typename> class ViewType, typename NodeType>
inline PyObject* pop(ViewType<NodeType>* view, size_t index) {
    using Node = typename ViewType<NodeType>::Node;
    Node* prev;
    Node* curr;

    // get neighboring nodes
    if constexpr (is_doubly_linked<Node>::value) {
        // NOTE: if the list is doubly-linked, then we can iterate from either
        // end to find the node that preceding node.
        if (index > view->size / 2) {
            curr = view->tail;
            for (size_t i = view->size - 1; i > index; i--) {
                curr = static_cast<Node*>(curr->prev);
            }
            prev = static_cast<Node*>(curr->prev);
            return _pop_node(view, prev, curr, static_cast<Node*>(curr->next));
        }
    }

    // NOTE: due to the singly-linked nature of the list, popping from the
    // front of the list is O(1) while popping from the back is O(n). This
    // is because we need to traverse the entire list to find the node that
    // precedes the popped node.
    prev = nullptr;
    curr = view->head;
    for (size_t i = 0; i < index; i++) {
        prev = curr;
        curr = static_cast<Node*>(curr->next);
    }

    // destroy node and return its value
    return _pop_node(view, prev, curr, static_cast<Node*>(curr->next));
}


/* Pop a key from a linked dictionary and return its corresponding value. */
template <typename NodeType>
inline PyObject* pop(
    DictView<NodeType>* view,
    PyObject* key,
    PyObject* default_value
) {
    using Node = typename DictView<NodeType>::Node;
    Node* prev;
    Node* curr;

    // search for node
    curr = view->search(key);
    if (curr == nullptr) {
        return default_value;
    }

    // get neighboring nodes
    if constexpr (is_doubly_linked<Node>::value) {
        // NOTE: this is O(1) for doubly-linked dictionaries because we can use
        // the node's prev and next pointers to unlink it from the list.
        prev = static_cast<Node*>(curr->prev);
    } else {
        // NOTE: this is O(n) for singly-linked dictionaries because we have to
        // traverse the whole list to find the node that precedes the popped node.
        prev = nullptr;
        Node* node = view->head;
        while (node != curr) {
            prev = node;
            node = static_cast<Node*>(node->next);
        }
    }

    // destroy node and return its value
    return _pop_node(view, prev, curr, static_cast<Node*>(curr->next));
}


/* Pop an item from the beginning of a list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline PyObject* popleft(ViewType<NodeType>* view) {
    if (view->size == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty list");
        return nullptr;
    }

    using Node = typename ViewType<NodeType>::Node;

    // destroy node and return its value
    Node* head = view->head;
    Node* next = static_cast<Node*>(head->next);
    return _pop_node(view, static_cast<Node*>(nullptr), head, next);
}


/* Pop an item from the end of a singly-linked list. */
template <template <typename> class ViewType, typename NodeType>
inline PyObject* popright(ViewType<NodeType>* view) {
    using Node = typename ViewType<NodeType>::Node;

    if (view->size == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty list");
        return nullptr;
    }

    // NOTE: this is O(1) for doubly-linked lists because we can use the
    // tail's prev pointer to unlink it from the list.
    if constexpr (is_doubly_linked<Node>::value) {
        Node* prev = static_cast<Node*>(view->tail->prev);
        return _pop_node(view, prev, view->tail, static_cast<Node*>(nullptr));
    }

    // otherwise, we have to traverse the whole list to find the node that
    // precedes the tail.
    return pop(view, view->size - 1);
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Unlink and remove a node and return its value. */
template <template <typename> class ViewType, typename NodeType, typename Node>
inline PyObject* _pop_node(
    ViewType<NodeType>* view,
    Node* prev,
    Node* curr,
    Node* next
) {
    // get return value
    PyObject* value = curr->value;
    Py_INCREF(value);  // have to INCREF because we DECREF in recycle()

    // unlink and deallocate node
    view->unlink(prev, curr, next);
    view->recycle(curr);
    return value;  // caller takes ownership
}


////////////////////////
////    WRAPPERS    ////
////////////////////////


// NOTE: Cython doesn't play well with nested templates, so we need to
// explicitly instantiate specializations for each combination of node/view
// type.  This is a bit of a pain, put it's the only way to get Cython to
// properly recognize the functions.

// Maybe in a future release we won't have to do this:


template PyObject* pop(ListView<SingleNode>* view, size_t index);
template PyObject* pop(SetView<SingleNode>* view, size_t index);
template PyObject* pop(DictView<SingleNode>* view, size_t index);
template PyObject* pop(
    DictView<SingleNode>* view,
    PyObject* key,
    PyObject* default_value
);
template PyObject* pop(ListView<DoubleNode>* view, size_t index);
template PyObject* pop(SetView<DoubleNode>* view, size_t index);
template PyObject* pop(DictView<DoubleNode>* view, size_t index);
template PyObject* pop(
    DictView<DoubleNode>* view,
    PyObject* key,
    PyObject* default_value
);
template PyObject* popleft(ListView<SingleNode>* view);
template PyObject* popleft(SetView<SingleNode>* view);
template PyObject* popleft(DictView<SingleNode>* view);
template PyObject* popleft(ListView<DoubleNode>* view);
template PyObject* popleft(SetView<DoubleNode>* view);
template PyObject* popleft(DictView<DoubleNode>* view);
template PyObject* popright(ListView<SingleNode>* view);
template PyObject* popright(SetView<SingleNode>* view);
template PyObject* popright(DictView<SingleNode>* view);
template PyObject* popright(ListView<DoubleNode>* view);
template PyObject* popright(SetView<DoubleNode>* view);
template PyObject* popright(DictView<DoubleNode>* view);


#endif // POP_H include guard
