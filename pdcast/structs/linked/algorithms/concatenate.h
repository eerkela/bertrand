#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <type_traits>  // std::enable_if_t<>
#include "../../util/iter.h"  // iter()
#include "../../util/ops.h"  // len()
#include "../core/view.h"  // ViewTraits
#include "extend.h"  // extend()


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
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using List = typename ViewTraits<DynamicView>::As::template List<yield, as_pytuple>;

        std::optional<size_t> length = len(container);
        List list(
            length.has_value() ? view.size() + length.value() : view.capacity(),
            view.specialization()
        );

        auto it = view.template begin<yield, as_pytuple>();
        auto end = view.template end<yield, as_pytuple>();
        for (; it != end; ++it) {
            list.link(list.tail(), list.node(*it), nullptr);
        }

        for (const auto& item : iter(container)) {
            if constexpr (as_pytuple && is_pairlike<std::decay_t<decltype(item)>>) {
                using PyTuple = python::Tuple<python::Ref::STEAL>;
                PyTuple tuple = PyTuple::pack(std::get<0>(item), std::get<1>(item));
                list.link(list.tail(), list.node(tuple), nullptr);
            } else {
                list.link(list.tail(), list.node(item), nullptr);
            }
        }
        return list;
    }


}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H
