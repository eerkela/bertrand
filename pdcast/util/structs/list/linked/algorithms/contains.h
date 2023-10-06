// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_CONTAINS_H
#define BERTRAND_STRUCTS_ALGORITHMS_CONTAINS_H

#include <Python.h>  // CPython API


namespace bertrand {
namespace structs {
namespace algorithms {


namespace list {

    /* Check if an item is contained within a linked list. */
    template <typename View>
    int contains(View& view, PyObject* item) {
        using Node = typename View::Node;

        for (Node* node : view) {
            if (node->eq(item)) {
                return true;
            }
        }
        return false;
    }

}  // namespace list


namespace set {

    /* Check if an item is contained within a linked set or dictionary. */
    template <typename View>
    inline int contains(View& view, PyObject* item) {
        return view.search(item) != nullptr;
    }

}  // namespace set


}  // namespace algorithms
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_CONTAINS_H include guard
