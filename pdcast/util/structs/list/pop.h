
// include guard prevents multiple inclusion
#ifndef POP_H
#define POP_H

#include <cstddef>  // for size_t
#include <queue>  // for std::queue
#include <Python.h>  // for CPython API
#include <view.h>  // for views





template <typename ViewType, typename NodeType>
class ListOps {
private:
    ViewType<NodeType>* view;


public:
    ListOps(ViewType<NodeType>* view) {
        this->view = view;
    }


    /*Pop an item at the given index and return its value.*/
    PyObject* pop(long long index = -1) {
        size_t norm_index = normalize_index(index);
        NodeType* curr = node_at_index(norm_index);

        // get return value
        PyObject* value = curr->value;
        Py_INCREF(value);  // have to INCREF because we DECREF in deallocate()

        // unlink and deallocate node
        view->unlink(curr->prev, curr, curr->next);
        view->deallocate(curr);
        return value;
    }

    /*Pop the first item in the list.*/
    PyObject* popleft() {
        if (view->head == NULL) {
            PyErr_SetString(PyExc_IndexError, "pop from empty list");
            return NULL;
        }

        // get return value
        PyObject* value = view->head->value;
        Py_INCREF(value);  // have to INCREF because we DECREF in deallocate()

        // unlink and deallocate node
        view->unlink(NULL, view->head, view->head->next);
        view->deallocate(view->head);
        return value;
    }

    /*Pop the last item in the list.*/
    PyObject* popright() {
        if (view->tail == NULL) {
            PyErr_SetString(PyExc_IndexError, "pop from empty list");
            return NULL;
        }

        // get return value
        PyObject* value = view->tail->value;
        Py_INCREF(value);  // have to INCREF because we DECREF in deallocate()

        // unlink and deallocate node
        view->unlink(view->tail->prev, view->tail, NULL);
        view->deallocate(view->tail);
        return value;
    }

};



#endif // POP_H include guard
