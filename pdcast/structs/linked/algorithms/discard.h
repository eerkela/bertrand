#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISCARD_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISCARD_H

#include <type_traits>  // std::enable_if_t<>
#include "../core/node.h"  // NodeTrats
#include "../core/view.h"  // ViewTrats


namespace bertrand {
namespace linked {


    template <typename View, typename Item>
    inline auto discard(View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Allocator = typename View::Allocator;
        view.template recycle<Allocator::NOEXIST_OK | Allocator::UNLINK>(item);
    }


}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_DISCARD_H
