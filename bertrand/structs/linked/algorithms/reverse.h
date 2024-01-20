#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_REVERSE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_REVERSE_H

#include <type_traits>  // std::enable_if_t<>
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace linked {


    template <typename View>
    auto reverse(View& view) -> std::enable_if_t<ViewTraits<View>::linked, void> {
        using Node = typename View::Node;
        Node* head = view.head();
        Node* curr = head;

        if constexpr (NodeTraits<Node>::has_prev) {
            while (curr != nullptr) {
                Node* next = curr->next();
                curr->next(curr->prev());
                curr->prev(next);
                curr = next;
            }
        } else {
            Node* prev = nullptr;
            while (curr != nullptr) {
                Node* next = curr->next();
                curr->next(prev);
                prev = curr;
                curr = next;
            }
        }

        view.head(view.tail());
        view.tail(head);
    }


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_REVERSE_H
