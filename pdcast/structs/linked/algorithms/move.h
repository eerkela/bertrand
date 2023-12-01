// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_MOVE_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_MOVE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_MOVE_H

#include <cstddef>  // size_t
#include <sstream>  // std::ostringstream
#include <type_traits>  // std::enable_if_t<>
#include "../../util/ops.h"  // repr()
#include "../core/iter.h"  // Iterator
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits


// TODO: move_to_index() has same problem as insert() when it comes to handling the
// last index in the list.  We should be truncating to one past the last index, not
// the last index itself.


namespace bertrand {
namespace structs {
namespace linked {


    /* Move an item within a linked set or dictionary. */
    template <typename View, typename Item>
    auto move(View& view, const Item& item, long long steps)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Node = typename View::Node;

        // search for node in hash table
        Node* node = view.search(item);
        if (node == nullptr) {
            std::ostringstream msg;
            msg << repr(item) << " is not in set";
            throw KeyError(msg.str());
        }

        // trivial case: no movement
        if (steps == 0 || node == (steps > 0 ? view.tail() : view.head())) return;

        // get neighbors at both insertion and removal point
        Node* old_prev;
        Node* old_next = node->next();
        Node* new_prev;
        Node* new_next;
        if constexpr (NodeTraits<Node>::has_prev) {  // O(m) if doubly-linked
            old_prev = node->prev();

            // construct local iterator around node and walk to new position
            if (steps > 0) {
                using Iter = typename View::template Iterator<Direction::forward>;
                auto it = Iter(view, old_prev, node, old_next);
                for (auto end = view.end(); steps > 0 && it != end; --steps, ++it);
                new_prev = it.curr();
                new_next = it.next();
            } else {
                using Iter = typename View::template Iterator<Direction::backward>;
                auto it = Iter(view, old_prev, node, old_next);
                for (auto end = view.begin(); steps < 0 && it != end; ++steps, ++it);
                new_prev = it.prev();
                new_next = it.curr();
            }

        } else {  // otherwise, O(n + m) using lookahead iterators
            auto it = view.begin();

            // if we're moving forward, then there's no need for lookahead
            if (steps > 0) {
                for (; it.curr() != node; ++it);
                old_prev = it.prev();
                for (auto end = view.end(); steps > 0 && it != end; --steps, ++it);
                new_prev = it.curr();
                new_next = it.next();
            }

            // Otherwise, we need to create a lookahead iterator offset from the head
            // of the list by an amount equal to `steps`.  We then advance both
            // iterators until the lookahead iterator reaches the removal point.  When
            // this occurs, the lookbehind iterator will be at the insertion point.
            else {
                bool truncate = false;
                for (; steps < 0; ++steps, ++it) {  // `it` becomes lookahead iterator
                    if (it.curr() == node) {
                        old_prev = it.prev();
                        new_prev = nullptr;
                        new_next = view.head();
                        truncate = true;
                        break;
                    }
                }

                // generate lookbehind and advance until we find removal point
                if (!truncate) {
                    auto lookbehind = view.begin();
                    for (; it.curr() != node; ++it, ++lookbehind);
                    old_prev = it.prev();
                    new_prev = lookbehind.prev();
                    new_next = lookbehind.curr();
                }
            }
        }

        // move node to new position
        view.unlink(old_prev, node, old_next);
        view.link(new_prev, node, new_next);
    }


    /* Move an item to a particular index of a linked set or dictionary. */
    template <typename View, typename Item>
    auto move_to_index(View& view, const Item& item, long long index)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Node = typename View::Node;

        // search for node in hash table
        Node* node = view.search(item);
        if (node == nullptr) {
            std::ostringstream msg;
            msg << repr(item) << " is not in set";
            throw KeyError(msg.str());
        }

        // normalize index
        size_t norm_index = normalize_index(index, view.size(), true);

        // get prev pointers at both insertion and removal point
        Node* old_prev;
        Node* old_next = node->next();
        Node* new_prev;
        Node* new_next;
        if constexpr (NodeTraits<Node>::has_prev) {  // O(n / 2) if doubly-linked
            old_prev = node->prev();
            if (view.closer_to_tail(norm_index)) {
                auto it = view.rbegin();
                for (size_t i = view.size(); i > norm_index + 1; --i, ++it);
                new_prev = it.prev();
                new_next = it.curr();
            } else {
                auto it = view.begin();
                for (size_t i = 0; i < norm_index; ++i, ++it);
                new_prev = it.prev();
                new_next = it.curr();
            }

        } else {  // otherwise, O(n)
            old_prev = nullptr;
            auto it = view.begin();
            for (size_t i = 0; i < norm_index; ++i, ++it) {
                if (it.curr() == node) {
                    old_prev = it.prev();  // remember prev if we pass it
                    break;
                }
            }
            new_prev = it.prev();
            new_next = it.curr();
            if (old_prev == nullptr) {  // continue until we find prev
                for (; it.curr() != node; ++it);
                old_prev = it.prev();
            }
        }

        // trivial case: node is already at index
        if (new_prev == node || new_next == node) return;

        // move node to new position
        view.unlink(old_prev, node, old_next);
        view.link(new_prev, node, new_next);
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_MOVE_H
