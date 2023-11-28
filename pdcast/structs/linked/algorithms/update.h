// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_UPDATE_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_UPDATE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_UPDATE_H

#include <cstddef>  // size_t
#include <type_traits>  // std::enable_if_t<>
#include <unordered_set>  // std::unordered_set<>
#include "../../util/iter.h"  // iter()
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    // TODO: dict compatibility uses an unordered_map rather than unordered_set for
    // auxiliary data structures.

    // TODO: if error correction in update() is supposed to work with dictionaries,
    // then we need some way of storing the old values.  This could be done with a
    // separate unordered_map.  We wouldn't be able to store node addresses though
    // due to possible reallocations.  Instead, we'd have to store the values
    // themselves.

    /* Update a set or dictionary, appending items that are not already present. */
    template <typename View, typename Container>
    auto update(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Allocator = typename View::Allocator;
        using MemGuard = typename View::MemGuard;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::INSERT_TAIL
        );

        // add each item
        size_t size = view.size();
        try {
            for (auto item : util::iter(items)) {
                view.template node<flags>(item);
            }

        // clean up nodes on error
        } catch (...) {
            size_t idx = view.size() - size;  // number of nodes to remove
            if (idx == 0) throw;  // nothing to clean up
            MemGuard inner = view.reserve();  // hold allocator at current size

            // remove last (view.size() - size) nodes
            if constexpr (NodeTraits<typename View::Node>::has_prev) {
                auto it = view.rbegin();
                for (size_t i = 0; i < idx; ++i) view.recycle(it.drop());
            } else {
                auto it = view.begin();  // forward traversal
                for (size_t i = 0; i < view.size() - idx; ++i) ++it;
                for (size_t i = 0; i < idx; ++i) view.recycle(it.drop());
            }
            throw;  // propagate
        }
    }


    /* Update a set or dictionary, appending items that are not already present. */
    template <typename View, typename Container>
    auto update_left(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Allocator = typename View::Allocator;
        using MemGuard = typename View::MemGuard;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::INSERT_HEAD
        );

        // add each item
        size_t size = view.size();
        try {
            for (auto item : util::iter(items)) {
                view.template node<flags>(item);
            }

        // clean up nodes on error
        } catch (...) {
            size_t idx = view.size() - size;  // number of nodes to remove
            if (idx == 0) throw;  // nothing to clean up
            MemGuard inner = view.reserve();  // hold allocator at current size

            // remove first (view.size() - size) nodes
            auto it = view.begin();
            for (size_t i = 0; i < idx; ++i) view.recycle(it.drop());
            throw;  // propagate
        }
    }


    /* Update a set or dictionary, adding or moving items to the head and evicting from
    the tail to make room. */
    template <typename View, typename Container>
    auto lru_update(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::INSERT_HEAD |
            Allocator::MOVE_HEAD | Allocator::EVICT_TAIL
        );

        // NOTE: This method is inherently destructive, so no attempt is made to return
        // the view to its original state if an error occurs.
        for (auto item : util::iter(items)) {
            view.template node<flags>(item);
        }
    }


    /* Update a linked set or dictionary in-place, removing elements from a second
    set or dictionary. */
    template <typename View, typename Container>
    auto difference_update(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Node = typename View::Node;
        using MemGuard = typename View::MemGuard;

        // hold allocator at current size
        MemGuard hold = view.reserve();

        // if set is doubly-linked, we can remove nodes while searching them to avoid
        // an extra loop
        if constexpr (NodeTraits<Node>::has_prev) {
            for (auto item : util::iter(items)) {
                Node* node = view.search(item);
                if (node != nullptr) {
                    view.unlink(node->prev(), node, node->next());
                    view.recycle(node);
                }
            }
            return;

        // otherwise, we iterate through the container and mark all nodes to remove
        } else {
            std::unordered_set<Node*> to_remove;
            for (auto item : util::iter(items)) {
                Node* node = view.search(item);
                if (node != nullptr) to_remove.insert(node);
            }

            // trivial case: no nodes to remove
            if (to_remove.empty()) return;

            // iterate through the view and remove the marked nodes
            for (auto it = view.begin(), end = view.end(); it != end;) {
                if (to_remove.find(it.curr()) != to_remove.end()) {
                    view.recycle(it.drop());  // implicitly advances iterator
                } else {
                    ++it;
                }
            }
        }
    }


    /* Update a linked set or dictionary in-place, keeping only elements found in
    both sets or dictionaries. */
    template <typename View, typename Container>
    auto intersection_update(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Node = typename View::Node;
        using MemGuard = typename View::MemGuard;

        // hold allocator at current size
        MemGuard hold = view.reserve();

        // iterate through the container and mark all nodes to keep
        std::unordered_set<Node*> keep;
        for (auto item : util::iter(items)) {
            Node* node = view.search(item);
            if (node != nullptr) keep.insert(node);
        }

        // trivial case: no nodes to keep
        if (keep.empty()) {
            view.clear();
            return;
        }

        // iterate through view and remove any unmarked nodes
        for (auto it = view.begin(), end = view.end(); it != end;) {
            if (keep.find(it.curr()) == keep.end()) {
                view.recycle(it.drop());  // implicitly advances iterator
            } else {
                ++it;
            }
        }
    }


    /* Update a linked set or dictionary in-place, keeping only elements found in
    either the set or a given container, but not both. */
    template <typename View, typename Container, bool left = false>
    auto symmetric_difference_update(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Node = typename View::Node;
        using MemGuard = typename View::MemGuard;

        // unpack items into temporary view
        View temp_view(
            items,
            std::nullopt,  // capacity: inferred from items
            true,  // dynamic: true
            nullptr,  // specialization: generic
            false  // reverse: false
        );

        // keep track of nodes as they are added in order to avoid cycles
        std::unordered_set<Node*> added;

        // if doubly-linked, we can remove nodes while searching to avoid an extra loop
        if constexpr (NodeTraits<Node>::has_prev) {
            for (auto it = temp_view.begin(), end = temp_view.end(); it != end; ++it) {
                Node* node = view.search(it.curr());

                // if node is not present, move it into the original view
                if (node == nullptr) {
                    node = view.node(std::move(*(it.curr())));
                    if constexpr (left) {
                        view.link(nullptr, node, view.head());
                    } else {
                        view.link(view.tail(), node, nullptr);
                    }
                    added.insert(node);

                // if node was not previously added, remove it
                } else if (added.find(node) == added.end()) {
                    view.unlink(node->prev(), node, node->next());
                    view.recycle(node);
                }
            }

        // otherwise, mark nodes to remove and then remove them in a separate loop
        } else {
            std::unordered_set<Node*> to_remove;
            for (auto it = temp_view.begin(), end = temp_view.end(); it != end; ++it) {
                Node* node = view.search(it.curr());

                // if node is not present, move it into the original view
                if (node == nullptr) {
                    node = view.node(std::move(*(it.curr())));
                    if constexpr (left) {
                        view.link(nullptr, node, view.head());
                    } else {
                        view.link(view.tail(), node, nullptr);
                    }
                    added.insert(node);

                // if node was not previously added, remove it
                } else if (added.find(node) == added.end()) {
                    to_remove.insert(node);
                }
            }

            // remove marked nodes
            MemGuard inner = view.reserve();  // hold allocator at current size
            for (auto it = view.begin(), end = view.end(); it != end;) {
                if (to_remove.find(it.curr()) != to_remove.end()) {
                    view.recycle(it.drop());  // implicitly advances iterator
                } else {
                    ++it;
                }
            }
        }
    }

    /* Update a linked set or dictionary in-place, keeping only elements found in
    either the set or a given container, but not both.  This method appends elements to
    the head of the set rather than the tail. */
    template <typename View, typename Container>
    auto symmetric_difference_update_left(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        symmetric_difference_update<View, Container, true>(view, items);
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_UPDATE_H
