// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_ROTATE_H
#define BERTRAND_STRUCTS_ALGORITHMS_ROTATE_H

#include <cstddef>  // size_t
#include <cmath>  // abs()
#include <utility>  // std::pair
#include <Python.h>  // CPython API


namespace bertrand {
namespace structs {
namespace algorithms {


namespace list {

    /* Rotate a linked list to the right by the specified number of steps. */
    template <typename View>
    void rotate(View& view, long long steps) {
        using Node = typename View::Node;

        // normalize steps
        size_t norm_steps = llabs(steps) % view.size();
        if (norm_steps == 0) {
            return;  // rotated list is identical to original
        }

        // get index at which to split the list
        size_t index;
        size_t rotate_left = (steps < 0);
        if (rotate_left) {  // count from head
            index = norm_steps;
        } else {  // count from tail
            index = view.size() - norm_steps;
        }

        Node* new_head;
        Node* new_tail;

        // identify new head and tail of rotated list
        if constexpr (Node::doubly_linked) {
            // NOTE: if the list is doubly-linked, then we can iterate in either
            // direction to find the junction point.
            if (index > view.size() / 2) {  // backward traversal
                new_head = view.tail();
                for (size_t i = view.size() - 1; i > index; i--) {
                    new_head = new_head->prev();
                }
                new_tail = new_head->prev();

                // join previous head/tail and split at new junction 
                Node::join(view.tail(), view.head());
                Node::split(new_tail, new_head);

                // update head/tail pointers
                view.head(new_head);
                view.tail(new_tail);
                return;
            }
        }

        // forward traversal
        new_tail = view.head();
        for (size_t i = 1; i < index; i++) {
            new_tail = new_tail->next();
        }
        new_head = new_tail->next();

        // split at junction and join previous head/tail
        Node::split(new_tail, new_head);
        Node::join(view.tail(), view.head());

        // update head/tail pointers
        view.head(new_head);
        view.tail(new_tail);
    }

}  // namespace list


}  // namespace algorithms
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_ROTATE_H include guard
