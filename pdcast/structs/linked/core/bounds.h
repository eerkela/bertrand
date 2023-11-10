// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_BOUNDS_H
#define BERTRAND_STRUCTS_CORE_BOUNDS_H

#include <cstddef>  // size_t
#include <limits>  // std::numeric_limits
#include <tuple>  // std::tuple
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "node.h"  // has_prev<>


// NOTE: Sometimes we need to traverse a list from an arbitrary node, which
// might not be the head or tail of the list.  This is common when dealing with
// linked sets and dictionaries, where we maintain a hash table that gives
// constant-time access to any node in the list.  In these cases, we can save
// time by walking along the list relative to a sentinel node rather than
// blindly starting at the head or tail.


/* Get a node relative to another node within a linked list, set, or dictionary. */
template <typename View, typename Node>
Node* walk(View* view, Node* node, Py_ssize_t offset, bool truncate) {
    // check for no-op
    if (offset == 0) {
        return node;
    }

    // if we're traversing forward, then the process is the same for both
    // singly- and doubly-linked lists
    if (offset > 0) {
        Node* curr = node;
        for (Py_ssize_t i = 0; i < offset; i++) {
            if (curr == nullptr) {
                if (truncate) {
                    return view->tail;  // truncate to end of list
                } else {
                    return nullptr;  // index out of range
                }
            }
            curr = static_cast<Node*>(curr->next);
        }
        return curr;
    }

    // if the list is doubly-linked, then we can traverse backward just as easily
    if constexpr (has_prev<Node>::value) {
        Node* curr = node;
        for (Py_ssize_t i = 0; i > offset; i--) {
            if (curr == nullptr) {
                if (truncate) {
                    return view->head;  // truncate to beginning of list
                } else {
                    return nullptr;  // index out of range
                }
            }
            curr = static_cast<Node*>(curr->prev);
        }
        return curr;
    }

    // Otherwise, we have to iterate from the head of the list.  We do this using
    // a two-pointer approach where the `lookahead` pointer is offset from the
    // `curr` pointer by the specified number of steps.  When it reaches the
    // sentinel, then `curr` will be at the correct position.
    Node* lookahead = view->head;
    for (Py_ssize_t i = 0; i > offset; i--) {  // advance lookahead to offset
        if (lookahead == node) {
            if (truncate) {
                return view->head;  // truncate to beginning of list
            } else {
                return nullptr;  // index out of range
            }
        }
        lookahead = static_cast<Node*>(lookahead->next);
    }

    // advance both pointers until lookahead reaches sentinel
    Node* curr = view->head;
    while (lookahead != node) {
        curr = static_cast<Node*>(curr->next);
        lookahead = static_cast<Node*>(lookahead->next);
    }
    return curr;
}


/* Traverse a list relative to a given sentinel to find the left and right
bounds for an insertion. */
template <typename View, typename Node>
std::pair<Node*, Node*> relative_junction(
    View* view,
    Node* sentinel,
    Py_ssize_t offset,
    bool truncate
) {
    // get the previous node for the insertion point
    Node* prev = walk(view, sentinel, offset - 1, false);

    // apply truncate rule
    if (prev == nullptr) {  // walked off end of list
        if (!truncate) {
            return std::make_pair(nullptr, nullptr);  // error code
        }
        if (offset < 0) {
            return std::make_pair(nullptr, view->head);  // beginning of list
        }
        return std::make_pair(view->tail, nullptr);  // end of list
    }

    // return the previous node and its successor
    Node* next = static_cast<Node*>(prev->next);
    return std::make_pair(prev, next);
}


/* Traverse a list relative to a given sentinel to find the left and right
bounds for a removal */
template <typename View, typename Node>
std::tuple<Node*, Node*, Node*> relative_neighbors(
    View* view,
    Node* sentinel,
    Py_ssize_t offset,
    bool truncate
) {
    // NOTE: we can't reuse relative_junction() here because we need access to
    // the node preceding the tail in the event that we walk off the end of the
    // list and truncate=true.
    Node* prev;
    Node* curr = sentinel;
    Node* next;

    // NOTE: this is trivial for doubly-linked lists
    if constexpr (has_prev<Node>::value) {
        if (offset > 0) {  // forward traversal
            next = static_cast<Node*>(curr->next);
            for (Py_ssize_t i = 0; i < offset; i++) {
                if (next == nullptr) {
                    if (truncate) {
                        break;  // truncate to end of list
                    } else {
                        return std::make_tuple(nullptr, nullptr, nullptr);
                    }
                }
                curr = next;
                next = static_cast<Node*>(curr->next);
            }
            prev = static_cast<Node*>(curr->prev);
        } else {  // backward traversal
            prev = static_cast<Node*>(curr->prev);
            for (Py_ssize_t i = 0; i > offset; i--) {
                if (prev == nullptr) {
                    if (truncate) {
                        break;  // truncate to beginning of list
                    } else {
                        return std::make_tuple(nullptr, nullptr, nullptr);
                    }
                }
                curr = prev;
                prev = static_cast<Node*>(curr->prev);
            }
            next = static_cast<Node*>(curr->next);
        }
        return std::make_tuple(prev, curr, next);
    }

    // NOTE: It gets significantly more complicated if the list is singly-linked.
    // In this case, we can only optimize the forward traversal branch if we're
    // advancing at least one node and the current node is not the tail of the
    // list.
    if (truncate && offset > 0 && curr == view->tail) {
        offset = 0;  // skip forward iteration branch
    }

    // forward iteration (efficient)
    if (offset > 0) {
        prev = nullptr;
        next = static_cast<Node*>(curr->next);
        for (Py_ssize_t i = 0; i < offset; i++) {
            if (next == nullptr) {  // walked off end of list
                if (truncate) {
                    break;
                } else {
                    return std::make_tuple(nullptr, nullptr, nullptr);
                }
            }
            if (prev == nullptr) {
                prev = curr;
            }
            curr = next;
            next = static_cast<Node*>(curr->next);
        }
        return std::make_tuple(prev, curr, next);
    }

    // backward iteration (inefficient)
    Node* lookahead = view->head;
    for (size_t i = 0; i > offset; i--) {  // advance lookahead to offset
        if (lookahead == curr) {
            if (truncate) {  // truncate to beginning of list
                next = static_cast<Node*>(view->head->next);
                return std::make_tuple(nullptr, view->head, next);
            } else {  // index out of range
                return std::make_tuple(nullptr, nullptr, nullptr);
            }
        }
        lookahead = static_cast<Node*>(lookahead->next);
    }

    // advance both pointers until lookahead reaches sentinel
    prev = nullptr;
    Node* temp = view->head;
    while (lookahead != curr) {
        prev = temp;
        temp = static_cast<Node*>(temp->next);
        lookahead = static_cast<Node*>(lookahead->next);
    }
    next = static_cast<Node*>(temp->next);
    return std::make_tuple(prev, temp, next);
}


#endif  // BERTRAND_STRUCTS_CORE_BOUNDS_H include guard
