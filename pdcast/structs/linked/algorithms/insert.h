// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_INSERT_H
#define BERTRAND_STRUCTS_ALGORITHMS_INSERT_H

#include <cstddef>  // size_t
#include <type_traits>  // std::enable_if_t<>
#include <utility>  // std::pair
#include <Python.h>  // CPython API
#include "../core/view.h"  // ViewTraits
#include "position.h"  // position()


namespace bertrand {
namespace structs {
namespace linked {


    /* Insert an item into a linked list, set, or dictionary at the given index. */
    template <
        typename View,
        typename Index,
        typename Item = typename View::Value
    >
    inline auto insert(View& view, Index index, Item& item)
        -> std::enable_if_t<ViewTraits<View>::listlike, void>
    {
        position(view, index).insert(item);
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_INSERT_H include guard
