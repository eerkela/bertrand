#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H

#include <optional>  // std::optional
#include <type_traits>  // std::enable_if_t<>
#include "../../util/iter.h"  // iter()
#include "../../util/ops.h"  // len()
#include "../core/view.h"  // ViewTraits
#include "extend.h"  // extend()


// TODO: work in Yield::VALUE/ITEM for dictlike views.


namespace bertrand {
namespace linked {


    /* Concatenate a linked list with another iterable. */
    template <typename View, typename Container>
    auto concatenate(const View& view, const Container& container)
        -> std::enable_if_t<ViewTraits<View>::listlike, View>
    {
        using Node = typename View::Node;
        std::optional<size_t> length = len(container);

        if (length.has_value()) {
            View copy(view.size() + length.value(), view.specialization());

            for (auto it = view.begin(), end = view.end(); it != end; ++it) {
                Node* node = copy.node(*(it.curr()));
                copy.link(copy.tail(), node, nullptr);
            }

            for (const auto& item : iter(container)) {
                Node* node = copy.node(item);
                copy.link(copy.tail(), node, nullptr);
            }
            return copy;
        }

        View copy(view);
        linked::extend(copy, container);
        return copy;
    }


}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H
