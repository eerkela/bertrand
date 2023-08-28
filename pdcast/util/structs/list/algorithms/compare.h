// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H
#define BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H

#include <optional>  // std::optional
#include <Python.h>  // CPython API
#include "../core/allocate.h"  // DynamicAllocator
#include "../core/view.h"  // views


// TODO: intersection() should update mapped values for linked dictionaries
// if the key is in both views.


// TODO: we can't look up nodes of a type that doesn't match the view's node type.
// This affects any function that unpacks items into a temporary view, especially if
// the temporary view is an explicit SetView.


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    ///////////////////////////////
    ////    SET COMPARISONS    ////
    ///////////////////////////////

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

    //////////////////////////////
    ////    SET OPERATIONS    ////
    //////////////////////////////

    /* Get the union between a linked set or dictionary and an arbitrary Python
    iterable. */
    template <typename View>
    View* union_(View* view, PyObject* items, bool left) {
        using Node = typename View::Node;

        // CPython API equivalent of `iter(items)`
        PyObject* iterator = PyObject_GetIter(items);
        if (iterator == nullptr) {
            return nullptr;  // propagate error
        }

        // copy view
        View* result = view->copy();
        if (result == nullptr) {
            Py_DECREF(iterator);
            return nullptr;  // propagate error
        }

        // iterate over items and add any elements that are missing from view
        while (true) {
            PyObject* item = PyIter_Next(iterator);  // next(iterator)
            if (item == nullptr) {  // end of iterator or error
                if (PyErr_Occurred()) {
                    Py_DECREF(iterator);
                    delete result;
                    return nullptr;
                }
                break;
            }

            // allocate a new node
            Node* node = result->node(item);
            if (node == nullptr) {
                Py_DECREF(iterator);
                Py_DECREF(item);
                delete result;
                return nullptr;
            }

            // check if item is contained in hash table
            Node* existing = result->search(node);
            if (existing != nullptr) {
                if constexpr (has_mapped<Node>::value) {
                    _update_mapped(existing, node->mapped);
                }
                result->recycle(node);
                Py_DECREF(item);
                continue;  // advance to next item
            }

            // link to beginning/end of list
            if (left) {
                result->link(nullptr, node, result->head);
            } else {
                result->link(result->tail, node, nullptr);
            }
            if (PyErr_Occurred()) {  // check for errors during link()
                Py_DECREF(iterator);
                Py_DECREF(item);
                delete result;
                return nullptr;
            }

            // advance to next item
            Py_DECREF(item);
        }

        return result;
    }

    /* Get the difference between a linked set or dictionary and an arbitrary Python
    iterable. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    ViewType<NodeType, Allocator>* difference(
        ViewType<NodeType, Allocator>* view,
        PyObject* items
    ) {
        using View = ViewType<NodeType, Allocator>;
        using Node = typename View::Node;

        // allocate a new view to hold the result
        View* result;
        try {
            result = new View(view->max_size, view->specialization);
        } catch (const std::bad_alloc&) {
            PyErr_NoMemory();
            return nullptr;
        }

        // unpack items into temporary view
        SetView<NodeType, DynamicAllocator> temp_view(items);

        // iterate over view and add all elements that are not in temp view to result
        Node* curr = view->head;
        while (curr != nullptr) {
            if (temp_view->search(curr) == nullptr) {
                _copy_into(result, curr, false);
                if (PyErr_Occurred()) {
                    delete result;
                    return nullptr;
                }
            }
            curr = static_cast<Node*>(curr->next);
        }

        return result;
    }

    /* Get the intersection between a linked set or dictionary and an arbitrary Python
    iterable. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    ViewType<NodeType, Allocator>* intersection(
        ViewType<NodeType, Allocator>* view,
        PyObject* items
    ) {
        using View = ViewType<NodeType, Allocator>;
        using Node = typename View::Node;

        // generate a new view to hold the result
        View* result;
        try {
            result = new View(view->max_size, view->specialization);
        } catch (const std::bad_alloc&) {
            PyErr_NoMemory();
            return nullptr;
        }

        // unpack items into temporary view
        ViewType<NodeType, DynamicAllocator> temp_view(items);

        // iterate over view1 and add all elements in view2 to result
        Node* curr = view->head;
        while (curr != nullptr) {
            Node* other = temp_view->search(curr);
            if (other != nullptr) {
                _copy_into(result, other, false);  // copy from temporary view
                if (PyErr_Occurred()) {
                    delete result;
                    return nullptr;
                }
            }
            curr = static_cast<Node*>(curr->next);
        }

        return result;
    }

    /* Get the symmetric difference between a linked set or dictionary and an arbitrary
    Python iterable. */
    template <typename View>
    View* symmetric_difference(View* view, PyObject* items) {
        using Node = typename View::Node;

        // unpack items into temporary view


        // compute (A - B)
        View* diff1 = difference(view, items, false);
        if (diff1 == nullptr) {
            return nullptr;  // propagate error
        }


        // compute (B - A)
        View* diff2 = difference(view2, view1, false);
        if (diff2 == nullptr) {
            delete diff1;
            return nullptr;  // propagate error
        }

        // (A - B) U (B - A)
        View* result = union_(diff1, diff2);
        delete diff1;  // clean up intermediate views
        delete diff2;
        return result;
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

    // NOTE: update() does not have a specialization for Python iterables because
    // it is already implemented in extend.h

    /* Update a linked set or dictionary in-place, removing elements from a second
    set or dictionary. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    void difference_update(ViewType<NodeType, Allocator>* view, PyObject* items) {
        using View = ViewType<NodeType, Allocator>;
        using Node = typename View::Node;

        // unpack items into temporary view
        SetView<NodeType, DynamicAllocator> temp_view(items);

        // iterate over view and remove all elements in view2
        Node* prev = nullptr;
        Node* curr = view->head;
        while (curr != nullptr) {
            Node* next = static_cast<Node*>(curr->next);
            if (view2->search(curr) != nullptr) {
                view1->unlink(prev, curr, next);
                view1->recycle(curr);
            }
            prev = curr;
            curr = next;
        }
    }

    /* Update a linked set or dictionary in-place, keeping only elements found in
    both sets or dictionaries. */
    template <typename View>
    void intersection_update(View* view1, View* view2) {
        using Node = typename View::Node;

        // iterate over view1 and remove all elements not in view2
        Node* prev = nullptr;
        Node* curr = view1->head;
        while (curr != nullptr) {
            Node* next = static_cast<Node*>(curr->next);
            if (view2->search(curr) == nullptr) {
                view1->unlink(prev, curr, next);
                view1->recycle(curr);
            }
            prev = curr;
            curr = next;
        }
    }
    /* Update a linked set or dictionary in-place, keeping only elements found in
    either set or dictionary, but not both. */
    template <typename View>
    void symmetric_difference_update(View* view1, View* view2) {
        using Node = typename View::Node;

        // (B - A)  (NOTE: this avoids modifying view2 during update)
        View* diff2 = difference(view2, view1);
        if (diff2 == nullptr) {
            return;  // propagate error
        }

        // (A - B)
        difference_update(view1, view2);

        // (A - B) U (B - A)
        update(view1, diff2);
    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Copy a node from one view to another. */
template <typename View, typename Node>
void _copy_into(View* view, Node* node, bool left) {
    Node* copied = view->copy(node);
    if (copied == nullptr) {  // check for errors during init_copy()
        return;  // propagate
    }

    // add node to result
    if (left) {
        view->link(nullptr, copied, view->head);
    } else {
        view->link(view->tail, copied, nullptr);
    }
    if (PyErr_Occurred()) {  // check for errors during link()
        view->recycle(copied);
        return;  // propagate
    }
}


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


/* Update mapped values for linked dictionaries during update(). */
template <typename Node>
inline void _update_mapped(Node* existing, PyObject* value) {
    Py_DECREF(existing->mapped);
    Py_INCREF(value);
    existing->mapped = value;
}


#endif // BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H include guard
