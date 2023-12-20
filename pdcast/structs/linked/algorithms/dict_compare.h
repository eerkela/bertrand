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
                PyObject* obj = static_cast<PyObject*>(item);
                PyObject* key;
                PyObject* value;
                if (PyTuple_Check(obj) && PyTuple_GET_SIZE(obj) == 2) {
                    key = PyTuple_GET_ITEM(obj, 0);
                    value = PyTuple_GET_ITEM(obj, 1);
                } else if (PyList_Check(obj) && PyList_GET_SIZE(obj) == 2) {
                    key = PyList_GET_ITEM(obj, 0);
                    value = PyList_GET_ITEM(obj, 1);
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


    template <bool strict, typename View, typename Container>
    auto itemsproxy_less(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, bool>
    {
        auto it_lhs = view.template begin<Yield::ITEM>();
        auto end_lhs = view.template end<Yield::ITEM>();
        auto it_rhs = iter(items).forward();

        while (it_lhs != end_lhs && it_rhs != it_rhs.end()) {
            auto x = *it_lhs;
            auto y = *it_rhs;

            using Item = std::decay_t<decltype(y)>;
            if constexpr (is_pyobject<Item>) {
                PyObject* obj = static_cast<PyObject*>(y);
                PyObject* key;
                PyObject* value;
                if (PyTuple_Check(obj) && PyTuple_GET_SIZE(obj) == 2) {
                    key = PyTuple_GET_ITEM(obj, 0);
                    value = PyTuple_GET_ITEM(obj, 1);
                } else if (PyList_Check(obj) && PyList_GET_SIZE(obj) == 2) {
                    key = PyList_GET_ITEM(obj, 0);
                    value = PyList_GET_ITEM(obj, 1);
                } else {
                    throw TypeError("expected tuple or list of length 2");
                }

                if (lt(x.first, key)) {
                    return true;
                } else if (lt(key, x.first)) {
                    return false;
                } else if (lt(x.second, value)) {
                    return true;
                } else if (lt(value, x.second)) {
                    return false;
                }

            } else if constexpr (is_pairlike<Item>) {
                if (lt(x.first, std::get<0>(y))) {
                    return true;
                } else if (lt(std::get<0>(y), x.first)) {
                    return false;
                } else if (lt(x.second, std::get<1>(y))) {
                    return true;
                } else if (lt(std::get<1>(y), x.second)) {
                    return false;
                }

            } else {
                throw TypeError("expected pair-like object");
            }

            ++it_lhs;
            ++it_rhs;
        }

        if constexpr (strict) {
            return (!(it_lhs != end_lhs) && it_rhs != it_rhs.end());
        } else {
            return !(it_lhs != end_lhs);
        }
    }


    template <typename View, typename Container>
    auto itemsproxy_equal(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, bool>
    {
        auto it_lhs = view.template begin<Yield::ITEM>();
        auto end_lhs = view.template end<Yield::ITEM>();
        auto it_rhs = iter(items).forward();

        while (it_lhs != end_lhs && it_rhs != it_rhs.end()) {
            auto x = *it_lhs;
            auto y = *it_rhs;

            using Item = std::decay_t<decltype(y)>;
            if constexpr (is_pyobject<Item>) {
                PyObject* obj = static_cast<PyObject*>(y);
                PyObject* key;
                PyObject* value;
                if (PyTuple_Check(obj) && PyTuple_GET_SIZE(obj) == 2) {
                    key = PyTuple_GET_ITEM(obj, 0);
                    value = PyTuple_GET_ITEM(obj, 1);
                } else if (PyList_Check(obj) && PyList_GET_SIZE(obj) == 2) {
                    key = PyList_GET_ITEM(obj, 0);
                    value = PyList_GET_ITEM(obj, 1);
                } else {
                    throw TypeError("expected tuple or list of length 2");
                }

                if (ne(x.first, key)) {
                    return false;
                } else if (ne(x.second, value)) {
                    return false;
                }

            } else if constexpr (is_pairlike<Item>) {
                if (ne(x.first, std::get<0>(y))) {
                    return false;
                } else if (ne(x.second, std::get<1>(y))) {
                    return false;
                }

            } else {
                throw TypeError("expected pair-like object");
            }

            ++it_lhs;
            ++it_rhs;
        }

        return (!(it_lhs != end_lhs) && !(it_rhs != it_rhs.end()));
    }


    template <bool strict, typename View, typename Container>
    auto itemsproxy_greater(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, bool>
    {
        auto it_lhs = view.template begin<Yield::ITEM>();
        auto end_lhs = view.template end<Yield::ITEM>();
        auto it_rhs = iter(items).forward();

        while (it_lhs != end_lhs && it_rhs != it_rhs.end()) {
            auto x = *it_lhs;
            auto y = *it_rhs;

            using Item = std::decay_t<decltype(y)>;
            if constexpr (is_pyobject<Item>) {
                PyObject* obj = static_cast<PyObject*>(y);
                PyObject* key;
                PyObject* value;
                if (PyTuple_Check(obj) && PyTuple_GET_SIZE(obj) == 2) {
                    key = PyTuple_GET_ITEM(obj, 0);
                    value = PyTuple_GET_ITEM(obj, 1);
                } else if (PyList_Check(obj) && PyList_GET_SIZE(obj) == 2) {
                    key = PyList_GET_ITEM(obj, 0);
                    value = PyList_GET_ITEM(obj, 1);
                } else {
                    throw TypeError("expected tuple or list of length 2");
                }

                if (lt(key, x.first)) {
                    return true;
                } else if (lt(x.first, key)) {
                    return false;
                } else if (lt(value, x.second)) {
                    return true;
                } else if (lt(x.second, value)) {
                    return false;
                }

            } else if constexpr (is_pairlike<Item>) {
                if (lt(std::get<0>(y), x.first)) {
                    return true;
                } else if (lt(x.first, std::get<0>(y))) {
                    return false;
                } else if (lt(std::get<1>(y), x.second)) {
                    return true;
                } else if (lt(x.second, std::get<1>(y))) {
                    return false;
                }

            } else {
                throw TypeError("expected pair-like object");
            }

            ++it_lhs;
            ++it_rhs;
        }

        if constexpr (strict) {
            return (!(it_rhs != it_rhs.end()) && it_lhs != end_lhs);
        } else {
            return !(it_rhs != it_rhs.end());
        }
    }


}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_DICT_COMPARE_H
