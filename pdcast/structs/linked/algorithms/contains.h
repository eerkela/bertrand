// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_CONTAINS_H
#define BERTRAND_STRUCTS_ALGORITHMS_CONTAINS_H

#include <Python.h>  // CPython API


namespace bertrand {
namespace structs {
namespace linked {
namespace algorithms {


namespace list {

    /* Check if an item is contained within a linked list. */
    template <typename View>
    int contains(View& view, PyObject* item) {
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            if (it.curr()->eq(item)) {
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
}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_CONTAINS_H include guard
