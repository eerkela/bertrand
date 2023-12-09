#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_ROTATE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_ROTATE_H

#include <cstddef>  // size_t
#include <cmath>  // abs()
#include <type_traits>  // std::enable_if_t<>
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Rotate a linked list to the right by the specified number of steps. */
    template <typename View>
    auto rotate(View& view, long long steps)
        -> std::enable_if_t<ViewTraits<View>::linked, void>
    {
        using Node = typename View::Node;
        size_t norm_steps = std::llabs(steps) % view.size();
        if (norm_steps == 0) {
            return;
        }

        size_t index;
        if (steps < 0) {
            index = norm_steps;
        } else {
            index = view.size() - norm_steps;
        }

        Node* new_head;
        Node* new_tail;

        if constexpr (NodeTraits<Node>::has_prev) {
            if (view.closer_to_tail(index)) {
                new_head = view.tail();
                for (size_t i = view.size() - 1; i > index; --i) {
                    new_head = new_head->prev();
                }
                new_tail = new_head->prev();
 
                Node::join(view.tail(), view.head());
                Node::split(new_tail, new_head);
                view.head(new_head);
                view.tail(new_tail);
                return;
            }
        }

        new_tail = view.head();
        for (size_t i = 1; i < index; ++i) {
            new_tail = new_tail->next();
        }
        new_head = new_tail->next();

        Node::join(view.tail(), view.head());
        Node::split(new_tail, new_head);
        view.head(new_head);
        view.tail(new_tail);
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_ROTATE_H
