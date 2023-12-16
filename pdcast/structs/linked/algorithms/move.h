#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_MOVE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_MOVE_H

#include <cstddef>  // size_t
#include <type_traits>  // std::enable_if_t<>
#include "../../util/ops.h"  // repr()
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits, Direction
#include "position.h"  // normalize_index()


namespace bertrand {
namespace linked {


    /* Move an item within a linked set or dictionary. */
    template <typename View, typename Item>
    auto move(View& view, const Item& item, long long steps)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Node = typename View::Node;

        Node* node = view.search(item);
        if (node == nullptr) {
            throw KeyError(repr(item));
        }

        if (steps == 0 || node == (steps > 0 ? view.tail() : view.head())) {
            return;
        }

        // get neighbors at both insertion and removal point
        Node* old_prev;
        Node* old_next = node->next();
        Node* new_prev;
        Node* new_next;
        if constexpr (NodeTraits<Node>::has_prev) {  // O(m) if doubly-linked
            old_prev = node->prev();
            if (steps > 0) {
                using Iter = typename View::template Iterator<Direction::FORWARD>;
                auto it = Iter(view, old_prev, node, old_next);
                while (steps > 0 && it.next() != nullptr) {
                    --steps;
                    ++it;
                }
                new_prev = it.curr();
                new_next = it.next();
            } else {
                using Iter = typename View::template Iterator<Direction::BACKWARD>;
                auto it = Iter(view, old_prev, node, old_next);
                while (steps < 0 && it.prev() != nullptr) {
                    ++steps;
                    ++it;
                }
                new_prev = it.prev();
                new_next = it.curr();
            }

        } else {  // otherwise, O(n + m) using lookahead iterators
            auto it = view.begin();

            // if we're moving forward, then there's no need for lookahead
            if (steps > 0) {
                for (; it.curr() != node; ++it);
                old_prev = it.prev();
                while (steps > 0 && it.next() != nullptr) {
                    --steps;
                    ++it;
                }
                new_prev = it.curr();
                new_next = it.next();

            // lookahead iterator is offset from head by an amount equal to `steps`.
            // When it reaches the removal point, lookbehind will be at insertion point.
            } else {
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

                // construct lookbehind and advance both until we find removal point
                if (!truncate) {
                    auto lookbehind = view.begin();
                    for (; it.curr() != node; ++it, ++lookbehind);
                    old_prev = it.prev();
                    new_prev = lookbehind.prev();
                    new_next = lookbehind.curr();
                }
            }
        }

        view.unlink(old_prev, node, old_next);
        view.link(new_prev, node, new_next);
    }


    /* Move a key to a particular index of a linked set or dictionary. */
    template <typename View, typename Item>
    auto move_to_index(View& view, const Item& item, long long index)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Node = typename View::Node;

        Node* node = view.search(item);
        if (node == nullptr) {
            throw KeyError(repr(item));
        }

        size_t norm_index = normalize_index(index, view.size(), true);

        // get neighbors at both insertion and removal point
        Node* old_prev;
        Node* old_next = node->next();
        Node* new_prev;
        Node* new_next;
        if constexpr (NodeTraits<Node>::has_prev) {  // O(n / 2) if doubly-linked
            old_prev = node->prev();
            if (view.closer_to_tail(norm_index)) {
                auto it = view.rbegin();
                for (size_t i = view.size() - 1; i > norm_index; ++it) {
                    i -= (it.curr() != node);
                }
                if (it.curr() == node) {
                    return;
                }
                new_prev = it.curr();
                new_next = it.next();
            } else {
                auto it = view.begin();
                for (size_t i = 0; i < norm_index; ++it) {
                    i += (it.curr() != node);
                }
                if (it.curr() == node) {
                    return;
                }
                new_prev = it.prev();
                new_next = it.curr();
            }

        } else {  // otherwise, O(n)
            bool found = false;
            auto it = view.begin();
            for (size_t i = 0; i < norm_index; ++it) {
                if (it.curr() == node) {
                    found = true;
                    old_prev = it.prev();  // remember prev if we pass over it
                } else {
                    ++i;
                }
            }
            if (it.curr() == node) {
                return;
            }
            new_prev = it.prev();
            new_next = it.curr();
            if (!found) {  // continue until we find prev
                for (; it.curr() != node; ++it);
                old_prev = it.prev();
            }
        }

        view.unlink(old_prev, node, old_next);
        view.link(new_prev, node, new_next);
    }


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_MOVE_H
