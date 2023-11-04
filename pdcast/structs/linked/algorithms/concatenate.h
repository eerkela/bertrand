// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H

#include <type_traits>  // std::enable_if_t<>
#include "../core/view.h"  // ViewTraits
#include "extend.h"  // extend()


namespace bertrand {
namespace structs {
namespace linked {


    /* Concatenate a linked data structure with another iterable. */
    template <typename View, typename Container>
    auto concatenate(const View& view, const Container& container)
        -> std::enable_if_t<ViewTraits<View>::listlike, View>
    {
        View copy(view);
        extend(copy, container, false);
        return copy;
    }


    /* Concatenate a linked data structure with another iterable (reversed). */
    template <typename Container, typename View>
    auto concatenate(const Container& container, const View& view)
        -> std::enable_if_t<ViewTraits<View>::listlike, View>
    {
        View copy(container);
        extend(copy, view, false);
        return copy;
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_CONCATENATE_H
