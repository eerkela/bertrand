// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_RELATIVE_H
#define BERTRAND_STRUCTS_ALGORITHMS_RELATIVE_H

#include <cstddef>  // size_t
#include <tuple>  // std::tuple
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/bounds.h"  // relative_junction(), relative_neighbors()
#include "../core/node.h"  // has_prev<>
#include "../core/view.h"  // views
#include "extend.h"  // _extend_left_to_right(), _extend_right_to_left()
#include "insert.h"  // _insert_between()
#include "pop.h"  // _pop_node()


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    /* Get a value from a linked set or dictionary relative to a given sentinel
    value. */
    template <typename View>
    void get_relative(View* view, PyObject* sentinel, Py_ssize_t offset) {
        using Node = typename View::Node;

        // ensure offset is nonzero
        if (offset == 0) {
            PyErr_Format(PyExc_ValueError, "offset must be non-zero");
            return;
        } else if (offset < 0) {
            offset += 1;
        }

        // search for sentinel
        Node* curr = view->search(sentinel);
        if (curr == nullptr) {  // sentinel not found
            PyErr_Format(PyExc_KeyError, "%R is not contained in the set", sentinel);
            return;
        }

        // If we're iterating forward from the sentinel, then the process is the same
        // for singly- and doubly-linked lists
        if (offset > 0) {
            for (Py_ssize_t i = 0; i < offset; i++) {
                curr = static_cast<Node*>(curr->next);
                if (curr == nullptr) {
                    PyErr_Format(PyExc_IndexError, "offset %zd is out of range", offset);
                    return;
                }
            }
            Py_INCREF(curr->value);
            return curr->value;
        }

        // If we're iterating backwards and the list is doubly-linked, then we can
        // just use the `prev` pointer at each node
        if constexpr (has_prev<Node>::value) {
            for (Py_ssize_t i = 0; i > offset; i--) {
                curr = static_cast<Node*>(curr->prev);
                if (curr == nullptr) {
                    PyErr_Format(PyExc_IndexError, "offset %zd is out of range", offset);
                    return;
                }
            }
            Py_INCREF(curr->value);
            return curr->value;
        }

        // Otherwise, we have to start from the head and walk forward using a 2-pointer
        // approach.
        Node* lookahead = view->head;
        for (Py_ssize_t i = 0; i > offset; i--) {  // advance lookahead to offset
            lookahead = static_cast<Node*>(lookahead->next);
            if (lookahead == curr) {
                PyErr_Format(PyExc_IndexError, "offset %zd is out of range", offset);
                return;
            }
        }

        // advance both pointers until lookahead hits the end of the list
        Node* temp = view->head;
        while (lookahead != curr) {
            temp = static_cast<Node*>(temp->next);
            lookahead = static_cast<Node*>(lookahead->next);
        }
        Py_INCREF(temp->value);
        return temp->value;
    }

    /* Insert an item into a linked set or dictionary relative to a given sentinel
    value. */
    template <typename View>
    inline void insert_relative(
        View* view,
        PyObject* item,
        PyObject* sentinel,
        Py_ssize_t offset
    ) {
        _insert_relative(view, item, sentinel, offset, true);  // propagate errors
    }

    /* Add an item to a linked set or dictionary relative to a given sentinel
    value if it is not already present. */
    template <typename View>
    inline void add_relative(
        View* view,
        PyObject* item,
        PyObject* sentinel,
        Py_ssize_t offset
    ) {
        _insert_relative(view, item, sentinel, offset, false);  // suppress errors
    }

    /* Insert elements into a linked set or dictionary relative to the given
    sentinel value. */
    template <typename View>
    inline void extend_relative(
        View* view,
        PyObject* items,
        PyObject* sentinel,
        Py_ssize_t offset,
        bool reverse
    ) {
        // propagate errors
        _extend_relative(view, items, sentinel, offset, reverse, false);
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
        // suppress errors
        _extend_relative(view, items, sentinel, offset, reverse, true);
    }

    /* Remove an item from a linked set or dictionary relative to a given sentinel
    value. */
    template <typename View>
    inline void remove_relative(View* view, PyObject* sentinel, Py_ssize_t offset) {
        _drop_relative(view, sentinel, offset, true);  // propagate errors
    }

    /* Remove an item from a linked set or dictionary immediately after the
    specified sentinel value. */
    template <typename View>
    inline void discard_relative(View* view, PyObject* sentinel, Py_ssize_t offset) {
        _drop_relative(view, sentinel, offset, false);  // suppress errors
    }

    /* Pop an item from a linked list, set, or dictionary relative to a given
    sentinel value. */
    template <typename View>
    PyObject* pop_relative(View* view, PyObject* sentinel, Py_ssize_t offset) {
        using Node = typename View::Node;

        // ensure offset is nonzero
        if (offset == 0) {
            PyErr_SetString(PyExc_ValueError, "offset must be non-zero");
            return nullptr;
        }

        // search for sentinel
        Node* node = view->search(sentinel);
        if (node == nullptr) {
            PyErr_Format(PyExc_ValueError, "%R is not in the set", sentinel);
            return nullptr;
        }

        // walk according to offset
        std::tuple<Node*, Node*, Node*> bounds = relative_neighbors(
            view, node, offset, false
        );
        Node* prev = std::get<0>(bounds);
        Node* curr = std::get<1>(bounds);
        Node* next = std::get<2>(bounds);
        if (prev == nullptr  && curr == nullptr && next == nullptr) {
            // walked off end of list
            PyErr_Format(PyExc_IndexError, "offset %zd is out of range", offset);
            return nullptr;  // propagate
        }

        // pop node between boundaries
        return _pop_node(view, prev, curr, next);
    }

    /* Remove a sequence of items from a linked set or dictionary relative to a
    given sentinel value. */
    template <typename View>
    void clear_relative(
        View* view,
        PyObject* sentinel,
        Py_ssize_t offset,
        Py_ssize_t length
    ) {
        using Node = typename View::Node;

        // ensure offset is nonzero
        if (offset == 0) {
            PyErr_SetString(PyExc_ValueError, "offset must be non-zero");
            return;
        }

        // search for sentinel
        Node* curr = view->search(sentinel);
        if (curr == nullptr) {  // sentinel not found
            PyErr_Format(PyExc_KeyError, "%R is not contained in the set", sentinel);
            return;
        }

        // check for no-op
        if (length == 0) {
            return;
        }

        // If we're iterating forward from the sentinel, then the process is the same
        // for singly- and doubly-linked lists
        if (offset > 0) {
            _clear_forward(view, curr, offset, length);
            return;
        }

        // If we're iterating backwards and the list is doubly-linked, then we can
        // just use the `prev` pointer at each node
        if constexpr (has_prev<Node>::value) {
            _clear_backward_double(view, curr, offset, length);
            return;
        }

        // Otherwise, we have to start from the head and walk forward using a 2-pointer
        // approach.
        _clear_backward_single(view, curr, offset, length);
    }

}


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Implement both insert_relative() and add_relative() depending on error handling
flag. */
template <typename View>
void _insert_relative(
    View* view,
    PyObject* item,
    PyObject* sentinel,
    Py_ssize_t offset,
    bool update
) {
    using Node = typename View::Node;

    // ensure offset is nonzero
    if (offset == 0) {
        PyErr_Format(PyExc_ValueError, "offset must be non-zero");
        return;
    } else if (offset < 0) {
        offset += 1;
    }

    // search for sentinel
    Node* node = view->search(sentinel);
    if (node == nullptr) {  // sentinel not found
        PyErr_Format(PyExc_KeyError, "%R is not contained in the set", sentinel);
        return;
    }

    // walk according to offset
    std::pair<Node*,Node*> neighbors = relative_junction(
        view, node, offset, true
    );

    // insert node between neighbors
    _insert_between(view, neighbors.first, neighbors.second, item, update);
}


/* Implement both extend_relative() and update_relative() depending on error handling
flag. */
template <typename View>
void _extend_relative(
    View* view,
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
    Node* node = view->search(sentinel);
    if (node == nullptr) {  // sentinel not found
        if (!update) {
            PyErr_Format(PyExc_KeyError, "%R is not contained in the set", sentinel);
        }
        return;
    }

    // get neighbors for insertion
    // NOTE: truncate = true means we will never raise an error
    std::pair<Node*, Node*> bounds = relative_junction(view, node, offset, true);

    // insert items between left and right bounds
    if (reverse) {
        _extend_right_to_left(view, bounds.first, bounds.second, items, update);
    } else {
        _extend_left_to_right(view, bounds.first, bounds.second, items, update);
    }
}


/* Implement both remove_relative() and discard_relative() depending on error handling
flag. */
template <typename View>
void _drop_relative(View* view, PyObject* sentinel, Py_ssize_t offset, bool raise) {
    using Node = typename View::Node;

    // ensure offset is nonzero
    if (offset == 0) {
        PyErr_Format(PyExc_ValueError, "offset must be non-zero");
        return;
    } else if (offset < 0) {
        offset += 1;
    }

    // search for sentinel
    Node* node = view->search(sentinel);
    if (node == nullptr) {
        if (raise) {  // sentinel not found
            PyErr_Format(PyExc_KeyError, "%R is not contained in the set", sentinel);
        }
        return;
    }

    // walk according to offset
    std::tuple<Node*, Node*, Node*> neighbors = relative_neighbors(
        view, node, offset, false
    );
    Node* prev = std::get<0>(neighbors);
    Node* curr = std::get<1>(neighbors);
    Node* next = std::get<2>(neighbors);
    if (prev == nullptr  && curr == nullptr && next == nullptr) {
        if (raise) {  // walked off end of list
            PyErr_Format(PyExc_IndexError, "offset %zd is out of range", offset);
        }
        return;
    }

    // remove node between boundaries
    view->unlink(prev, curr, next);
    view->recycle(curr);
}


/* Helper for clearing a list relative to a given sentinel (iterating forwards). */
template <typename View, typename Node>
void _clear_forward(View* view, Node* curr, Py_ssize_t offset, Py_ssize_t length) {
    Node* prev = curr;
    curr = static_cast<Node*>(curr->next);
    for (Py_ssize_t i = 1; i < offset; i++) {  // advance curr to offset
        if (curr == nullptr) {
            return;  // do nothing
        }
        prev = curr;
        curr = static_cast<Node*>(curr->next);
    }
    if (length < 0) {  // clear to tail of list
        while (curr != nullptr) {
            Node* next = static_cast<Node*>(curr->next);
            view->unlink(prev, curr, next);
            view->recycle(curr);
            curr = next;
        }
    } else {  // clear up to length
        for (Py_ssize_t i = 0; i < length; i++) {
            if (curr == nullptr) {
                return;
            }
            Node* next = static_cast<Node*>(curr->next);
            view->unlink(static_cast<Node*>(curr->prev), curr, next);
            view->recycle(curr);
            curr = next;
        }
    }
}


/* Helper for clearing a list relative to a given sentinel (iterating backwards,
doubly-linked). */
template <typename View, typename Node>
void _clear_backward_double(
    View* view,
    Node* curr,
    Py_ssize_t offset,
    Py_ssize_t length
) {
    // advance curr to offset
    for (Py_ssize_t i = 0; i > offset; i--) {
        curr = static_cast<Node*>(curr->prev);
        if (curr == nullptr) {
            return;  // do nothing
        }
    }
    if (length < 0) {  // clear to head of list
        while (curr != nullptr) {
            Node* prev = static_cast<Node*>(curr->prev);
            view->unlink(prev, curr, static_cast<Node*>(curr->next));
            view->recycle(curr);
            curr = prev;
        }
    } else {  // clear up to length
        for (Py_ssize_t i = 0; i < length; i++) {
            if (curr == nullptr) {
                return;
            }
            Node* prev = static_cast<Node*>(curr->prev);
            view->unlink(prev, curr, static_cast<Node*>(curr->next));
            view->recycle(curr);
            curr = prev;
        }
    }
}


/* Helper for clearing a list relative to a given sentinel (iterating backwards,
singly-linked). */
template <typename View, typename Node>
void _clear_backward_single(
    View* view,
    Node* curr,
    Py_ssize_t offset,
    Py_ssize_t length
) {
    Node* lookahead = view->head;
    for (Py_ssize_t i = 0; i > offset; i--) {  // advance lookahead to offset
        lookahead = static_cast<Node*>(lookahead->next);
        if (lookahead == curr) {
            return;  // do nothing
        }
    }
    if (length < 0) {  // clear to head of list
        Node* temp = view->head;
        while (lookahead != curr) {
            Node* next = static_cast<Node*>(temp->next);
            view->unlink(nullptr, temp, next);
            view->recycle(temp);
            temp = next;
            lookahead = static_cast<Node*>(lookahead->next);
        }
    } else {
        // NOTE: the basic idea here is that we advance the lookahead pointer
        // by the length of the slice, and then advance both pointers until
        // we hit the sentinel.  When this happens, then the left pointer will
        // be pointing to the first node in the slice.  We then just delete
        // nodes 
        for (Py_ssize_t i = 0; i < length; i++) {  // advance lookahead by length
            lookahead = static_cast<Node*>(lookahead->next);
            if (lookahead == curr) {
                length = i;  // truncate length
                break;
            }
        }

        // advance both pointers until lookahead reaches sentinel
        Node* prev = nullptr;
        Node* temp = view->head;
        while (lookahead != curr) {
            prev = temp;
            temp = static_cast<Node*>(temp->next);
            lookahead = static_cast<Node*>(lookahead->next);
        }

        // delete nodes between boundaries
        for (Py_ssize_t i = 0; i < length; i++) {
            Node* next = static_cast<Node*>(temp->next);
            view->unlink(prev, temp, next);
            view->recycle(temp);
            temp = next;
        }
    }
}



#endif // BERTRAND_STRUCTS_ALGORITHMS_RELATIVE_H include guard
