// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_RELATIVE_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_RELATIVE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_RELATIVE_H



namespace bertrand {
namespace structs {
namespace linked {


namespace Relative {


    /* Add an item to a linked set or dictionary relative to a given sentinel
    value if it is not already present. */
    template <typename View>
    inline void add_relative(
        View* view,
        PyObject* item,
        PyObject* sentinel,
        Py_ssize_t offset
    ) {
        _insert_relative(view, item, sentinel, offset, true);
    }


    /* Remove an item from a linked set or dictionary relative to a given sentinel
    value. */
    template <typename View>
    inline void remove_relative(View& view, PyObject* sentinel, Py_ssize_t offset) {
        _drop_relative(view, sentinel, offset, true);  // propagate errors
    }


    /* Remove an item from a linked set or dictionary immediately after the
    specified sentinel value. */
    template <typename View>
    void discard_relative(View* view, PyObject* sentinel, Py_ssize_t offset) {
        _drop_relative(view, sentinel, offset, false);  // suppress errors
    }




    /* Move an item within a linked set or dictionary relative to a given sentinel
    value. */
    template <typename View>
    void move_relative(
        View* view,
        PyObject* item,
        PyObject* sentinel,
        Py_ssize_t offset
    ) {
        using Node = typename View::Node;

        // check for trivial case
        int comp = PyObject_RichCompareBool(item, sentinel, Py_EQ);
        if (comp == -1) {
            return;  // propagate error
        } else if (comp == 1) {  // this devolves into a simple move()
            move(view, item, offset);
            return;
        }

        // search for item in hash table
        Node* node = view->search(item);
        if (node == nullptr) {
            PyErr_Format(PyExc_KeyError, "%R is not in the set", item);
            return;
        }

        // search for sentinel in hash table
        Node* sentinel_node = view->search(sentinel);
        if (sentinel_node == nullptr) {
            PyErr_Format(PyExc_KeyError, "%R is not in the set", sentinel);
            return;
        }

        // get prev pointers at both insertion and removal point
        Node* old_prev;
        Node* old_next = static_cast<Node*>(node->next);
        Node* new_prev;
        Node* new_next;

        // NOTE: if the list is doubly-linked, then we can use the `prev` pointer
        // to get the previous node in constant time.
        if constexpr (has_prev<Node>::value) {
            // NOTE: because we're moving relative to some other node, we can
            // run into a situation where the junction includes the node to be
            // moved, which can cause an error.  If we remove the node before
            // searching for the junction, then we can avoid this problem.
            old_prev = static_cast<Node*>(node->prev);
            view->unlink(old_prev, node, old_next);
            std::pair<Node*, Node*> bounds = relative_junction(
                view, sentinel_node, offset, true
            );
            new_prev = bounds.first;
            new_next = bounds.second;

        // NOTE: otherwise, we have to iterate from the head of the list.  If
        // we're careful, we can do this in a single traversal for both the old
        // and new pointers, without having to repeat any work.
        } else {
            if (offset > 0) {
                // advance to sentinel, recording the original node if we encounter it.
                new_prev = nullptr;
                new_next = view->head;
                bool found = false;
                while (new_next != sentinel_node) {
                    if (new_next == node) {
                        old_prev = new_prev;
                        found = true;
                    } else {
                        new_prev = new_next;
                    }
                    new_next = static_cast<Node*>(new_next->next);
                }

                // continue out to offset to find insertion point
                for (Py_ssize_t i = 0; i < offset; i++) {
                    if (new_next == nullptr) {
                        break;  // truncate to end of list
                    } else if (new_next == node) {
                        old_prev = new_prev;
                        found = true;
                    } else {
                        new_prev = new_next;
                    }
                    new_next = static_cast<Node*>(new_next->next);
                }

                // if we still haven't found the original node, then we need to
                // continue iterating until we do
                if (!found) {
                    Node* temp = new_next;
                    while (temp != node) {
                        old_prev = temp;
                        temp = static_cast<Node*>(temp->next);
                    }
                }
            } else {
                // create lookahead pointer
                Node* temp = nullptr;
                Node* lookahead = view->head;
                bool found = false;
                bool truncate = false;
                for (Py_ssize_t i = 0; i > offset; i--) {
                    if (lookahead == sentinel_node) {  // truncate to beginning of list
                        new_prev = nullptr;
                        new_next = view->head;
                        truncate = true;
                        break;
                    } else if (lookahead == node) {
                        old_prev = temp;
                        found = true;
                    }
                    temp = lookahead;
                    lookahead = static_cast<Node*>(lookahead->next);
                }

                // if we didn't truncate, then we advance both pointers until
                // we find the removal point
                if (!truncate) {
                    new_prev = view->head;
                    while (lookahead != sentinel_node) {
                        if (lookahead == node) {
                            old_prev = temp;
                            found = true;
                        } else {
                            new_prev = static_cast<Node*>(new_prev->next);
                        }
                        temp = lookahead;
                        lookahead = static_cast<Node*>(lookahead->next);
                    }
                    new_next = static_cast<Node*>(new_prev->next);
                }

                // if we still haven't found the original node, then we need to
                // continue iterating until we do
                if (!found) {
                    temp = new_next;
                    while (temp != node) {
                        old_prev = temp;
                        temp = static_cast<Node*>(temp->next);
                    }
                }
            }

            // remove node from original position
            view->unlink(old_prev, node, old_next);
        }

        // insert node at new position
        view->link(new_prev, node, new_next);
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


///////////////////////
////    PRIVATE    ////
///////////////////////


// NOTE: these are reused for discard() as well


/* Implement both remove_relative() and discard_relative() depending on error handling
flag. */
template <typename View>
void _drop_relative(View& view, PyObject* sentinel, Py_ssize_t offset, bool raise) {
    using Node = typename View::Node;

    // ensure offset is nonzero
    if (offset == 0) {
        PyErr_Format(PyExc_ValueError, "offset must be non-zero");
        return;
    } else if (offset < 0) {
        offset += 1;
    }

    // search for sentinel
    Node* node = view.search(sentinel);
    if (node == nullptr) {
        if (raise) {  // sentinel not found
            PyErr_Format(PyExc_KeyError, "%R is not contained in the set", sentinel);
        }
        return;
    }

    // walk according to offset
    std::tuple<Node*, Node*, Node*> neighbors = relative_neighbors(
        &view, node, offset, false
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
    view.unlink(prev, curr, next);
    view.recycle(curr);
}



    // /* Pop an item from a linked list, set, or dictionary relative to a given
    // sentinel value. */
    // template <typename View>
    // PyObject* pop_relative(View& view, PyObject* sentinel, Py_ssize_t offset) {
    //     using Node = typename View::Node;

    //     // ensure offset is nonzero
    //     if (offset == 0) {
    //         PyErr_SetString(PyExc_ValueError, "offset must be non-zero");
    //         return nullptr;
    //     }

    //     // search for sentinel
    //     Node* node = view.search(sentinel);
    //     if (node == nullptr) {
    //         PyErr_Format(PyExc_ValueError, "%R is not in the set", sentinel);
    //         return nullptr;
    //     }

    //     // walk according to offset
    //     std::tuple<Node*, Node*, Node*> bounds = relative_neighbors(
    //         &view, node, offset, false
    //     );
    //     Node* prev = std::get<0>(bounds);
    //     Node* curr = std::get<1>(bounds);
    //     Node* next = std::get<2>(bounds);
    //     if (prev == nullptr  && curr == nullptr && next == nullptr) {
    //         // walked off end of list
    //         PyErr_Format(PyExc_IndexError, "offset %zd is out of range", offset);
    //         return nullptr;  // propagate
    //     }

    //     // pop node between boundaries
    //     return _pop_node(view, prev, curr, next);
    // }


///////////////////////
////    PRIVATE    ////
///////////////////////


// /* Unlink and remove a node and return its value. */
// template <typename View, typename Node>
// inline PyObject* _pop_node(View& view, Node* prev, Node* curr, Node* next) {
//     // get return value
//     PyObject* value = curr->value;
//     Py_INCREF(value);  // have to INCREF because we DECREF in recycle()

//     // unlink and deallocate node
//     view.unlink(prev, curr, next);
//     view.recycle(curr);
//     return value;  // caller takes ownership of value
// }



// namespace Relative {

//     /* Insert an item into a linked set or dictionary relative to a given sentinel
//     value. */
//     template <typename RelativeProxy>
//     inline void insert(RelativeProxy* proxy, PyObject* item) {
//         _insert_relative(proxy, item, false);  // propagate errors
//     }

// }


// ///////////////////////
// ////    PRIVATE    ////
// ///////////////////////


// // NOTE: these are reused for add_relative() as well


// /* Attempt to insert a node between the left and right neighbors. */
// template <typename View, typename Node>
// void _insert_between(
//     View& view,
//     Node* left,
//     Node* right,
//     PyObject* item,
//     bool update
// ) {
//     // allocate a new node
//     Node* curr = view.node(item);
//     if (curr == nullptr) {
//         return;  // propagate error
//     }

//     // check if we should update an existing node
//     if constexpr (is_setlike<View>::value) {
//         if (update) {
//             Node* existing = view.search(curr);
//             if (existing != nullptr) {  // item already exists
//                 if constexpr (has_mapped<Node>::value) {
//                     // update mapped value
//                     Py_DECREF(existing->mapped);
//                     Py_INCREF(curr->mapped);
//                     existing->mapped = curr->mapped;
//                 }
//                 view.recycle(curr);
//                 return;
//             }
//         }
//     }

//     // insert node between neighbors
//     view.link(left, curr, right);
//     if (PyErr_Occurred()) {
//         view.recycle(curr);  // clean up staged node before propagating
//     }
// }


// /* Implement both insert_relative() and add_relative() depending on error handling
// flag. */
// template <typename RelativeProxy>
// void _insert_relative(RelativeProxy* proxy, PyObject* item, bool update) {
//     using Node = typename RelativeProxy::Node;

//     // ensure offset is nonzero
//     if (proxy->offset == 0) {
//         PyErr_Format(PyExc_ValueError, "offset must be non-zero");
//         return;
//     } else if (proxy->offset < 0) {
//         proxy->offset += 1;
//     }

//     // walk according to offset
//     // std::pair<Node*,Node*> neighbors = relative_junction(
//     //     proxy->view, proxy->sentinel, proxy->offset, true
//     // );
//     std::pair<Node*, Node*> neighbors = proxy->junction(proxy->offset, true);

//     // insert node between neighbors
//     _insert_between(proxy->view, neighbors.first, neighbors.second, item, update);
// }


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



}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_RELATIVE_H include guard
