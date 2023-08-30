// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_UNION_H
#define BERTRAND_STRUCTS_ALGORITHMS_UNION_H

#include <Python.h>  // CPython API
#include "../core/allocate.h"  // DynamicAllocator
#include "../core/view.h"  // views


namespace Ops {

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
                    Py_DECREF(existing->mapped);
                    Py_INCREF(node->mapped);
                    existing->mapped = node->mapped;
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

        // unpack items into temporary set
        SetView<NodeType, DynamicAllocator> temp_view(items);

        // iterate over view and add all elements that are not in temp set to result
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

        // iterate over view and add all elements in temp view to result
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
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    ViewType<NodeType, Allocator>* symmetric_difference(
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

        // iterate over view and add all elements not in temp view to result
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

        // iterate over temp view and add all elements not in view to result
        curr = temp_view->head;
        while (curr != nullptr) {
            if (view->search(curr) == nullptr) {
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


#endif  // BERTRAND_STRUCTS_ALGORITHMS_UNION_H include guard
