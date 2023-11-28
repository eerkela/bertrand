// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_UNION_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_UNION_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_UNION_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/python.h"  // len()
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits
#include "update.h"  // update()


// TODO: memory reservation is somewhat iffy here.  If the view is not dynamic,
// then we won't end up shrinking at the end, and can end up with an enormous
// set that never shrinks.


namespace bertrand {
namespace structs {
namespace linked {


    /* Get the union between a linked set or dictionary and an arbitrary Python
    iterable. */
    template <typename View, typename Container, bool left = false>
    auto union_(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, View>
    {
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED |
            (left ? Allocator::INSERT_HEAD : Allocator::INSERT_TAIL)
        );

        // copy existing view
        View copy(view.size(), true, view.specialization());  // dynamic
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            copy.link(copy.tail(), copy.node(*(it.curr())), nullptr);
        }

        // add elements from items
        for (auto item : util::iter(items)) {
            copy.template node<flags>(item);
        }

        // if original view was not dynamic, move into new view
        if (!view.dynamic()) {
            // TODO: inefficient, but the only other way is to build a temporary
            // set of all the nodes that are not in the original view.
            View result(copy.size(), false, view.specialization());
            for (auto it = copy.begin(), end = copy.end(); it != end; ++it) {
                result.link(result.tail(), result.node(std::move(*(it.curr()))), nullptr);
            }
            return result;
        }
        return copy;
    }


    /* Get the union between a linked set or dictionary and an arbitrary Python
    iterable.  This method appends elements to the head of the set rather than the
    tail. */
    template <typename View, typename Container>
    inline auto union_left(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, View>
    {
        return union_<View, Container, true>(view, items);
    }


    /* Get the difference between a linked set or dictionary and an arbitrary Python
    iterable. */
    template <typename View, typename Container>
    auto difference(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, View>
    {
        using Node = typename View::Node;

        // iterate over items and mark all found nodes
        std::unordered_set<const Node*> found;
        for (auto item : util::iter(items)) {
            Node* node = view.search(item);
            if (node != nullptr) found.insert(node);
        }

        // add all elements that were not found
        View copy(view.size() - found.size(), view.dynamic(), view.specialization());
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            if (found.find(it.curr()) == found.end()) {
                copy.link(copy.tail(), copy.node(*(it.curr())), nullptr);
            }
        }
        return copy;
    }


    /* Get the intersection between a linked set or dictionary and an arbitrary Python
    iterable. */
    template <typename View, typename Container>
    auto intersection(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, View>
    {
        using Node = typename View::Node;

        // iterate over items and mark all found nodes
        std::unordered_set<const Node*> found;
        for (auto item : util::iter(items)) {
            Node* node = view.search(item);
            if (node != nullptr) found.insert(node);
        }

        // add all elements that were found
        View copy(found.size(), view.dynamic(), view.specialization());
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            if (found.find(it.curr()) != found.end()) {
                copy.link(copy.tail(), copy.node(*(it.curr())), nullptr);
            }
        }
        return copy;
    }


    /* Get the symmetric difference between a linked set or dictionary and an arbitrary
    Python iterable. */
    template <typename View, typename Container, bool left = false>
    auto symmetric_difference(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, View>
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

        // preallocate to exact size
        View copy(
            view.size() + temp_view.size(),
            view.dynamic(),
            view.specialization()
        );
        MemGuard hold = copy.reserve();  // hold allocator at current size

        // add all elements from view that are not in temp view
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            Node* node = temp_view.search(it.curr());
            if (node == nullptr) {
                copy.link(copy.tail(), copy.node(*(it.curr())), nullptr);
            }
        }

        // add all elements from temp view that are not in view
        for (auto it = temp_view.begin(), end = temp_view.end(); it != end; ++it) {
            Node* node = view.search(it.curr());
            if (node == nullptr) {
                node = copy.node(std::move(*(it.curr())));
                if constexpr (left) {
                    copy.link(nullptr, node, copy.head());
                } else {
                    copy.link(copy.tail(), node, nullptr);
                }
            }
        }

        return copy;
    }


    /* Get the symmetric difference between a linked set or dictionary and an arbitrary
    Python iterable.  This method appends elements to the head of the set rather than
    the tail. */
    template <typename View, typename Container>
    auto symmetric_difference_left(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, View>
    {
        return symmetric_difference<View, Container, true>(view, items);
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_UNION_H
