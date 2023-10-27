// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_RELATIVE_H
#define BERTRAND_STRUCTS_ALGORITHMS_RELATIVE_H

#include <cstddef>  // size_t
#include <Python.h>  // CPython API
#include "../core/node.h"  // NodeTraits


namespace bertrand {
namespace structs {
namespace linked {
namespace algorithms {


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace list {

    template <typename View>
    inline void clear(View& view) {
        view.clear();
    }

}  // namespace list


namespace Relative {

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
        if constexpr (NodeTraits<Node>::has_prev) {
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
            view->unlink(prev, curr, next);
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


}  // namespace algorithms
}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_RELATIVE_H include guard
