#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H

#include <cstddef>
#include <optional>  // std::optional
#include <type_traits>  // std::enable_if_t<>
#include "../../util/iter.h"  // iter()
#include "../../util/ops.h"  // len()
#include "../core/view.h"  // ViewTraits
#include "extend.h"  // extend()


// TODO: work in Yield::ITEM.
// -> have to convert existing items to python tuples during concatenation.
// -> return type must also be a list of tuples if as_pytuple is true or a list of
// std::pair<Key, Value> if as_pytuple is false.


namespace bertrand {
namespace linked {


    /* Concatenate a linked list with another iterable. */
    template <typename View, typename Container>
    auto concatenate(const View& view, const Container& container)
        -> std::enable_if_t<
            ViewTraits<View>::listlike,
            typename ViewTraits<View>::As::DYNAMIC
        >
    {
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        std::optional<size_t> length = len(container);

        DynamicView copy(
            length.has_value() ? view.size() + length.value() : view.capacity(),
            view.specialization()
        );

        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            copy.link(copy.tail(), copy.node(*(it.curr())), nullptr);
        }

        for (const auto& item : iter(container)) {
            copy.link(copy.tail(), copy.node(item), nullptr);
        }

        return copy;
    }


    /* Concatenate a linked dictionary's values with another iterable, returning the
    result as a linked list. */
    template <
        Yield yield = Yield::KEY,
        bool as_pytuple = false,
        typename View,
        typename Container
    >
    auto concatenate(const View& view, const Container& container)
        -> std::enable_if_t<
            ViewTraits<View>::dictlike,
            typename ViewTraits<
                typename ViewTraits<View>::As::DYNAMIC
            >::As::template List<yield, as_pytuple>
        >
    {
        static_assert(
            yield != Yield::KEY,
            "cannot concatenate dictionary keys: use setlike operators instead"
        );
        static_assert(
            ViewTraits<View>::Assert::template as_pytuple<as_pytuple, yield>,
            "as_pytuple is only valid if view is dictlike, yield is set to "
            "Yield::ITEM, and the dictionary contains pure PyObject* keys and values"
        );
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using List = typename ViewTraits<DynamicView>::As::template List<yield, as_pytuple>;

        std::optional<size_t> length = len(container);
        List list(
            length.has_value() ? view.size() + length.value() : view.capacity(),
            view.specialization()
        );

        auto it = view.template begin<yield>();
        auto end = view.template end<yield>();
        for (; it != end; ++it) {
            if constexpr (as_pytuple) {
                const std::pair<PyObject*, PyObject*> pair = *it;
                PyObject* tuple = PyTuple_Pack(2, pair.first, pair.second);
                try {
                    list.link(list.tail(), list.node(tuple), nullptr);
                    Py_DECREF(tuple);
                } catch (...) {
                    Py_DECREF(tuple);
                    throw;
                }
            } else {
                list.link(list.tail(), list.node(*it), nullptr);
            }
        }

        for (const auto& item : iter(container)) {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (as_pytuple && is_pairlike<Item>) {
                PyObject* tuple = PyTuple_Pack(2, std::get<0>(item), std::get<1>(item));
                try {
                    list.link(list.tail(), list.node(tuple), nullptr);
                    Py_DECREF(tuple);
                } catch (...) {
                    Py_DECREF(tuple);
                    throw;
                }
            } else {
                list.link(list.tail(), list.node(item), nullptr);
            }
        }
        return list;
    }


}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H
