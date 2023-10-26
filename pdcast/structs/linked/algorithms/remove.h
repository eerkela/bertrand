// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_REMOVE_H
#define BERTRAND_STRUCTS_ALGORITHMS_REMOVE_H

#include <Python.h>  // CPython API
#include <sstream>  // std::ostringstream
#include <stdexcept>  // std::invalid_argument
#include "../../util/iter.h"  // iter()
#include "../../util/repr.h"  // repr()
#include "../node.h"  // NodeTraits


namespace bertrand {
namespace structs {
namespace linked {
namespace algorithms {


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace list {

    /* Remove the first occurrence of an item from a linked list. */
    template <typename View>
    void remove(View& view, PyObject* item) {
        using Node = typename View::Node;
        using util::iter;

        // find item in list
        for (auto it = iter(view).iter(); it != it.end(); ++it) {
            Node* node = it.curr();
            if (node->eq(item)) {
                view.recycle(it.drop());
                return;
            }
        }

        // item not found
        std::ostringstream msg;
        msg << util::repr(item) << " is not in list";
        throw std::invalid_argument(msg.str());  
    }

}  // namespace list


namespace set {

    /* Remove an item from a linked set or dictionary. */
    template <typename View>
    inline void remove(View& view, PyObject* item) {
        _drop_setlike(view, item, true);  // propagate errors
    }

    /* Remove an item from a linked set or dictionary relative to a given sentinel
    value. */
    template <typename View>
    inline void remove_relative(View& view, PyObject* sentinel, Py_ssize_t offset) {
        _drop_relative(view, sentinel, offset, true);  // propagate errors
    }

}  // namespace set


///////////////////////
////    PRIVATE    ////
///////////////////////


// NOTE: these are reused for discard() as well


/* Implement both remove() and discard() for sets and dictionaries depending on error
handling flag. */
template <typename View>
void _drop_setlike(View& view, PyObject* item, bool raise) {
    using Node = typename View::Node;

    // search for node
    Node* curr = view.search(item);
    if (curr == nullptr) {  // item not found
        if (raise) {
            PyErr_Format(PyExc_KeyError, "%R not in set", item);
        }
        return;
    }

    // get previous node
    Node* prev;
    if constexpr (NodeTraits<Node>::has_prev) {
        // NOTE: this is O(1) for doubly-linked sets and dictionaries because
        // we already have a pointer to the previous node.
        prev = static_cast<Node*>(curr->prev);
    } else {
        // NOTE: this is O(n) for singly-linked sets and dictionaries because we
        // have to traverse the whole list to find the previous node.
        prev = nullptr;
        Node* temp = view.head;
        while (temp != curr) {
            prev = temp;
            temp = static_cast<Node*>(temp->next);
        }
    }

    // unlink and free node
    view.unlink(prev, curr, static_cast<Node*>(curr->next));
    view.recycle(curr);
}


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


}  // namespace algorithms
}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_ALGORITHMS_REMOVE_H include guard
