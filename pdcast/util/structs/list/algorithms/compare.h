// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H
#define BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H

#include <Python.h>  // CPython API
#include "../core/allocate.h"  // DynamicAllocator
#include "../core/view.h"  // views


namespace Ops {

    /* Check whether a linked set or dictionary has any elements in common with
    an arbitrary Python iterable. */
    template <typename View>
    int isdisjoint(View* view, PyObject* items) {
        // CPython API equivalent of `iter(items)`
        PyObject* iterator = PyObject_GetIter(items);
        if (iterator == nullptr) {
            return -1;  // propagate error
        }

        // iterate over items and check if any are in view
        while (true) {
            PyObject* item = PyIter_Next(iterator);  // next(iterator)
            if (item == nullptr) {  // end of iterator or error
                if (PyErr_Occurred()) {
                    Py_DECREF(iterator);
                    return -1;
                }
                break;
            }

            // check if item is in view
            if (view->search(item) != nullptr) {
                Py_DECREF(item);
                Py_DECREF(iterator);
                return 0;
            }

            // advance to next item
            Py_DECREF(item);
        }

        // release iterator
        Py_DECREF(iterator);
        return 1;  // no items in common
    }

    /* Check whether a linked set or dictionary represents a subset of an arbitrary
    Python iterable. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    int issubset(ViewType<NodeType, Allocator>* view, PyObject* items, bool strict) {
        using View = ViewType<NodeType, Allocator>;
        using Node = typename View::Node;

        // unpack items into temporary view
        SetView<NodeType, DynamicAllocator> temp_view(items);

        // if strict, check that view is smaller than temporary view
        if (strict && view->size >= temp_view->size) {
            return 0;
        }

        // iterate over view and check if all elements are in temporary view
        Node* curr = view->head;
        while (curr != nullptr) {
            if (temp_view->search(curr) == nullptr) {
                return 0;
            }
            curr = static_cast<Node*>(curr->next);
        }

        return 1;
    }

    /* Check whether a linked set or dictionary represents a superset of an arbitrary
    Python iterable. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    int issuperset(ViewType<NodeType, Allocator>* view, PyObject* items, bool strict) {
        using View = ViewType<NodeType, Allocator>;
        using Node = typename View::Node;

        // unpack items into temporary view
        SetView<NodeType, DynamicAllocator> temp_view(items);

        // if strict, check that view is larger than temporary view
        if (strict && view->size <= temp_view->size) {
            return 0;
        }

        // iterate over temporary view and check if all elements are in view
        Node* curr = temp_view->head;
        while (curr != nullptr) {
            if (view->search(curr) == nullptr) {
                return 0;
            }
            curr = static_cast<Node*>(curr->next);
        }

        return 1;
    }

}


#endif // BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H include guard
