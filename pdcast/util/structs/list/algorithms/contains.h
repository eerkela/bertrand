// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_CONTAINS_H
#define BERTRAND_STRUCTS_ALGORITHMS_CONTAINS_H

#include <Python.h>  // CPython API
#include "../core/view.h"  // views


namespace Ops {

    /* Check if an item is contained within a linked set or dictionary. */
    template <typename View>
    inline int contains(View& view, PyObject* item) {
        return view.search(item) != nullptr;
    }

    /* Check if an item is contained within a linked list. */
    template <typename NodeType, template <typename> class Allocator>
    int contains(ListView<NodeType, Allocator>& view, PyObject* item) {
        using Node = typename ListView<NodeType, Allocator>::Node;

        // search until we find the item or hit stop index
        Node* curr = view.head;
        while (curr != nullptr) {
            // C API equivalent of the == operator
            int comp = PyObject_RichCompareBool(curr->value, item, Py_EQ);
            if (comp == -1) {  // == comparison raised an exception
                return -1;
            } else if (comp == 1) {  // found a match
                return 1;
            }

            // advance to next node
            curr = static_cast<Node*>(curr->next);
        }

        // item not found
        return 0;
    }

}


#endif // BERTRAND_STRUCTS_ALGORITHMS_CONTAINS_H include guard
