
// include guard prevents multiple inclusion
#ifndef CONTAINS_H
#define CONTAINS_H

#include <Python.h>  // for CPython API
#include "node.h"  // for nodes
#include "view.h"  // for views


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Check if an item is contained within a set or dictionary. */
template <
    template <typename, template <typename> class> class ViewType,
    typename NodeType,
    template <typename> class Allocator
>
inline int contains(ViewType<NodeType, Allocator>* view, PyObject* item) {
    return view->search(item) != nullptr;
}


/* Check if an item is contained within a list. */
template <typename NodeType, template <typename> class Allocator>
inline int contains(ListView<NodeType, Allocator>* view, PyObject* item) {
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


///////////////////////
////    ALIASES    ////
///////////////////////


// NOTE: Cython doesn't play well with heavily templated functions, so we need
// to explicitly instantiate the specializations we need.  Maybe in a future
// release we won't have to do this:


template int contains(DynamicListView<SingleNode>* view, PyObject* item);
template int contains(DynamicSetView<SingleNode>* view, PyObject* item);
template int contains(DynamicDictView<SingleNode>* view, PyObject* item);
template int contains(DynamicListView<DoubleNode>* view, PyObject* item);
template int contains(DynamicSetView<DoubleNode>* view, PyObject* item);
template int contains(DynamicDictView<DoubleNode>* view, PyObject* item);
template int contains(FixedListView<SingleNode>* view, PyObject* item);
template int contains(FixedSetView<SingleNode>* view, PyObject* item);
template int contains(FixedDictView<SingleNode>* view, PyObject* item);
template int contains(FixedListView<DoubleNode>* view, PyObject* item);
template int contains(FixedSetView<DoubleNode>* view, PyObject* item);
template int contains(FixedDictView<DoubleNode>* view, PyObject* item);


#endif // CONTAINS_H include guard
