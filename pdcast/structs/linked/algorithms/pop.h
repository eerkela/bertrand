// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_POP_H
#define BERTRAND_STRUCTS_ALGORITHMS_POP_H

#include <tuple>  // std::tuple
#include <type_traits>  // std::enable_if_t<>
#include <Python.h>  // CPython API
#include "../core/view.h"  // ViewTraits
#include "position.h"  // position()


namespace bertrand {
namespace structs {
namespace linked {


    /* Pop an item from a linked list, set, or dictionary at the given index. */
    template <typename View, typename Index>
    inline auto pop(View& view, Index index)
        -> std::enable_if_t<ViewTraits<View>::listlike, typename View::Value>
    {
        return algorithms::list::position(view, index).pop();
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


    /* Pop a key from a linked dictionary and return its corresponding value. */
    template <typename View>
    PyObject* pop(
        View& view,
        PyObject* key,
        PyObject* default_value
    ) {
        using Node = typename View::Node;
        Node* prev;
        Node* curr;

        // search for node
        curr = view.search(key);
        if (curr == nullptr) {
            return default_value;
        }

        // get neighboring nodes
        if constexpr (View::doubly_linked) {
            // NOTE: this is O(1) for doubly-linked dictionaries because we can use
            // the node's prev and next pointers to unlink it from the list.
            prev = static_cast<Node*>(curr->prev);
        } else {
            // NOTE: this is O(n) for singly-linked dictionaries because we have to
            // traverse the whole list to find the node that precedes the popped node.
            prev = nullptr;
            Node* temp = view.head;
            while (temp != curr) {
                prev = temp;
                temp = static_cast<Node*>(temp->next);
            }
        }

        // recycle node and return a new reference to its value
        Node* next = static_cast<Node*>(curr->next);
        return _pop_node(view, prev, curr, next);
    }



///////////////////////
////    PRIVATE    ////
///////////////////////


/* Unlink and remove a node and return its value. */
template <typename View, typename Node>
inline PyObject* _pop_node(View& view, Node* prev, Node* curr, Node* next) {
    // get return value
    PyObject* value = curr->value;
    Py_INCREF(value);  // have to INCREF because we DECREF in recycle()

    // unlink and deallocate node
    view.unlink(prev, curr, next);
    view.recycle(curr);
    return value;  // caller takes ownership of value
}


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_POP_H include guard
