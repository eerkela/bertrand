#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_REMOVE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_REMOVE_H

#include <sstream>  // std::ostringstream
#include <type_traits>  // std::enable_if_t<>
#include "../../util/ops.h"  // eq(), repr()
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Remove the first occurrence of an item from a linked list. */
    template <typename View, typename Item>
    auto remove(View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::listlike, void>
    {
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            if (eq(*it, item)) {
                view.recycle(it.drop());
                return;
            }
        }
        std::ostringstream msg;
        msg << repr(item) << " is not in list";
        throw KeyError(msg.str());  
    }


    /* Remove an item from a linked set or dictionary. */
    template <typename View, typename Item>
    auto remove(View& view, const Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Allocator = typename View::Allocator;
        view.template recycle<Allocator::UNLINK>(item);
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_REMOVE_H
