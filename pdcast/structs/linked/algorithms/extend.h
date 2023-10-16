// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_EXTEND_H
#define BERTRAND_STRUCTS_ALGORITHMS_EXTEND_H

#include <Python.h>  // CPython API
#include "append.h"  // append()
#include "../../util/python.h"  // PyIterable

#include "../view.h"  // ViewTraits

// bertrand::structs::linked::List()
// bertrand::structs::linked::ListView()
// bertrand::structs::linked::ListAllocator()
// bertrand::structs::linked::Set()
// bertrand::structs::linked::Dict()
// bertrand::structs::linked::extend(list, items, left)


#include "../../util/iter.h" // iter()


#include <vector>  // std::vector


namespace bertrand {
namespace structs {
namespace linked {
namespace algorithms {


namespace list {

    /* Add multiple items to the end of a list, set, or dictionary. */
    template <typename ListLike, typename Iterable>
    inline void extend(ListLike& list, Iterable& items, bool left) {
        using Node = typename ListLike::Node;
        using util::iter;

        // note original head/tail in case of error
        Node* original;
        if (left) {
            original = list.view.head();
        } else {
            original = list.view.tail();
        }

        // proceed with extend
        try {
            for (auto item : iter(items)) {
                append(list, item, left);
            }

        // if an error occurs, clean up any nodes that were added to the list
        } catch (...) {
            if (left) {
                // if we linked to head, just remove until we reach the original head
                Node* curr = list.view.head();
                while (curr != original) {
                    Node* next = curr->next();
                    list.view.unlink(nullptr, curr, next);
                    list.view.recycle(curr);
                    curr = next;
                }
            } else {
                // otherwise, start from original tail and remove until end of list
                Node* curr = original->next();
                while (curr != nullptr) {
                    Node* next = curr->next();
                    list.view.unlink(original, curr, next);
                    list.view.recycle(curr);
                    curr = next;
                }
            }
            throw;  // propagate
        }
    }

}


namespace set {

    /* Insert elements into a linked set or dictionary relative to the given sentinel
    value. */
    template <typename View>
    inline void extend_relative(
        View& view,
        PyObject* items,
        PyObject* sentinel,
        Py_ssize_t offset,
        bool reverse
    ) {
        _extend_relative(view, items, sentinel, offset, reverse, false);
    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


// NOTE: these are reused for update() and update_relative() as well


/* Insert items from an arbitrary Python iterable from the left node to the right
node. */
template <typename View, typename Node>
void _extend_left_to_right(
    View& view,
    Node* left,
    Node* right,
    PyObject* items,
    const bool update
) {
    // CPython API equivalent of `iter(items)`
    PyObject* iterator = PyObject_GetIter(items);
    if (iterator == nullptr) {  // TypeError() during iter()
        return;
    }

    Node* prev = left;
    Node* curr;

    // CPython API equivalent of `for item in items:`
    while (true) {
        PyObject* item = PyIter_Next(iterator);  // next(iterator)
        if (item == nullptr) {  // end of iterator or error
            break;
        }

        // allocate a new node
        curr = view.node(item);
        if (curr == nullptr) {
            Py_DECREF(item);
            break;  // enter undo branch
        }

        // check if we should update existing nodes
        if constexpr (ViewTraits<View>::is_setlike) {
            if (update) {
                Node* existing = view.search(curr);
                if (existing != nullptr) {  // item already exists
                    if constexpr (NodeTraits<Node>::has_mapped) {
                        Py_DECREF(existing->mapped);
                        Py_INCREF(curr->mapped);
                        existing->mapped = curr->mapped;
                    }
                    view.recycle(curr);
                    Py_DECREF(item);
                    continue;  // advance to next item without updating `prev`
                }
            }
        }

        // insert from left to right
        view.link(prev, curr, right);
        if (PyErr_Occurred()) {  // ValueError() item is already in list
            Py_DECREF(item);
            break;  // enter undo branch
        }

        // advance to next item
        prev = curr;
        Py_DECREF(item);
    }

    // release iterator
    Py_DECREF(iterator);

    // check for error
    if (PyErr_Occurred()) {  // recover original list
        _undo_left_to_right(view, left, right);
    }
}


/* Insert items from an arbitrary Python iterable from the right node to the left
node. */
template <typename View, typename Node>
void _extend_right_to_left(
    View& view,
    Node* left,
    Node* right,
    PyObject* items,
    const bool update
) {
    // CPython API equivalent of `iter(items)`
    PyObject* iterator = PyObject_GetIter(items);
    if (iterator == nullptr) {  // TypeError() during iter()
        return;
    }

    Node* next = right;
    Node* curr;

    // CPython API equivalent of `for item in items:`
    while (true) {
        PyObject* item = PyIter_Next(iterator);  // next(iterator)
        if (item == nullptr) {  // end of iterator or error
            break;
        }

        // allocate a new node
        curr = view.node(item);
        if (curr == nullptr) {  // error during node allocation
            Py_DECREF(item);
            break;  // enter undo branch
        }

        // check if we should update existing nodes
        if constexpr (ViewTraits<View>::is_setlike) {
            if (update) {
                Node* existing = view.search(curr);
                if (existing != nullptr) {  // item already exists
                    if constexpr (NodeTraits<Node>::has_mapped) {
                        Py_DECREF(existing->mapped);
                        Py_INCREF(curr->mapped);
                        existing->mapped = curr->mapped;
                    }
                    view.recycle(curr);
                    Py_DECREF(item);
                    continue;  // advance to next item without updating `next`
                }
            }
        }

        // insert from right to left
        view.link(left, curr, next);
        if (PyErr_Occurred()) {  // error during list insertion
            Py_DECREF(item);
            break;  // enter undo branch
        }

        // advance to next item
        next = curr;
        Py_DECREF(item);
    }

    // release iterator
    Py_DECREF(iterator);

    // check for error
    if (PyErr_Occurred()) {  // recover original list
        _undo_right_to_left(view, left, right);
    }
}


/* Implement both extend_relative() and update_relative() depending on error handling
flag. */
template <typename View>
void _extend_relative(
    View& view,
    PyObject* items,
    PyObject* sentinel,
    Py_ssize_t offset,
    bool reverse,
    bool update
) {
    using Node = typename View::Node;

    // ensure offset is nonzero
    if (offset == 0) {
        PyErr_SetString(PyExc_ValueError, "offset must be non-zero");
        return;
    }

    // search for sentinel
    Node* node = view.search(sentinel);
    if (node == nullptr) {  // sentinel not found
        if (!update) {
            PyErr_Format(PyExc_KeyError, "%R is not contained in the set", sentinel);
        }
        return;
    }

    // get neighbors for insertion
    // NOTE: truncate = true means we will never raise an error
    std::pair<Node*, Node*> bounds = relative_junction(&view, node, offset, true);

    // insert items between left and right bounds
    if (reverse) {
        _extend_right_to_left(view, bounds.first, bounds.second, items, update);
    } else {
        _extend_left_to_right(view, bounds.first, bounds.second, items, update);
    }
}


/* Recover the original list in the event of error during extend()/update(). */
template <typename View, typename Node>
void _undo_left_to_right(
    View& view,
    Node* left,
    Node* right
) {
    // remove staged nodes from left to right
    Node* prev = left;
    Node* curr = static_cast<Node*>(prev->next);
    while (curr != right) {
        Node* next = static_cast<Node*>(curr->next);
        view.unlink(prev, curr, next);
        view.recycle(curr);
        curr = next;
    }

    // join left and right bounds
    Node::join(left, right);
    if (right == nullptr) {
        view.tail = right;  // reset tail if necessary
    }
}


/* Recover the original list in the event of error during extend()/update(). */
template <typename View, typename Node>
void _undo_right_to_left(
    View& view,
    Node* left,
    Node* right
) {
    // NOTE: the list isn't guaranteed to be doubly-linked, so we have to
    // iterate from left to right to delete the staged nodes.
    Node* prev;
    if (left == nullptr) {
        prev = view.head;
    } else {
        prev = left;
    }

    // remove staged nodes from left to right bounds
    Node* curr = static_cast<Node*>(prev->next);
    while (curr != right) {
        Node* next = static_cast<Node*>(curr->next);
        view.unlink(prev, curr, next);
        view.recycle(curr);
        curr = next;
    }

    // join left and right bounds (can be NULL)
    Node::join(left, right);
    if (left == nullptr) {
        view.head = left;  // reset head if necessary
    }
}


}  // namespace algorithms
}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_EXTEND_H include guard
