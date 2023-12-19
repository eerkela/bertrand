#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_DICT_COMPARE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_DICT_COMPARE_H

#include <type_traits>  // std::enable_if_t<>
#include <unordered_set>  // std::unordered_set
#include "../../util/base.h"  // is_pairlike<>
#include "../../util/iter.h"  // iter()
#include "../../util/python.h"  // python::Dict
#include "../core/view.h"  // ViewTraits, Yield


namespace bertrand {
namespace linked {


    /* Check whether the key-value pairs of a linked dictionary are equal to those of
    another container. */
    template <bool equal, typename View, typename Container>
    auto dict_equal(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, bool>
    {
        using Node = typename View::Node;
        std::unordered_set<const Node*> found;

        if constexpr (is_pyobject<Container>) {
            if (PyDict_Check(items)) {
                python::Dict<python::Ref::BORROW> dict(items);
                for (const auto& item : iter(dict)) {
                    const Node* node = view.search(std::get<0>(item));
                    if (node == nullptr || !eq(node->mapped(), std::get<1>(item))) {
                        return !equal;
                    }
                    found.insert(node);
                }
                if constexpr (equal) {
                    return found.size() == view.size();
                } else {
                    return found.size() != view.size();
                }
            }
        }

        for (const auto& item : iter(items)) {
            using Item = std::decay_t<decltype(item)>;
            const Node* node;

            if constexpr (is_pyobject<Item>) {
                PyObject* key;
                PyObject* value;
                if (PyTuple_Check(item) && PyTuple_GET_SIZE(item) == 2) {
                    key = PyTuple_GET_ITEM(item, 0);
                    value = PyTuple_GET_ITEM(item, 1);
                } else if (PyList_Check(item) && PyList_GET_SIZE(item) == 2) {
                    key = PyList_GET_ITEM(item, 0);
                    value = PyList_GET_ITEM(item, 1);
                } else {
                    return !equal;
                }

                node = view.search(key);
                if (node == nullptr || !eq(node->mapped(), value)) {
                    return !equal;
                }

            } else {
                if constexpr (!is_pairlike<Item>) {
                    return !equal;
                }
                node = view.search(std::get<0>(item));
                if (node == nullptr || !eq(node->mapped(), std::get<1>(item))) {
                    return !equal;
                }
            }

            found.insert(node);
        }

        if constexpr (equal) {
            return found.size() == view.size();
        } else {
            return found.size() != view.size();
        }
    }


    // /* Check whether the key-value pairs of a linked dictionary's items() proxy are
    // lexicographically less than those of another container. */
    // template <typename View, typename Container>
    // auto items_lt(const View& view, const Container& items)
    //     -> std::enable_if_t<ViewTraits<View>::dictlike, bool>
    // {

    // }



}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_DICT_COMPARE_H
