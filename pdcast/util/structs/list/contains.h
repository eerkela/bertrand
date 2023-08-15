
// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_CONTAINS_H
#define BERTRAND_STRUCTS_ALGORITHMS_CONTAINS_H

#include <Python.h>  // for CPython API
#include "node.h"  // for nodes
#include "view.h"  // for views


//////////////////////
////    PUBLIC    ////
//////////////////////


namespace Ops {

    /* Check if an item is contained within a linked set or dictionary. */
    template <
        template <typename, template <typename> class> class ViewType,
        typename NodeType,
        template <typename> class Allocator
    >
    inline int contains(ViewType<NodeType, Allocator>* view, PyObject* item) {
        return view->search(item) != nullptr;
    }

    /* Check if an item is contained within a linked list. */
    template <typename NodeType, template <typename> class Allocator>
    int contains(ListView<NodeType, Allocator>* view, PyObject* item) {
        using Node = typename ListView<NodeType, Allocator>::Node;
        Node* curr = view->head;
        int comp;

        // search until we find the item or hit stop index
        while (curr != nullptr) {
            // C API equivalent of the == operator
            comp = PyObject_RichCompareBool(curr->value, item, Py_EQ);
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
