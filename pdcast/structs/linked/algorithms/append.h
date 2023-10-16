// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_APPEND_H
#define BERTRAND_STRUCTS_ALGORITHMS_APPEND_H

#include <Python.h>  // CPython API


namespace bertrand {
namespace structs {
namespace linked {
namespace algorithms {


namespace list {

    /* Add an item to the end of a linked list, set, or dictionary. */
    template <typename ListLike, typename Value = typename ListLike::Value>
    void append(ListLike& list, Value& item, bool left) {
        using Node = typename ListLike::Node;

        // allocate a new node
        Node* node = list.view.node(item);

        // link to beginning/end of list
        if (left) {
            list.view.link(nullptr, node, list.view.head());
        } else {
            list.view.link(list.view.tail(), node, nullptr);
        }
    }

}  // namespace list


namespace dict {

    /* Add a key-value pair to the end of a linked dictionary. */
    template <typename View>
    void append(
        View& view,
        PyObject* key,
        PyObject* value,
        bool left
    ) {
        using Node = typename View::Node;

        // allocate a new node (use 2-argument init())
        Node* node = view.node(key, value);

        // link to beginning/end of list
        if (left) {
            view.link(nullptr, node, view.head);
        } else {
            view.link(view.tail, node, nullptr);
        }
    }

}  // namespace dict


}  // namespace algorithms
}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_APPEND_H
