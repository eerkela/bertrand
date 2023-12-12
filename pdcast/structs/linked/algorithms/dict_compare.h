#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_DICT_COMPARE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_DICT_COMPARE_H

#include <type_traits>  // std::enable_if_t<>
#include <unordered_set>  // std::unordered_set
#include "../../util/base.h"  // is_pairlike<>
#include "../../util/iter.h"  // iter()
#include "../core/view.h"  // ViewTraits, Yield


namespace bertrand {
namespace linked {


    ///////////////////////
    ////    PRIVATE    ////
    ///////////////////////


    /* Container-independent implementation for dict_equal(). */
    template <typename View, typename Container, bool invert>
    auto dict_equal_impl(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, bool>
    {
        using Node = typename View::Node;

        std::unordered_set<const Node*> found;

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
                    return invert;
                }

                node = view.search(key);
                if (node == nullptr || !eq(node->mapped(), value)) {
                    return invert;
                }

            } else {
                if constexpr (!is_pairlike<Item>) {
                    return invert;
                }
                node = view.search(std::get<0>(item));
                if (node == nullptr || !eq(node->mapped(), std::get<1>(item))) {
                    return invert;
                }
            }

            found.insert(node);
        }

        if constexpr (invert) {
            return found.size() != view.size();
        } else {
            return found.size() == view.size();
        }
    }


    //////////////////////
    ////    PUBLIC    ////
    //////////////////////


    /* Check whether the key-value pairs of a linked dictionary are equal to those of
    another container. */
    template <typename View, typename Container>
    auto dict_equal(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, bool>
    {
        if constexpr (is_pyobject<Container>) {
            if (PyDict_Check(items)) {
                PyDict dict(items);
                return dict_equal_impl<View, PyDict, false>(view, dict);
            }
            // TODO: if (PyLinkedDict::typecheck(items))
        }
        return dict_equal_impl<View, Container, false>(view, items);
    }


    /* Check whether the key-value pairs of a linked dictionary are not equal to those
    of another container. */
    template <typename View, typename Container>
    auto dict_not_equal(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, bool>
    {
        if constexpr (is_pyobject<Container>) {
            if (PyDict_Check(items)) {
                PyDict dict(items);
                return dict_equal_impl<View, PyDict, true>(view, dict);
            }
            // TODO: if (PyLinkedDict::typecheck(items))
        }
        return dict_equal_impl<View, Container, true>(view, items);
    }





}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_DICT_COMPARE_H
