#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_UPDATE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_UPDATE_H

#include <cstddef>  // size_t
#include <type_traits>  // std::enable_if_t<>
#include <unordered_set>  // std::unordered_set<>
#include "../../util/container.h"  // python::Dict
#include "../../util/iter.h"  // iter()
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits


// TODO: intersection should overwrite values for dictlike views.


namespace bertrand {
namespace linked {


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
            for (const auto& item : iter(items)) {
                view.template node<flags>(item);
            }

        } catch (...) {
            /* NOTE: no attempt is made to restore the original values if an error
             * occurs during a dictlike update using REPLACE_MAPPED.  Handling this
             * would add significant complexity and reduce performance for the intended
             * path where no errors occur.  Extra keys will still be removed like
             * normal, but any that have replaced an existing value will remain.
             */

            size_t idx = view.size() - size;
            if (idx == 0) {
                throw;
            }

            // hold allocator at current size while we remove nodes
            MemGuard inner = view.reserve();
            if constexpr (left) {
                auto it = view.begin();
                for (size_t i = 0; i < idx; ++i) {
                    view.recycle(it.drop());
                }
            } else {
                if constexpr (NodeTraits<typename View::Node>::has_prev) {
                    auto it = view.rbegin();
                    for (size_t i = 0; i < idx; ++i) {
                        view.recycle(it.drop());
                    }
                } else {
                    auto it = view.begin();
                    for (size_t i = 0; i < view.size() - idx; ++i, ++it);
                    for (size_t i = 0; i < idx; ++i) {
                        view.recycle(it.drop());
                    }
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
        for (const auto& item : iter(items)) {
            view.template node<flags>(item);
        }
    }


    /* Container-independent implementation for symmetric_difference_update(). */
    template <typename View, typename Container, bool left>
    void symmetric_difference_update_impl(View& view, const Container& items) {
        using TempView = typename ViewTraits<View>::template Reconfigure<
            Config::SINGLY_LINKED
        >;
        using Node = typename View::Node;
        using MemGuard = typename View::MemGuard;

        TempView temp_view(
            items,
            std::nullopt,  // capacity: inferred from items
            nullptr,  // specialization: generic
            false  // reverse: false
        );
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

            // hold allocator at current size while we remove nodes
            MemGuard inner = view.reserve();
            for (auto it = view.begin(), end = view.end(); it != end;) {
                if (to_remove.find(it.curr()) != to_remove.end()) {
                    view.recycle(it.drop());
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
        if constexpr (ViewTraits<View>::dictlike && is_pyobject<Container>) {
            if (PyDict_Check(items)) {
                using PyDict = python::Dict<python::Ref::BORROW>;
                PyDict dict(items);
                update_impl<View, PyDict, false>(view, dict);
                return;
            }
        }
        update_impl<View, Container, false>(view, items);
    }


    /* Update a set or dictionary, appending items that are not already present. */
    template <typename View, typename Container>
    inline auto update_left(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        if constexpr (ViewTraits<View>::dictlike && is_pyobject<Container>) {
            if (PyDict_Check(items)) {
                using PyDict = python::Dict<python::Ref::BORROW>;
                PyDict dict(items);
                update_impl<View, PyDict, true>(view, dict);
                return;
            }
        }
        update_impl<View, Container, true>(view, items);
    }


    /* Update a set or dictionary, adding or moving items to the head and evicting from
    the tail to make room. */
    template <typename View, typename Container>
    inline auto lru_update(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        if constexpr (ViewTraits<View>::dictlike && is_pyobject<Container>) {
            if (PyDict_Check(items)) {
                using PyDict = python::Dict<python::Ref::BORROW>;
                PyDict dict(items);
                lru_update_impl(view, dict);
                return;
            }
        }
        lru_update_impl(view, items);
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
            for (const auto& item : iter(items)) {
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
            for (const auto& item : iter(items)) {
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
        for (const auto& item : iter(items)) {
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
        if constexpr (ViewTraits<View>::dictlike && is_pyobject<Container>) {
            if (PyDict_Check(items)) {
                using PyDict = python::Dict<python::Ref::BORROW>;
                PyDict dict(items);
                symmetric_difference_update_impl<View, PyDict, false>(view, dict);
                return;
            }
        }
        symmetric_difference_update_impl<View, Container, false>(view, items);
    }


    /* Update a linked set or dictionary in-place, keeping only elements found in
    either the set or a given container, but not both.  This method appends elements to
    the head of the set rather than the tail. */
    template <typename View, typename Container>
    inline auto symmetric_difference_update_left(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        if constexpr (ViewTraits<View>::dictlike && is_pyobject<Container>) {
            if (PyDict_Check(items)) {
                using PyDict = python::Dict<python::Ref::BORROW>;
                PyDict dict(items);
                symmetric_difference_update_impl<View, PyDict, true>(view, dict);
                return;
            }
        }
        symmetric_difference_update_impl<View, Container, true>(view, items);
    }


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_UPDATE_H
