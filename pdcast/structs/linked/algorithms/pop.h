#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_POP_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_POP_H

#include <tuple>  // std::tuple
#include <type_traits>  // std::enable_if_t<>
#include <Python.h>  // CPython API
#include "../../util/base.h"  // is_pyobject<>
#include "../core/view.h"  // ViewTraits
#include "position.h"  // position()


namespace bertrand {
namespace linked {


    template <typename View>
    auto pop(View& view, long long index)
        -> std::enable_if_t<
            ViewTraits<View>::listlike || ViewTraits<View>::setlike, typename View::Value
        >
    {
        using Node = typename View::Node;
        using Value = typename View::Value;
        if (view.size() == 0) {
            throw IndexError("pop from empty list");
        }

        size_t norm_index = normalize_index(index, view.size(), true);

        // payload for return value
        auto execute = [&view](Node* node) -> Value {
            Value result = node->value();
            if constexpr (is_pyobject<Value>) {
                Py_INCREF(result);  // new reference
            }
            view.recycle(node);
            return result;
        };

        if constexpr (NodeTraits<Node>::has_prev) {
            if (view.closer_to_tail(norm_index)) {
                auto it = view.rbegin();
                for (size_t i = view.size() - 1; i > norm_index; --i, ++it);
                return execute(it.drop());
            }
        }

        auto it = view.begin();
        for (size_t i = 0; i < norm_index; ++i, ++it);
        return execute(it.drop());
    }


    template <typename View, typename Key>
    auto pop(View& view, const Key& key)
        -> std::enable_if_t<ViewTraits<View>::dictlike, typename View::MappedValue>
    {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::UNLINK | Allocator::RETURN_MAPPED
        );

        if (view.size() == 0) {
            throw IndexError("pop from empty dict");
        }
        return view.template recycle<flags>(key);
    }


    template <typename View, typename Key, typename Value>
    auto pop(View& view, const Key& key, Value& default_value)
        -> std::enable_if_t<ViewTraits<View>::dictlike, Value>
    {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::NOEXIST_OK | Allocator::UNLINK | Allocator::RETURN_MAPPED
        );

        if (view.size() == 0) {
            throw IndexError("pop from empty dict");
        }
        return view.template recycle<flags>(key, default_value);
    }


    template <typename View>
    auto popitem(View& view, long long index)
        -> std::enable_if_t<
            ViewTraits<View>::dictlike,
            std::pair<typename View::Value, typename View::MappedValue>
        >
    {
        using Node = typename View::Node;
        using Key = typename View::Value;
        using Value = typename View::MappedValue;
        if (view.size() == 0) {
            throw IndexError("pop from empty dictionary");
        }

        size_t norm_index = normalize_index(index, view.size(), true);

        // payload for return value
        auto execute = [&view](Node* node) -> std::pair<Key, Value> {
            Key key = node->value();
            if constexpr (is_pyobject<Key>) {
                Py_INCREF(key);
            }
            Value value = node->mapped();
            if constexpr (is_pyobject<Value>) {
                Py_INCREF(value);
            }
            view.recycle(node);
            return std::make_pair(key, value);
        };

        if constexpr (NodeTraits<Node>::has_prev) {
            if (view.closer_to_tail(norm_index)) {
                auto it = view.rbegin();
                for (size_t i = view.size() - 1; i > norm_index; --i, ++it);
                return execute(it.drop());
            }
        }

        auto it = view.begin();
        for (size_t i = 0; i < norm_index; ++i, ++it);
        return execute(it.drop());
    }


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_POP_H
