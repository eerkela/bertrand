
// include guard prevents multiple inclusion
#ifndef POP_H
#define POP_H

#include <cstddef>  // for size_t
#include <queue>  // for std::queue
#include <Python.h>  // for CPython API
#include <node.h>  // for nodes
#include <view.h>  // for views


// TODO: consider having pop() start from the front of the dictionary in order to
// make it O(1) for singly-linked lists rather than O(n).  Index just defaults
// to 0 rather than -1.


///////////////////
////    POP    ////
///////////////////


/* Pop an item from a singly-linked list, set, or dictionary at the given index. */
template <template <typename> class ViewType, typename NodeType>
inline PyObject* pop_single(ViewType<NodeType>* view, size_t index) {
    // NOTE: due to the singly-linked nature of the list, popping from the
    // front of the list is O(1) while popping from the back is O(n). This
    // is because we need to traverse the entire list to find the node that
    // precedes the popped node.
    using Node = typename ViewType<NodeType>::Node;

    // iterate forwards from head
    Node* prev = NULL;
    Node* curr = view->head;
    for (size_t i = 0; i < index; i++) {
        prev = curr;
        curr = (Node*)curr->next;
    }

    // destroy node and return its value
    return _pop_node(view, prev, curr, (Node*)curr->next);
}


/* Pop a key from a singly-linked dictionary and return its corresponding value. */
template <typename NodeType>
inline PyObject* pop_single(
    DictView<NodeType>* view,
    PyObject* key,
    PyObject* default_value
) {
    using Node = typename DictView<NodeType>::Node;

    // search for node
    Node* curr = view->search(key);
    if (curr == NULL) {
        return default_value;
    }

    // NOTE: this is O(n) for singly-linked dictionaries because we have to
    // traverse the whole list to find the node that precedes the popped node.

    // iterate forwards from head to find prev
    Node* prev = NULL;
    Node* node = view->head;
    while (node != curr) {
        prev = node;
        node = (Node*)node->next;
    }

    // destroy node and return its value
    return _pop_node(view, prev, curr, (Node*)curr->next);
}


/* Pop an item from a doubly-linked list, set, or dictionary at the given index. */
template <template <typename> class ViewType, typename NodeType>
inline PyObject* pop_double(ViewType<NodeType>* view, size_t index) {
    // NOTE: doubly-linked lists can pop from both sides in O(1) time.
    if (index <= view->size / 2) {
        return pop_single(view, index);  // use singly-linked version
    }

    using Node = typename ViewType<NodeType>::Node;

    // iterate backwards from tail
    Node* curr = view->tail;
    for (size_t i = view->size - 1; i > index; i--) {
        curr = (Node*)curr->prev;
    }

    // destroy node and return its value
    return _pop_node(view, (Node*)curr->prev, curr, (Node*)curr->next);
}


/* Pop a key from a doubly-linked dictionary and return its corresponding value. */
template <typename NodeType>
inline PyObject* pop_double(
    DictView<NodeType>* view,
    PyObject* key,
    PyObject* default_value
) {
    using Node = typename DictView<NodeType>::Node;

    // search for node
    Node* curr = view->search(key);
    if (curr == NULL) {
        return default_value;
    }

    // NOTE: this is O(1) for doubly-linked dictionaries because we can use
    // the node's prev and next pointers to unlink it from the list.

    // destroy node and return its value
    return _pop_node(view, (Node*)curr->prev, curr, (Node*)curr->next);
}


///////////////////////
////    POPLEFT    ////
///////////////////////


/* Pop an item from the beginning of a list, set, or dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline PyObject* popleft(ViewType<NodeType>* view) {
    if (view->size == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty list");
        return NULL;
    }

    using Node = typename ViewType<NodeType>::Node;

    // destroy node and return its value
    Node* head = view->head;
    return _pop_node(view, (Node*)NULL, head, (Node*)head->next);
}


////////////////////////
////    POPRIGHT    ////
////////////////////////


/* Pop an item from the end of a singly-linked list. */
template <template <typename> class ViewType, typename NodeType>
inline PyObject* popright_single(ViewType<NodeType>* view) {
    if (view->size == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty list");
        return NULL;
    }

    // NOTE: this is O(n) for singly-linked lists because we have to traverse
    // the whole list to find the node that precedes the tail.
    return pop_single(view, view->size - 1);
}


/* Pop an item from the end of a doubly-linked list. */
template <template <typename> class ViewType, typename NodeType>
inline PyObject* popright_double(ViewType<NodeType>* view) {
    if (view->size == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty dictionary");
        return NULL;
    }

    using Node = typename ViewType<NodeType>::Node;

    // NOTE: this is O(1) for doubly-linked lists because we can use the tail's
    // prev pointer to unlink it from the list.
    Node* tail = view->tail;
    return _pop_node(view, (Node*)tail->prev, tail, (Node*)NULL);
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
    Py_INCREF(value);  // have to INCREF because we DECREF in deallocate()

    // unlink and deallocate node
    view->unlink(prev, curr, next);
    view->recycle(curr);
    return value;
}


////////////////////////
////    WRAPPERS    ////
////////////////////////


// NOTE: Cython doesn't play well with nested templates, so we need to
// explicitly instantiate specializations for each combination of node/view
// type.  This is a bit of a pain, put it's the only way to get Cython to
// properly recognize the functions.

// Maybe in a future release we won't have to do this:


template PyObject* pop_single(ListView<SingleNode>* view, size_t index);
template PyObject* pop_single(SetView<SingleNode>* view, size_t index);
template PyObject* pop_single(DictView<SingleNode>* view, size_t index);
template PyObject* pop_single(
    DictView<SingleNode>* view,
    PyObject* key,
    PyObject* default_value
);
template PyObject* pop_single(ListView<DoubleNode>* view, size_t index);
template PyObject* pop_single(SetView<DoubleNode>* view, size_t index);
template PyObject* pop_single(DictView<DoubleNode>* view, size_t index);
template PyObject* pop_single(
    DictView<DoubleNode>* view,
    PyObject* key,
    PyObject* default_value
);
template PyObject* pop_double(ListView<DoubleNode>* view, size_t index);
template PyObject* pop_double(SetView<DoubleNode>* view, size_t index);
template PyObject* pop_double(DictView<DoubleNode>* view, size_t index);
template PyObject* pop_double(
    DictView<DoubleNode>* view,
    PyObject* key,
    PyObject* default_value
);
template PyObject* popleft(ListView<SingleNode>* view);
template PyObject* popleft(SetView<SingleNode>* view);
template PyObject* popleft(DictView<SingleNode>* view);
template PyObject* popright_single(ListView<SingleNode>* view);
template PyObject* popright_single(SetView<SingleNode>* view);
template PyObject* popright_single(DictView<SingleNode>* view);
template PyObject* popright_single(ListView<DoubleNode>* view);
template PyObject* popright_single(SetView<DoubleNode>* view);
template PyObject* popright_single(DictView<DoubleNode>* view);
template PyObject* popright_double(ListView<DoubleNode>* view);
template PyObject* popright_double(SetView<DoubleNode>* view);
template PyObject* popright_double(DictView<DoubleNode>* view);


#endif // POP_H include guard
