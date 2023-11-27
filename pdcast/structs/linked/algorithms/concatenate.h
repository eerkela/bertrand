// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/python.h"  // len()
#include "../core/view.h"  // ViewTraits
#include "extend.h"  // extend()


namespace bertrand {
namespace structs {
namespace linked {


    /* Concatenate a linked list with another iterable. */
    template <typename View, typename Container>
    auto concatenate(const View& view, const Container& container)
        -> std::enable_if_t<ViewTraits<View>::listlike, View>
    {
        using Node = typename View::Node;

        // try to get length of container
        std::optional<size_t> length = util::len(container);
        if (length.has_value()) {
            // preallocate exact size
            View copy(
                view.size() + length.value(),
                view.dynamic(),
                view.specialization()
            );

            // add elements from view
            for (auto it = view.begin(), end = view.end(); it != end; ++it) {
                Node* node = copy.node(*(it.curr()));
                copy.link(copy.tail(), node, nullptr);
            }

            // add elements from container
            for (auto item : util::iter(container)) {
                Node* node = copy.node(item);
                copy.link(copy.tail(), node, nullptr);
            }

            return copy;
        }

        // otherwise, copy existing view and extend dynamically
        View copy(view);
        linked::extend(copy, container);
        return copy;
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H
