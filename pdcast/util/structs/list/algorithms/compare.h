// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H
#define BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H

#include <optional>  // std::optional
#include <Python.h>  // CPython API
#include "../core/allocate.h"  // DynamicAllocator
#include "../core/view.h"  // views


// TODO: intersection() should update mapped values for linked dictionaries
// if the key is in both views.


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    /* Check whether a linked set or dictionary has any elements in common with
    an arbitrary Python iterable. */
    template <typename View>
    int isdisjoint(View* view, PyObject* items) {
        using Node = typename View::Node;

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

        // TODO: temp_view->search() will fail if the original view is a dictionary.
        // -> node types don't match in this case.

        // unpack items into temporary view
        SetView<NodeType, DynamicAllocator> temp_view(items);

        // iterate over view and check if all elements are in temporary view
        Node* curr = view->head;
        while (curr != nullptr) {
            if (temp_view->search(curr) == nullptr) {
                return 0;
            }
            curr = static_cast<Node*>(curr->next);
        }

        // if strict, check that view is not equal to temporary view
        if (strict) {
            return view->size < temp_view->size;
        }
        return 1;
    }

    /* Check whether a linked set or dictionary represents a superset of an arbitrary
    Python iterable. */
    template <typename View>
    int issuperset(View* view, PyObject* items, bool strict) {
        using Node = typename View::Node;

        // CPython API equivalent of `iter(items)`
        PyObject* iterator = PyObject_GetIter(items);
        if (iterator == nullptr) {
            return -1;  // propagate error
        }
        size_t iter_size = 0;  // track number of items in iterator

        // iterate over items and check if any are not in view
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
            if (view->search(item) == nullptr) {
                Py_DECREF(item);
                Py_DECREF(iterator);
                return 0;
            }

            // advance to next item
            Py_DECREF(item);
            iter_size++;
        }

        // release iterator
        Py_DECREF(iterator);

        // if strict, check that view is not equal to items
        if (strict) {
            return view->size > iter_size;
        }
        return 1;
    }

    ////////////////////////
    ////    IN-PLACE    ////
    ////////////////////////

    /* Update a linked set or dictionary in-place, adding elements from a second
    set or dictionary. */
    template <typename View>
    void update(View* view, PyObject* items, bool left) {
        using Node = typename View::Node;

        // iterate over view2 and add all unique elements to view1
        Node* curr = view2->head;
        while (curr != nullptr) {
            if (view1->search(curr) == nullptr) {
                _copy_into(view1, curr, left);
                if (PyErr_Occurred()) {
                    return;
                }
            }
            curr = static_cast<Node*>(curr->next);
        }
    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Convert a Python iterable into an appropriate view before calling the specified
function. */
template <typename Func, typename View, typename... Args>
auto _from_python(Func func, View* view, PyObject* iterable, Args... args) {
    using ReturnType = decltype(func(view, std::declval<View*>(), args...));

    try {
        // convert iterable into a temporary view
        View temp_view(iterable);

        // if the specified function returns void, call it directly.  Otherwise,
        // wrap the result in a std::optional.
        if constexpr (std::is_void_v<ReturnType>) {
            func(view, &temp_view, args...);
        } else {
            return std::optional<ReturnType>{func(view, &temp_view, args...)};
        }

    // memory error during view construction
    } catch (const std::bad_alloc&) {
        PyErr_NoMemory();
        if constexpr (!std::is_void_v<ReturnType>) {
            return std::optional<ReturnType>{};  // propagate error
        }
    }
}


#endif // BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H include guard
