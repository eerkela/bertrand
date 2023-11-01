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
        return position(view, index).pop();
    }


    /* Pop a key from a linked dictionary and return its corresponding value. */
    template <
        typename View,
        typename Key = typename View::Value,
        typename Value = typename View::MappedValue
    >
    auto pop(View& view, Key& key, Value& default_value)
        -> std::enable_if_t<ViewTraits<View>::dictlike, Value>
    {
        using Node = typename View::Node;

        // search for node
        Node* curr = view.search(key);
        if (curr == nullptr) {
            return default_value;
        }

        // get neighboring nodes
        Node* prev;
        if constexpr (NodeTraits<Node>::has_prev) {  // O(1) if doubly-linked
            prev = curr->prev();
        } else {
            auto it = view.begin();
            while (it.next() != curr) ++it;
            prev = it.curr();
        }

        // get return value and recycle node
        Value value = curr->value;
        if constexpr (util::is_pyobject<Value>) {
            Py_INCREF(value);  // return new reference
        }
        view.unlink(prev, curr, curr->next());
        view.recycle(curr);
        return value;
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_POP_H include guard
