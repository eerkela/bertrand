#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_UPDATE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_UPDATE_H

#include <cstddef>  // size_t
#include <type_traits>  // std::enable_if_t<>
#include <unordered_set>  // std::unordered_set<>
#include "../../util/container.h"  // PyDict
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


    ///////////////////////
    ////    PRIVATE    ////
    ///////////////////////

    /* Container-independent implementation for update(). */
    template <typename View, typename Container, bool left>
    void update_impl(View& view, const Container& items) {
        using Allocator = typename View::Allocator;
        using MemGuard = typename View::MemGuard;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED |
            (left ? Allocator::INSERT_HEAD : Allocator::INSERT_TAIL)
        );

        size_t size = view.size();
        try {
            for (auto item : iter(items)) {
                view.template node<flags>(item);
            }

        // restore set on error
        } catch (...) {
            size_t idx = view.size() - size;
            if (idx == 0) {
                throw;
            }

            // hold allocator at current size while we remove nodes
            MemGuard inner = view.reserve();
            if constexpr (left) {
                auto it = view.begin();
                for (size_t i = 0; i < idx; ++i) view.recycle(it.drop());
            } else {
                if constexpr (NodeTraits<typename View::Node>::has_prev) {
                    auto it = view.rbegin();
                    for (size_t i = 0; i < idx; ++i) view.recycle(it.drop());
                } else {
                    auto it = view.begin();
                    for (size_t i = 0; i < view.size() - idx; ++i) ++it;
                    for (size_t i = 0; i < idx; ++i) view.recycle(it.drop());
                }
            }
            throw;
        }
    }


    /* Container-independent implementation for lru_update(). */
    template <typename View, typename Container>
    inline void lru_update_impl(View& view, const Container& items) {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::INSERT_HEAD |
            Allocator::MOVE_HEAD | Allocator::EVICT_TAIL
        );

        // NOTE: This method is inherently destructive, so no attempt is made to return
        // the view to its original state if an error occurs.
        for (auto item : iter(items)) {
            view.template node<flags>(item);
        }
    }


    /* Container-independent implementation for symmetric_difference_update(). */
    template <typename View, typename Container, bool left>
    void symmetric_difference_update_impl(View& view, const Container& items) {
        using TempView = typename View::template Reconfigure<
            Config::SINGLY_LINKED | Config::DYNAMIC
        >;
        using Node = typename View::Node;
        using MemGuard = typename View::MemGuard;

        // unpack items into temporary view
        TempView temp_view(
            items,
            std::nullopt,  // capacity: inferred from items
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


    //////////////////////
    ////    PUBLIC    ////
    //////////////////////


    /* Update a set or dictionary, appending items that are not already present. */
    template <typename View, typename Container>
    inline auto update(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        update_impl<View, Container, false>(view, items);
    }


    /* A special case of update() for dictlike views that accounts for Python dict
    inputs. */
    template <typename View>
    inline auto update(View& view, const PyObject* items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        // wrap Python dictionaries to yield key-value pairs during iteration
        if (PyDict_Check(items)) {
            PyDict dict(items);
            update_impl<View, PyDict, false>(view, dict);
        } else {
            update_impl<View, PyObject*, false>(view, items);
        }
    }


    /* Update a set or dictionary, appending items that are not already present. */
    template <typename View, typename Container>
    inline auto update_left(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        update_impl<View, Container, true>(view, items);
    }


    /* Wrap Python dictionaries to yield key-value pairs during iteration. */
    template <typename View>
    inline auto update_left(View& view, const PyObject* items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        if (PyDict_Check(items)) {
            PyDict dict(items);
            update_impl<View, PyDict, true>(view, dict);
        } else {
            update_impl<View, PyObject*, true>(view, items);
        }
    }


    /* Update a set or dictionary, adding or moving items to the head and evicting from
    the tail to make room. */
    template <typename View, typename Container>
    inline auto lru_update(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        lru_update_impl(view, items);
    }


    /* Wrap Python dictionaries to yield key-value pairs during iteration. */
    template <typename View>
    inline auto lru_update(View& view, const PyObject* items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        if (PyDict_Check(items)) {
            PyDict dict(items);
            lru_update_impl(view, dict);
        } else {
            lru_update_impl(view, items);
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
            for (auto item : iter(items)) {
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
            for (auto item : iter(items)) {
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
        for (auto item : iter(items)) {
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
    template <typename View, typename Container>
    inline auto symmetric_difference_update(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        symmetric_difference_update_impl<View, Container, false>(view, items);
    }


    /* A special case of update() for dictlike views that accounts for Python dict
    inputs. */
    template <typename View>
    inline auto symmetric_difference_update(View& view, const PyObject* items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        // wrap dictionaries to yield key-value pairs during iteration
        if (PyDict_Check(items)) {
            PyDict dict(items);
            symmetric_difference_update_impl<View, PyDict, false>(view, dict);

        // otherwise, iterate through items directly
        } else {
            symmetric_difference_update_impl<View, PyObject*, false>(view, items);
        }
    }


    /* Update a linked set or dictionary in-place, keeping only elements found in
    either the set or a given container, but not both.  This method appends elements to
    the head of the set rather than the tail. */
    template <typename View, typename Container>
    inline auto symmetric_difference_update_left(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        symmetric_difference_update_impl<View, Container, true>(view, items);
    }


    /* A special case of symmetric_difference_update_left() for dictlike views that
    accounts for Python dict inputs. */
    template <typename View>
    inline auto symmetric_difference_update_left(View& view, const PyObject* items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        // wrap dictionaries to yield key-value pairs during iteration
        if (PyDict_Check(items)) {
            PyDict dict(items);
            symmetric_difference_update_impl<View, PyDict, true>(view, dict);

        // otherwise, iterate through items directly
        } else {
            symmetric_difference_update_impl<View, PyObject*, true>(view, items);
        }
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_UPDATE_H
