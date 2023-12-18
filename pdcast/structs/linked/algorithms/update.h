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


    /* Update a linked set to hold the union of its current keys as well as those of an
    arbitrary container. */
    template <bool left = false, typename View, typename Container>
    auto update(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Allocator = typename View::Allocator;
        using MemGuard = typename View::MemGuard;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED |
            (left ? Allocator::INSERT_HEAD : Allocator::INSERT_TAIL)
        );

        size_t size = view.size();
        try {
            if constexpr (ViewTraits<View>::dictlike && is_pyobject<Container>) {
                if (PyDict_Check(items)) {
                    python::Dict<python::Ref::BORROW> dict(items);
                    for (const auto& item : iter(dict)) {
                        view.template node<flags>(item);
                    }
                    return;
                }
            }

            for (const auto& item : iter(items)) {
                view.template node<flags>(item);
            }

        } catch (...) {
            /* NOTE: for dictlike views, no attempt is made to restore the original
             * values associated with each key if an error occurs.  Handling this would
             * add significant complexity and reduce performance for the intended path
             * where no errors occur.  Extra keys will still be removed like normal,
             * but any whose values have been overwritten will remain.
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


    /* Update a set or dictionary, adding or moving items to the head and evicting from
    the tail to make room. */
    template <typename View, typename Container>
    inline auto lru_update(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::INSERT_HEAD |
            Allocator::MOVE_HEAD | Allocator::EVICT_TAIL
        );

        // NOTE: This method is inherently destructive, so no attempt is made to return
        // the view to its original state if an error occurs.

        if constexpr (ViewTraits<View>::dictlike && is_pyobject<Container>) {
            if (PyDict_Check(items)) {
                python::Dict<python::Ref::BORROW> dict(items);
                for (const auto& item : iter(dict)) {
                    view.template node<flags>(item);
                }
                return;
            }
        }

        for (const auto& item : iter(items)) {
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

        // if doubly-linked, we can remove nodes while searching to avoid an extra loop
        if constexpr (NodeTraits<Node>::has_prev) {
            for (const auto& item : iter(items)) {
                using Item = std::decay_t<decltype(item)>;
                if constexpr (ViewTraits<View>::dictlike && is_pairlike<Item>) {
                    Node* node = view.search(std::get<0>(item));
                    if (node != nullptr) {
                        view.unlink(node->prev(), node, node->next());
                        view.recycle(node);
                    }
                } else {
                    Node* node = view.search(item);
                    if (node != nullptr) {
                        view.unlink(node->prev(), node, node->next());
                        view.recycle(node);
                    }
                }
            }

        // otherwise, mark and sweep
        } else {
            std::unordered_set<Node*> to_remove;
            for (const auto& item : iter(items)) {
                using Item = std::decay_t<decltype(item)>;
                if constexpr (ViewTraits<View>::dictlike && is_pairlike<Item>) {
                    Node* node = view.search(std::get<0>(item));
                    if (node != nullptr) {
                        to_remove.insert(node);
                    }
                } else {
                    Node* node = view.search(item);
                    if (node != nullptr) {
                        to_remove.insert(node);
                    }
                }
            }

            if (to_remove.empty()) {
                return;
            }

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
        -> std::enable_if_t<ViewTraits<View>::setlike, void>
    {
        using Node = typename View::Node;
        using MemGuard = typename View::MemGuard;

        // hold allocator at current size
        MemGuard hold = view.reserve();

        std::unordered_set<Node*> keep;
        for (const auto& item : iter(items)) {
            Node* node = view.search(item);
            if (node != nullptr) {
                keep.insert(node);
            }
        }

        if (keep.empty()) {
            view.clear();
            return;
        }

        for (auto it = view.begin(), end = view.end(); it != end;) {
            if (keep.find(it.curr()) == keep.end()) {
                view.recycle(it.drop());  // implicitly advances iterator
            } else {
                ++it;
            }
        }
    }


    /* Update a linked set or dictionary in-place, keeping only elements found in
    both sets or dictionaries. */
    template <typename View, typename Container>
    auto intersection_update(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, void>
    {
        using Node = typename View::Node;
        using MemGuard = typename View::MemGuard;
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED
        );

        // hold allocator at current size
        MemGuard hold = view.reserve();

        std::unordered_map<Node*, typename View::MappedValue> keep;
        if constexpr (is_pyobject<Container>) {
            if (PyDict_Check(items)) {
                python::Dict<python::Ref::BORROW> dict(items);
                for (const auto& item : iter(dict)) {
                    Node* node = view.search(std::get<0>(item));
                    if (node != nullptr) {
                        keep.insert({node, std::get<1>(item)});
                    }
                }
            } else {
                for (const auto& item : iter(items)) {
                    Node* node = view.search(item);
                    if (node != nullptr) {
                        keep.insert({node, node->mapped()});
                    }
                }
            }
        } else {
            for (const auto& item : iter(items)) {
                if constexpr (is_pairlike<std::decay_t<decltype(item)>>) {
                    Node* node = view.search(std::get<0>(item));
                    if (node != nullptr) {
                        keep.insert({node, std::get<1>(item)});
                    }
                } else {
                    Node* node = view.search(item);
                    if (node != nullptr) {
                        keep.insert({node, node->mapped()});
                    }
                }
            }
        }

        if (keep.empty()) {
            view.clear();
            return;
        }

        auto it = view.template begin<Yield::KEY>();
        auto end = view.template end<Yield::KEY>();
        while (it != end) {
            auto found_it = keep.find(it.curr());
            if (found_it == keep.end()) {
                view.recycle(it.drop());  // implicitly advances iterator
            } else {
                view.template node<flags>(*it, found_it->second);
                ++it;
            }
        }
    }


    /* Update a linked set or dictionary in-place, keeping only elements found in
    either the set or a given container, but not both. */
    template <bool left = false, typename View, typename Container>
    inline auto symmetric_difference_update(View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, void>
    {
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


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_UPDATE_H
