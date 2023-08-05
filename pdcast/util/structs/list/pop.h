
// include guard prevents multiple inclusion
#ifndef POP_H
#define POP_H

#include <cstddef>  // for size_t
#include <queue>  // for std::queue
#include <Python.h>  // for CPython API
#include <view.h>  // for views


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Pop an item from the beginning of a list. */
template <typename NodeType>
inline PyObject* popleft(ListView<NodeType>* view) {
    if (view->size == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty list");
        return NULL;
    }

    // destroy node and return its value
    NodeType* head = view->head;
    NodeType* next = head->next;
    return _drop_node(view, NULL, head, next);
}


/* Pop an item from the beginning of a set. */
template <typename NodeType>
inline PyObject* popleft(SetView<NodeType>* view) {
    if (view->size == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty set");
        return NULL;
    }

    // destroy node and return its value
    Hashed<NodeType>* head = view->head;
    Hashed<NodeType>* next = (Hashed<NodeType>*)head->next;
    return _drop_node(view, NULL, head, next);
}


/* Pop an item from the beginning of a dictionary. */
template <typename NodeType>
inline PyObject* popleft(DictView<NodeType>* view) {
    if (view->size == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty dictionary");
        return NULL;
    }

    // destroy node and return its value
    Mapped<NodeType>* head = view->head;
    Mapped<NodeType>* next = (Mapped<NodeType>*)head->next;
    return _drop_node(view, NULL, head, next);
}


// NOTE: due to the singly-linked nature of the list, popping from the
// front of the list is O(1) while popping from the back is O(n). This
// is because we need to traverse the entire list to find the node that
// precedes the popped node.


/* Pop an item from a singly-linked list at the given index. */
template <typename NodeType>
inline PyObject* pop_single(ListView<NodeType>* view, size_t index) {
    // iterate from head
    NodeType* curr = view->head;
    NodeType* prev = NULL;  // shadows curr
    for (size_t i = 0; i < index; i++) {
        prev = curr;
        curr = curr->next;
    }

    // destroy node and return its value
    return _drop_node(view, prev, curr, curr->next);
}


/* Pop an item from a singly-linked set at the given index. */
template <typename NodeType>
inline PyObject* pop_single(SetView<NodeType>* view, size_t index) {
    // iterate from head
    Hashed<NodeType>* curr = view->head;
    Hashed<NodeType>* prev = NULL;  // shadows curr
    for (size_t i = 0; i < index; i++) {
        prev = curr;
        curr = (Hashed<NodeType>*)curr->next;
    }

    // destroy node and return its value
    return _drop_node(view, prev, curr, curr->next);
}


/* Pop an item from a singly-linked dictionary at the given index. */
template <typename NodeType>
inline PyObject* pop_single(DictView<NodeType>* view, size_t index) {
    // iterate from head
    Mapped<NodeType>* curr = view->head;
    Mapped<NodeType>* prev = NULL;  // shadows curr
    for (size_t i = 0; i < index; i++) {
        prev = curr;
        curr = (Mapped<NodeType>*)curr->next;
    }

    // destroy node and return its value
    return _drop_node(view, prev, curr, curr->next);
}


/* Pop an item from a singly-linked dictionary by its key. */
template <typename NodeType>
inline PyObject* pop_single(
    DictView<NodeType>* view,
    PyObject* key,
    PyObject* default_value
) {
    // NOTE: this is O(n) for singly-linked dictionaries because we have
    // to iterate from the head to find the node that precedes the popped
    // node.

    // search for node
    Mapped<NodeType>* curr = view->search(key);
    if (curr == NULL) {
        return default_value;
    }

    // iterate forwards from head to find prev
    Mapped<NodeType>* prev = NULL;
    Mapped<NodeType>* node = view->head;
    while (node != curr) {
        prev = node;
        node = (Mapped<NodeType>*)node->next;
    }

    // destroy node and return its value
    return _drop_node(view, prev, curr, curr->next);
}


/* Pop an item from the end of a singly-linked list. */
template <typename NodeType>
inline PyObject* popright_single(ListView<NodeType>* view) {
    if (view->size == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty list");
        return NULL;
    }

    // iterate from head
    NodeType* curr = view->head;
    NodeType* prev = NULL;  // shadows curr
    while (curr != view->tail) {
        prev = curr;
        curr = curr->next;
    }

    // destroy node and return its value
    return _drop_node(view, prev, curr, NULL);
}


/* Pop an item from the end of a singly-linked set. */
template <typename NodeType>
inline PyObject* popright_single(SetView<NodeType>* view) {
    if (view->size == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty set");
        return NULL;
    }

    // iterate from head
    Hashed<NodeType>* curr = view->head;
    Hashed<NodeType>* prev = NULL;  // shadows curr
    while (curr != view->tail) {
        prev = curr;
        curr = (Hashed<NodeType>*)curr->next;
    }

    // destroy node and return its value
    return _drop_node(view, prev, curr, NULL);
}


/* Pop an item from the end of a singly-linked dictionary. */
template <typename NodeType>
inline PyObject* popright_single(DictView<NodeType>* view) {
    if (view->size == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty dictionary");
        return NULL;
    }

    // iterate from head
    Mapped<NodeType>* curr = view->head;
    Mapped<NodeType>* prev = NULL;  // shadows curr
    while (curr != view->tail) {
        prev = curr;
        curr = (Mapped<NodeType>*)curr->next;
    }

    // destroy node and return its value
    return _drop_node(view, prev, curr, NULL);
}


// NOTE: doubly-linked lists, on the other hand, can pop from both sides in
// O(1) time.


/* Pop an item from a doubly-linked list at the given index. */
template <typename NodeType>
inline PyObject* pop_double(ListView<NodeType>* view, size_t index) {
    // if index is closer to head, default to singly-linked variant
    if (index <= view->size / 2) {
        return pop_single(view, index);
    }

    // iterate from tail
    NodeType* curr = view->tail;
    for (size_t i = view->size - 1; i > index; i--) {
        curr = curr->prev;
    }

    // destroy node and return its value
    return _drop_node(view, curr->prev, curr, curr->next);
}


/* Pop an item from a doubly-linked set at the given index. */
template <typename NodeType>
inline PyObject* pop_double(SetView<NodeType>* view, size_t index) {
    // if index is closer to head, default to singly-linked variant
    if (index <= view->size / 2) {
        return pop_single(view, index);
    }

    // iterate from tail
    Hashed<NodeType>* curr = view->tail;
    for (size_t i = view->size - 1; i > index; i--) {
        curr = (Hashed<NodeType>*)curr->prev;
    }

    // destroy node and return its value
    return _drop_node(view, curr->prev, curr, curr->next);
}


/* Pop an item from a doubly-linked dictionary at the given index. */
template <typename NodeType>
inline PyObject* pop_double(DictView<NodeType>* view, size_t index) {
    // if index is closer to head, default to singly-linked variant
    if (index <= view->size / 2) {
        return pop_single(view, index);
    }

    // iterate from tail
    Mapped<NodeType>* curr = view->tail;
    for (size_t i = view->size - 1; i > index; i--) {
        curr = (Mapped<NodeType>*)curr->prev;
    }

    // destroy node and return its value
    return _drop_node(view, curr->prev, curr, curr->next);
}


/* Pop an item from a doubly-linked dictionary by its key. */
template <typename NodeType>
inline PyObject* pop_double(
    DictView<NodeType>* view,
    PyObject* key,
    PyObject* default_value
) {
    // NOTE: this is O(1) for doubly-linked dictionaries because we can
    // directly use the node's prev and next pointers to unlink it from
    // the list, rather than iterating from the head.

    // search for node
    Mapped<NodeType>* curr = view->search(key);
    if (curr == NULL) {
        return default_value;
    }

    // destroy node and return its value
    return _drop_node(view, curr->prev, curr, curr->next);
}


/* Pop an item from the end of a doubly-linked list. */
template <typename NodeType>
inline PyObject* popright_double(ListView<NodeType>* view) {
    if (view->size == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty dictionary");
        return NULL;
    }

    // destroy node and return its value
    NodeType* curr = view->tail;
    NodeType* prev = curr->prev;
    return _drop_node(view, prev, curr, NULL);
}


/* Pop an item from the end of a doubly-linked set. */
template <typename NodeType>
inline PyObject* popright_double(SetView<NodeType>* view) {
    if (view->size == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty dictionary");
        return NULL;
    }

    // destroy node and return its value
    Hashed<NodeType>* curr = view->tail;
    Hashed<NodeType>* prev = (Hashed<NodeType>*)curr->prev;
    return _drop_node(view, prev, curr, NULL);
}


/* Pop an item from the end of a doubly-linked dictionary. */
template <typename NodeType>
inline PyObject* popright_double(DictView<NodeType>* view) {
    if (view->size == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty dictionary");
        return NULL;
    }

    // destroy node and return its value
    Mapped<NodeType>* curr = view->tail;
    Mapped<NodeType>* prev = (Mapped<NodeType>*)curr->prev;
    return _drop_node(view, prev, curr, NULL);
}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Unlink and remove a node and return its value. */
template <template <typename> class ViewType, typename NodeType>
inline PyObject* _drop_node(
    ViewType<NodeType>* view,
    NodeType* prev,
    NodeType* curr,
    NodeType* next
) {
    // get return value
    PyObject* value = curr->value;
    Py_INCREF(value);  // have to INCREF because we DECREF in deallocate()

    // unlink and deallocate node
    view->unlink(prev, curr, next);
    view->deallocate(curr);
    return value;

}


#endif // POP_H include guard
