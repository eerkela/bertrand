// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H
#define BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H

#include <optional>  // std::optional
#include <Python.h>  // CPython API
#include "../core/view.h"  // views


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    ///////////////////////////////
    ////    SET COMPARISONS    ////
    ///////////////////////////////

    /* Check whether a linked set or dictionary has any elements in common with
    another set or dictionary. */
    template <typename View>
    int isdisjoint(View* view1, View* view2) {
        using Node = typename View::Node;

        // iterate over view1 and check if any elements are in view2
        Node* curr = view1->head;
        while (curr != nullptr) {
            if (view2->search(curr) != nullptr) {
                return 0;
            }
            curr = static_cast<Node*>(curr->next);
        }
        return 1;
    }

    /* A specialization for isdisjoint() that converts a Python iterable into an
    appropriate view before continuing as normal. */
    template <typename View>
    int isdisjoint(View* view1, PyObject* iterable) {
        auto result = _unpack_python(isdisjoint, view1, iterable);
        if (!result.has_value()) {
            return -1;  // propagate error
        }
        return result.value();
    }

    /* Check whether a linked set or dictionary is a subset of another set or
    dictionary. */
    template <typename View>
    int issubset(View* view1, View* view2, bool strict) {
        using Node = typename View::Node;

        // iterate over view1 and check if all elements are in view2
        Node* curr = view1->head;
        while (curr != nullptr) {
            if (view2->search(curr) == nullptr) {
                return 0;
            }
            curr = static_cast<Node*>(curr->next);
        }

        // if strict, check that view1 is not equal to view2
        if (strict) {
            return view1->size < view2->size;
        }
        return 1;
    }

    /* A specialization for issubset() that converts a Python iterable into an
    appropriate view before continuing as normal. */
    template <typename View>
    int issubset(View* view1, PyObject* iterable, bool strict) {
        auto result = _unpack_python(issubset, view1, iterable, strict);
        if (!result.has_value()) {
            return -1;  // propagate error
        }
        return result.value();
    }

    /* Check whether a linked set or dictionary is a superset of another set or
    dictionary. */
    template <typename View>
    int issuperset(View* view1, View* view2, bool strict) {
        using Node = typename View::Node;

        // iterate over view2 and check if all elements are in view1
        Node* curr = view2->head;
        while (curr != nullptr) {
            if (view1->search(curr) == nullptr) {
                return 0;
            }
            curr = static_cast<Node*>(curr->next);
        }

        // if strict, check that view1 is not equal to view2
        if (strict) {
            return view1->size > view2->size;
        }
        return 1;
    }

    /* A specialization for issuperset() that converts a Python iterable into an
    appropriate view before continuing as normal. */
    template <typename View>
    int issuperset(View* view1, PyObject* iterable, bool strict) {
        auto result = _unpack_python(issuperset, view1, iterable, strict);
        if (!result.has_value()) {
            return -1;  // propagate error
        }
        return result.value();
    }

    //////////////////////////////
    ////    SET OPERATIONS    ////
    //////////////////////////////

    /* Get the union between two linked sets or dictionaries. */
    template <typename View>
    View* union_(View* view1, View* view2) {
        using Node = typename View::Node;

        // copy view1
        View* result = view1->copy();
        if (result == nullptr) {
            return nullptr;  // propagate error
        }

        // iterate over view2 and add all elements that are missing from result
        Node* curr = view2->head;
        while (curr != nullptr) {
            if (result->search(curr) == nullptr) {
                _copy_into(result, curr);
                if (PyErr_Occurred()) {
                    delete result;
                    return nullptr;
                }
            }
            curr = static_cast<Node*>(curr->next);
        }

        return result;
    }

    /* A specialization for union_() that converts a Python iterable into an
    appropriate view before continuing as normal. */
    template <typename View>
    View* union_(View* view1, PyObject* iterable) {
        try {
            View view2(iterable);
            return union_(view1, &view2);
        } catch (const std::bad_alloc&) {
            PyErr_NoMemory();
            return nullptr;
        }
    }

    /* Get the difference between two linked sets or dictionaries. */
    template <typename View>
    View* difference(View* view1, View* view2) {
        using Node = typename View::Node;

        // generate a new view to hold the result
        View* result;
        try {
            result = new View(view1->max_size, view1->specialization);
        } catch (const std::bad_alloc&) {
            PyErr_NoMemory();
            return nullptr;
        }

        // iterate over view1 and add all elements not in view2 to result
        Node* curr = view1->head;
        while (curr != nullptr) {
            if (view2->search(curr) == nullptr) {
                _copy_into(result, curr);
                if (PyErr_Occurred()) {
                    delete result;
                    return nullptr;
                }
            }
            curr = static_cast<Node*>(curr->next);
        }

        return result;
    }

    /* A specialization for difference() that converts a Python iterable into an
    appropriate view before continuing as normal. */
    template <typename View>
    View* difference(View* view1, PyObject* iterable) {
        try {
            View view2(iterable);
            return difference(view1, &view2);
        } catch (const std::bad_alloc&) {
            PyErr_NoMemory();
            return nullptr;
        }
    }

    /* Get the intersection between two linked sets or dictionaries. */
    template <typename View>
    View* intersection(View* view1, View* view2) {
        using Node = typename View::Node;

        // generate a new view to hold the result
        View* result;
        try {
            result = new View(view1->max_size, view1->specialization);
        } catch (const std::bad_alloc&) {
            PyErr_NoMemory();
            return nullptr;
        }

        // iterate over view1 and add all elements in view2 to result
        Node* curr = view1->head;
        while (curr != nullptr) {
            if (view2->search(curr) != nullptr) {
                _copy_into(result, curr);
                if (PyErr_Occurred()) {
                    delete result;
                    return nullptr;
                }
            }
            curr = static_cast<Node*>(curr->next);
        }

        return result;
    }

    /* Get the symmetric difference between two linked sets or dictionaries. */
    template <typename View>
    View* symmetric_difference(View* view1, View* view2) {
        using Node = typename View::Node;

        // (A - B)
        View* diff1 = difference(view1, view2, false);
        if (diff1 == nullptr) {
            return nullptr;  // propagate error
        }

        // (B - A)
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
    void update(View* view1, View* view2) {
        using Node = typename View::Node;

        // iterate over view2 and add all unique elements to view1
        Node* curr = view2->head;
        while (curr != nullptr) {
            if (view1->search(curr) == nullptr) {
                _copy_into(view1, curr);
                if (PyErr_Occurred()) {
                    return;
                }
            }
            curr = static_cast<Node*>(curr->next);
        }
    }

    /* Update a linked set or dictionary in-place, removing elements from a second
    set or dictionary. */
    template <typename View>
    void difference_update(View* view1, View* view2) {
        using Node = typename View::Node;

        // iterate over view1 and remove all elements in view2
        Node* prev = nullptr;
        Node* curr = view1->head;
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


// TODO: use function pointers for PyObject* based specializations.


/* Copy a node from one view to another. */
template <typename View, typename Node>
void _copy_into(View* view, Node* node) {
    Node* copied = view->copy(node);
    if (copied == nullptr) {  // check for errors during init_copy()
        return;  // propagate
    }

    // add node to result
    view->link(view->tail, copied, nullptr);
    if (PyErr_Occurred()) {  // check for errors during link()
        view->recycle(copied);
        return;  // propagate
    }
}


/* Convert a Python iterable into an appropriate view before calling the associated
function. */
template <typename Func, typename View, typename... Args>
auto _unpack_python(Func func, View* view, PyObject* iterable, Args... args)
    -> std::optional<decltype(func(view, std::declval<View*>(), args...))>
{
    try {
        View view2(iterable);
        return {func(view, &view2, args...)};
    } catch (const std::bad_alloc&) {
        PyErr_NoMemory();
        return std::nullopt;
    }
}



#endif // BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H include guard
