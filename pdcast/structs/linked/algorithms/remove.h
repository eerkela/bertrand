// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_REMOVE_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_REMOVE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_REMOVE_H

#include <sstream>  // std::ostringstream
#include <stdexcept>  // std::invalid_argument
#include <type_traits>  // std::enable_if_t<>
#include "../../util/iter.h"  // iter()
#include "../../util/repr.h"  // repr()
#include "../../util/python.h"  // eq()
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Remove the first occurrence of an item from a linked list. */
    template <typename View, typename Item = typename View::Value>
    auto remove(View& view, Item& item)
        -> std::enable_if_t<ViewTraits<View>::listlike, void>
    {
        using Node = typename View::Node;
        using util::iter;

        // find item in list
        for (auto it = iter(view).forward(); it != it.end(); ++it) {
            Node* node = it.curr();
            if (util::eq(node->value(), item)) {
                view.recycle(it.drop());
                return;
            }
        }

        // item not found
        std::ostringstream msg;
        msg << util::repr(item) << " is not in list";
        throw std::invalid_argument(msg.str());  
    }


    /* Remove an item from a linked set or dictionary. */
    template <typename View, typename Item = typename View::Value>
    auto remove(View& view, Item& item)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Allocator = typename View::Allocator;
        view.template recycle<Allocator::UNLINK>(item);
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_REMOVE_H
