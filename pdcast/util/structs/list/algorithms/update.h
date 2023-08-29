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
