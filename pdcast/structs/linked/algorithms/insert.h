#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_INSERT_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_INSERT_H

#include <cstddef>  // size_t
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/view.h"  // DictView


namespace bertrand {
namespace linked {


    template <typename View, typename Item>
    void insert(View& view, long long index, const Item& item) {
        using MemGuard = typename View::MemGuard;

        if (index < 0) {
            index += view.size();
            if (index < 0) {
                index = 0;
            }
        } else if (index > static_cast<long long>(view.size())) {
            index = view.size();
        }
        size_t norm_index = static_cast<size_t>(index);

        // NOTE: if we don't reserve ahead of time, then the iterator might be
        // invalidated by the node() constructor
        MemGuard guard = view.reserve(view.size() + 1);

        if constexpr (NodeTraits<typename View::Node>::has_prev) {
            if (view.closer_to_tail(norm_index)) {
                auto it = view.rbegin();
                for (size_t i = view.size(); i > norm_index; --i, ++it);
                it.insert(view.node(item));
                return;
            }
        }

        auto it = view.begin();
        for (size_t i = 0; i < norm_index; ++i, ++it);
        it.insert(view.node(item));
    }


    template <typename Node, unsigned int Flags, typename Key, typename Value>
    void insert(
        DictView<Node, Flags>& view,
        long long index,
        const Key& key,
        const Value& value
    ) {
        using View = DictView<Node, Flags>;
        using MemGuard = typename View::MemGuard;

        if (index < 0) {
            index += view.size();
            if (index < 0) {
                index = 0;
            }
        } else if (index > static_cast<long long>(view.size())) {
            index = view.size();
        }
        size_t norm_index = static_cast<size_t>(index);

        // NOTE: if we don't reserve ahead of time, then the iterator might be
        // invalidated by the node() constructor
        MemGuard guard = view.reserve(view.size() + 1);

        if constexpr (NodeTraits<typename View::Node>::has_prev) {
            if (view.closer_to_tail(norm_index)) {
                auto it = view.rbegin();
                for (size_t i = view.size(); i > norm_index; --i, ++it);
                it.insert(view.node(key, value));  // use 2-argument init
                return;
            }
        }

        auto it = view.begin();
        for (size_t i = 0; i < norm_index; ++i, ++it);
        it.insert(view.node(key, value));
    }


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_INSERT_H
