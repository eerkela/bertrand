
// include guard prevents multiple inclusion
#ifndef CONTAINS_H
#define CONTAINS_H

#include <Python.h>  // for CPython API
#include <view.h>  // for views


/* Check if an item is contained within a set or dictionary. */
template <template <typename> class ViewType, typename NodeType>
inline int contains(ViewType<NodeType>* view, PyObject* item) {
    return view->search(item) != NULL;
}


/* Check if an item is contained within a list. */
template <typename NodeType>
inline int contains(ListView<NodeType>* view, PyObject* item) {
    using Node = typename ListView<NodeType>::Node;
    Node* curr = view->head;
    int comp;

    // search until we find the item or hit stop index
    while (curr != NULL) {
        // C API equivalent of the == operator
        comp = PyObject_RichCompareBool(curr->value, item, Py_EQ);
        if (comp == -1) {  // == comparison raised an exception
            return -1;
        } else if (comp == 1) {  // found a match
            return 1;
        }

        // advance to next node
        curr = (Node*)curr->next;
    }

    // item not found
    return 0;
}


#endif // CONTAINS_H include guard
