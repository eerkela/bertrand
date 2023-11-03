// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/iter.h"  // iter()
#include "../../util/python.h"  // len()
#include "../core/view.h"  // ViewTraits
#include "extend.h"  // extend()


// TODO: replace with extend()


namespace bertrand {
namespace structs {
namespace linked {


    /* Concatenate a linked data structure with another iterable. */
    template <typename View, typename Container>
    auto concatenate(const View& view, const Container& container)
        -> std::enable_if_t<ViewTraits<View>::listlike, View>
    {
        using Node = typename View::Node;

        // copy existing view and preallocate space for container
        View copy(view);
        std::optional<size_t> size = util::len(container);
        if (size.has_value()) {
            copy.reserve(view.size() + size.value());
        }

        // iterate through container and append each item
        for (auto item : util::iter(container)) {
            Node* node = copy.node(item);
            copy.link(copy.tail(), node, nullptr);
        }
        return copy;
    }


    /* Concatenate a linked data structure with another iterable (reversed). */
    template <typename Container, typename View>
    auto concatenate(const Container& container, const View& view)
        -> std::enable_if_t<ViewTraits<View>::listlike, View>
    {
        using Node = typename View::Node;

        // copy existing view and preallocate space for container
        View copy(view);
        std::optional<size_t> size = util::len(container);
        if (size.has_value()) {
            copy.reserve(view.size() + size.value());
        }

        // iterate through container and append each item
        for (auto item : util::iter(container).reverse()) {
            Node* node = copy.node(item);
            copy.link(nullptr, node, copy.head());
        }
        return copy;
    }



}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H
