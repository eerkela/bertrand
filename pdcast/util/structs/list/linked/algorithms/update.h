// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_UPDATE_H
#define BERTRAND_STRUCTS_ALGORITHMS_UPDATE_H

#include <Python.h>  // CPython API
#include "../core/view.h"  // views
#include "extend.h"  // _extend_left_to_right(), _extend_right_to_left(), etc.
#include "union.h"  // difference()


namespace Ops {

    /* Update a set or dictionary, appending items that are not already present. */
    template <typename View>
    inline void update(View* view, PyObject* items, bool left) {
        using Node = typename View::Node;

        Node* null = static_cast<Node*>(nullptr);
        if (left) {
            _extend_right_to_left(view, null, view->head, items, true);
        } else {
            _extend_left_to_right(view, view->tail, null, items, true);
        }
    }

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

        // unpack items into temporary set
        SetView<NodeType, DynamicAllocator> temp_view(items);

        // iterate over view and remove all elements in temporary set
        Node* prev = nullptr;
        Node* curr = view->head;
        while (curr != nullptr) {
            Node* next = static_cast<Node*>(curr->next);
            if (temp_view->search(curr) != nullptr) {
                view->unlink(prev, curr, next);
                view->recycle(curr);
            }
            prev = curr;
            curr = next;
        }
    }

    /* Update a linked set or dictionary in-place, keeping only elements found in
    both sets or dictionaries. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    void intersection_update(ViewType<NodeType, Allocator>* view1, PyObject* items) {
        using View = ViewType<NodeType, Allocator>;
        using Node = typename View::Node;

        // unpack items into temporary view
        ViewType<NodeType, DynamicAllocator> temp_view(items);

        // iterate over view and remove all elements not in temp view
        Node* prev = nullptr;
        Node* curr = view1->head;
        while (curr != nullptr) {
            Node* next = static_cast<Node*>(curr->next);
            Node* other = temp_view->search(curr);
            if (other == nullptr) {
                view1->unlink(prev, curr, next);
                view1->recycle(curr);
            } else {
                if constexpr (has_mapped<Node>::value) {  // update mapped value
                    Py_DECREF(curr->mapped);
                    Py_INCREF(other->mapped);
                    curr->mapped = other->mapped;
                }
            }
            prev = curr;
            curr = next;
        }
    }

    /* Update a linked set or dictionary in-place, keeping only elements found in
    either set or dictionary, but not both. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    void symmetric_difference_update(
        ViewType<NodeType, Allocator>* view,
        PyObject* items
    ) {
        using View = ViewType<NodeType, Allocator>;
        using Node = typename View::Node;

        // unpack items into temporary view
        ViewType<NodeType, DynamicAllocator> temp_view(items);

        // iterate over view and remove all elements in temp view
        Node* prev = nullptr;
        Node* curr = view->head;
        while (curr != nullptr) {
            Node* next = static_cast<Node*>(curr->next);
            if (temp_view->search(curr) != nullptr) {
                view->unlink(prev, curr, next);
                view->recycle(curr);
            }
            prev = curr;
            curr = next;
        }

        // iterate over temp view and add all elements not in view
        curr = temp_view->head;
        while (curr != nullptr) {
            if (view->search(curr) == nullptr) {
                _copy_into(view, curr, false);  // copy from temporary view
                if (PyErr_Occurred()) {
                    return;
                }
            }
            curr = static_cast<Node*>(curr->next);
        }

    }

    /* Update a set or dictionary relative to a given sentinel value, appending
    items that are not already present. */
    template <typename View>
    inline void update_relative(
        View* view,
        PyObject* items,
        PyObject* sentinel,
        Py_ssize_t offset,
        bool reverse
    ) {
        _extend_relative(view, items, sentinel, offset, reverse, true);
    }

}


#endif // BERTRAND_STRUCTS_ALGORITHMS_UPDATE_H include guard
