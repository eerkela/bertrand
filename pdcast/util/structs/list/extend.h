
// include guard prevents multiple inclusion
#ifndef EXTEND_H
#define EXTEND_H

#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API
#include <node.h>  // for node definitions
#include <view.h>  // for views


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Add multiple items to the end of a list. */
template <typename NodeType>
inline void extend(ListView<NodeType>* view, PyObject* items) {
    _extend_left_to_right(view, view->tail, NULL, items);  // handles errors
}


/* Add multiple items to the end of a set. */
template <typename NodeType>
inline void extend(SetView<NodeType>* view, PyObject* items) {
    _extend_left_to_right(view, view->tail, NULL, items);  // handles errors
}


/* Add multiple items to the end of a dictionary. */
template <typename NodeType>
inline void extend(DictView<NodeType>* view, PyObject* items) {
    _extend_left_to_right(view, view->tail, NULL, items);  // handles errors
}


/* Add multiple items to the beginning of a list. */
template <typename NodeType>
inline void extendleft(ListView<NodeType>* view, PyObject* items) {
    _extend_right_to_left(view, NULL, view->head, items);  // handles errors
}


/* Add multiple items to the beginning of a list. */
template <typename NodeType>
inline void extendleft(SetView<NodeType>* view, PyObject* items) {
    _extend_right_to_left(view, NULL, view->head, items);  // handles errors
}


/* Add multiple items to the beginning of a list. */
template <typename NodeType>
inline void extendleft(DictView<NodeType>* view, PyObject* items) {
    _extend_right_to_left(view, NULL, view->head, items);  // handles errors
}


/* Insert elements into a set after a given sentinel value. */
template <typename NodeType>
inline void extendafter(
    SetView<NodeType>* view,
    PyObject* sentinel,
    PyObject* items
) {
    // search for sentinel
    Hashed<NodeType>* left = view->search(sentinel);
    if (left == NULL) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R is not contained in the list", sentinel);
        return;
    }
    Hashed<NodeType>* right = (Hashed<NodeType>*)left->next;

    // insert items between the left and right bounds
    _extend_left_to_right(view, left, right, items);  // handles errors
}


/* Insert elements into a dictionary after a given sentinel value. */
template <typename NodeType>
inline void extendafter(
    SetView<NodeType>* view,
    PyObject* sentinel,
    PyObject* items
) {
    // search for sentinel
    Mapped<NodeType>* left = view->search(sentinel);
    if (left == NULL) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R is not contained in the list", sentinel);
        return;
    }
    Mapped<NodeType>* right = (Mapped<NodeType>*)left->next;

    // insert items between the left and right bounds
    _extend_left_to_right(view, left, right, items);  // handles errors
}


namespace SinglyLinked {

    // NOTE: due to the singly-linked nature of the list, extendafter() is
    // O(m) while extendbefore() is O(n + m).  This is because we need to
    // traverse the whole list to find the node before the sentinel.

    /* Insert elements into a set before a given sentinel value. */
    template <typename NodeType>
    inline void extendbefore(
        SetView<NodeType>* view,
        PyObject* sentinel,
        PyObject* items
    ) {
        // search for sentinel
        Hashed<NodeType>* right = view->search(sentinel);
        if (right == NULL) {  // sentinel not found
            PyErr_Format(PyExc_KeyError, "%R is not contained in the list", sentinel);
            return;
        }

        // iterate from head to find left bound
        Hashed<NodeType>* left;
        Hashed<NodeType>* next;
        if (right == view->head) {
            left = NULL;
        } else {
            left = view->head;
            next = (Hashed<NodeType>*)left->next;
            while (next != right) {
                left = next;
                next = (Hashed<NodeType>*)next->next;
            }
        }

        // insert items between the left and right bounds
        _extend_right_to_left(view, left, right, items);  // handles errors
    }

    /* Insert elements into a set before a given sentinel value. */
    template <typename NodeType>
    inline void extendbefore(
        DictView<NodeType>* view,
        PyObject* sentinel,
        PyObject* items
    ) {
                // search for sentinel
        Mapped<NodeType>* right = view->search(sentinel);
        if (right == NULL) {  // sentinel not found
            PyErr_Format(PyExc_KeyError, "%R is not contained in the list", sentinel);
            return;
        }

        // iterate from head to find left bound
        Mapped<NodeType>* left;
        Mapped<NodeType>* next;
        if (right == view->head) {
            left = NULL;
        } else {
            left = view->head;
            next = (Mapped<NodeType>*)left->next;
            while (next != right) {
                left = next;
                next = (Mapped<NodeType>*)next->next;
            }
        }

        // insert items between the left and right bounds
        _extend_right_to_left(view, left, right, items);  // handles errors
    }

}


namespace DoublyLinked {

    /* Insert elements into a set after a given sentinel value. */
    template <typename NodeType>
    inline void extendbefore(
        SetView<NodeType>* view,
        PyObject* sentinel,
        PyObject* items
    ) {
        // search for sentinel
        Hashed<NodeType>* right = view->search(sentinel);
        if (right == NULL) {  // sentinel not found
            PyErr_Format(PyExc_KeyError, "%R is not contained in the list", sentinel);
            return;
        }
        Hashed<NodeType>* left = (Hashed<NodeType>*)view->prev;  // use prev pointer

        // insert items between the left and right bounds
        _extend_right_to_left(view, left, right, items);  // handles errors
    }

    /* Insert elements into a dictionary after a given sentinel value. */
    template <typename NodeType>
    inline void extendbefore(
        SetView<NodeType>* view,
        PyObject* sentinel,
        PyObject* items
    ) {
        // search for sentinel
        Mapped<NodeType>* right = view->search(sentinel);
        if (right == NULL) {  // sentinel not found
            PyErr_Format(PyExc_KeyError, "%R is not contained in the list", sentinel);
            return;
        }
        Mapped<NodeType>* left = (Mapped<NodeType>*)view->prev;  // use prev pointer

        // insert items between the left and right bounds
        _extend_right_to_left(view, left, right, items);  // handles errors
    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Insert items from the left node to the right node. */
template <template <typename> class ViewType, typename T, typename U>
void _extend_left_to_right(ViewType<T>* view, U* left, U* right, PyObject* items) {
    // CPython API equivalent of `iter(items)`
    PyObject* iterator = PyObject_GetIter(items);
    if (iterator == NULL) {  // TypeError() during iter()
        return;
    }

    // CPython API equivalent of `for item in items:`
    U* node;
    U* prev = left;
    PyObject* item;
    while (true) {
        item = PyIter_Next(iterator);  // next(iterator)
        if (item == NULL) {  // end of iterator or error
            break;
        }

        // allocate a new node
        node = view->allocate(item);
        if (node == NULL) {  // MemoryError() or TypeError() during hash()
            Py_DECREF(item);
            break;
        }

        // insert from left to right
        view->link(prev, node, right);
        if (PyErr_Occurred()) {  // ValueError() during link()
            Py_DECREF(item);
            break;
        }

        // advance to next item
        prev = node;
        Py_DECREF(item);
    }

    // release iterator
    Py_DECREF(iterator);

    // check for error
    if (PyErr_Occurred()) {
        _undo_left_to_right(view, left, right);  // recover original list
        if (right == NULL) {
            view->tail = right;  // replace original tail
        }
    }
}


/* Insert items from the right node to the left node. */
template <template <typename> class ViewType, typename T, typename U>
void _extend_right_to_left(ViewType<T>* view, U* left, U* right, PyObject* items) {
    // CPython API equivalent of `iter(items)`
    PyObject* iterator = PyObject_GetIter(items);
    if (iterator == NULL) {  // TypeError() during iter()
        return;
    }

    // CPython API equivalent of `for item in items:`
    U* node;
    U* prev = right;
    PyObject* item;
    while (true) {
        item = PyIter_Next(iterator);  // next(iterator)
        if (item == NULL) {  // end of iterator or error
            break;
        }

        // allocate a new node
        node = view->allocate(item);
        if (node == NULL) {  // MemoryError() or TypeError() during hash()
            Py_DECREF(item);
            break;
        }

        // insert from right to left
        view->link(left, node, prev);
        if (PyErr_Occurred()) {  // ValueError() during link()
            Py_DECREF(item);
            break;
        }

        // advance to next item
        prev = node;
        Py_DECREF(item);
    }

    // release iterator
    Py_DECREF(iterator);

    // check for error
    if (PyErr_Occurred()) {
        _undo_right_to_left(view, left, right);  // recover original list
        if (left == NULL) {
            view->head = left;  // replace original head
        }
    }
}


/* Rewind an `extend()`/`extendafter()` call in the event of an error. */
template <template <typename> class ViewType, typename T, typename U>
void _undo_left_to_right(ViewType<T>* view, U* left, U* right) {
    // NOTE: we always assume left is not NULL.  right may be, however.
    U* prev = left;

    // free staged nodes
    U* curr = (U*)prev->next;
    U* next;
    while (curr != right) {
        next = (U*)curr->next;
        view->unlink(prev, curr, next);
        view->deallocate(curr);
        curr = next;
    }

    // join left and right
    U::join(left, right);  // handles NULLs
}


/* Rewind an `extendleft()`/`extendbefore()` call in the event of an error. */
template <template <typename> class ViewType, typename T, typename U>
void _undo_right_to_left(ViewType<T>* view, U* left, U* right) {
    // NOTE: we always assume right is not NULL.  left may be, however.
    U* prev;
    if (left == NULL) {
        prev = view->head;
    } else {
        prev = left;
    }

    // free staged nodes
    U* curr = (U*)prev->next;
    U* next;
    while (curr != right) {
        next = (U*)curr->next;
        view->unlink(prev, curr, next);
        view->deallocate(curr);
        curr = next;
    }

    // join left and right
    U::join(left, right);  // handles NULLs
}


#endif // EXTEND_H include guard
