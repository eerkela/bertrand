// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_POP_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_POP_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_POP_H

#include <tuple>  // std::tuple
#include <type_traits>  // std::enable_if_t<>
#include <Python.h>  // CPython API
#include "../../util/base.h"  // is_pyobject<>
#include "../core/view.h"  // ViewTraits
#include "position.h"  // position()


namespace bertrand {
namespace structs {
namespace linked {


    /* Pop an item from a linked list, set, or dictionary at the given index. */
    template <typename View>
    inline auto pop(View& view, long long index)
        -> std::enable_if_t<ViewTraits<View>::linked, typename View::Value>
    {
        using Node = typename View::Node;
        using Value = typename View::Value;

        // trivial case: empty list
        if (view.size() == 0) throw IndexError("pop from empty list");

        // normalize index
        size_t norm_index = normalize_index(index, view.size(), true);

        // payload for return value
        auto execute = [&view](Node* node) {
            Value result = node->value();
            if constexpr (is_pyobject<Value>) {
                Py_INCREF(result);  // return new reference
            }
            view.recycle(node);
            return result;
        };

        // get iterator to index
        if constexpr (NodeTraits<Node>::has_prev) {
            if (view.closer_to_tail(norm_index)) {
                auto it = view.rbegin();
                for (size_t i = view.size() - 1; i > norm_index; --i) ++it;
                return execute(it.drop());
            }
        }

        // forward traversal
        auto it = view.begin();
        for (size_t i = 0; i < norm_index; ++i) ++it;
        return execute(it.drop());
    }


    /* Pop a key from a linked dictionary and return its corresponding value. */
    template <typename View, typename Key, typename Value>
    auto pop(View& view, const Key& key, const Value& default_value)
        -> std::enable_if_t<ViewTraits<View>::dictlike, Value>
    {
        using Node = typename View::Node;

        // trivial case: empty list
        if (view.size() == 0) throw IndexError("pop from empty list");

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
        if constexpr (is_pyobject<Value>) {
            Py_INCREF(value);  // return new reference
        }
        view.unlink(prev, curr, curr->next());
        view.recycle(curr);
        return value;
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_POP_H
