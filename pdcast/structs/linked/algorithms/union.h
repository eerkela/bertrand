#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_UNION_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_UNION_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/container.h"  // python::Dict
#include "../../util/iter.h"  // iter()
#include "../../util/ops.h"  // len()
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits
#include "update.h"  // update()


// TODO: intersection should use value from other container if it is dictlike?

// TODO: work in Yield::VALUE/ITEM for dictlike views.


namespace bertrand {
namespace linked {


    //////////////////////
    ////    PUBLIC    ////
    //////////////////////


    /* Get the union between a linked set or dictionary and an arbitrary container. */
    template <bool left, typename View, typename Container>
    inline auto union_(const View& view, const Container& items)
        -> std::enable_if_t<
            ViewTraits<View>::hashed,
            typename ViewTraits<View>::As::DYNAMIC
        >
    {
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | 
            (left ? Allocator::INSERT_HEAD : Allocator::INSERT_TAIL)
        );

        DynamicView result(view.size(), view.specialization());
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            result.template node<Allocator::INSERT_TAIL>(*(it.curr()));
        }

        if constexpr (ViewTraits<View>::dictlike && is_pyobject<Container>) {
            if (PyDict_Check(items)) {
                python::Dict<python::Ref::BORROW> dict(items);
                for (const auto& item : iter(dict)) {
                    result.template node<flags>(item);
                }
                return result;
            }
        }

        for (const auto& item : iter(items)) {
            result.template node<flags>(item);
        }
        return result;
    }


    /* Get the difference between a linked set or dictionary and an arbitrary Python
    iterable. */
    template <typename View, typename Container>
    auto difference(const View& view, const Container& items)
        -> std::enable_if_t<
            ViewTraits<View>::hashed,
            typename ViewTraits<View>::As::DYNAMIC
        >
    {
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using Allocator = typename View::Allocator;
        using Node = typename View::Node;

        std::unordered_set<const Node*> found;
        for (const auto& item : iter(items)) {
            const Node* node = view.search(item);  // TODO: this fails if item is a key-value pair.
            if (node != nullptr) {
                found.insert(node);
            }
        }

        DynamicView result(view.size() - found.size(), view.specialization());
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            if (found.find(it.curr()) == found.end()) {
                result.template node<Allocator::INSERT_TAIL>(*(it.curr()));
            }
        }
        return result;
    }


    // TODO: if yield = Yield::ITEM, then the result should use values from the
    // other container if it is dictlike.


    /* Get the intersection between a linked set or dictionary and an arbitrary Python
    iterable. */
    template <typename View, typename Container>
    auto intersection(const View& view, const Container& items)
        -> std::enable_if_t<
            ViewTraits<View>::hashed,
            typename ViewTraits<View>::As::DYNAMIC
        >
    {
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using Allocator = typename View::Allocator;
        using Node = typename View::Node;

        std::unordered_set<const Node*> found;
        for (const auto& item : iter(items)) {
            const Node* node = view.search(item);
            if (node != nullptr) {
                found.insert(node);
            }
        }

        DynamicView result(found.size(), view.specialization());
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            if (found.find(it.curr()) != found.end()) {
                result.template node<Allocator::INSERT_TAIL>(*(it.curr()));
            }
        }
        return result;
    }


    /* Get the symmetric difference between a linked set or dictionary and an arbitrary
    Python iterable. */
    template <bool left, typename View, typename Container>
    inline auto symmetric_difference(const View& view, const Container& items)
        -> std::enable_if_t<
            ViewTraits<View>::hashed,
            typename ViewTraits<View>::As::DYNAMIC
        >
    {
        using TempView = typename ViewTraits<View>::template Reconfigure<
            Config::SINGLY_LINKED
        >;
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using Allocator = typename View::Allocator;
        using Node = typename View::Node;

        TempView temp_view(
            items,
            std::nullopt,  // capacity: dynamic
            nullptr,  // specialization: generic
            false  // reverse: false
        );

        DynamicView result(std::nullopt, view.specialization());

        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            Node* node = temp_view.search(it.curr());
            if (node == nullptr) {
                result.template node<Allocator::INSERT_TAIL>(*(it.curr()));
            }
        }

        for (auto it = temp_view.begin(), end = temp_view.end(); it != end; ++it) {
            const Node* node = view.search(it.curr());
            if (node == nullptr) {
                if constexpr (left) {
                    result.template node<Allocator::INSERT_HEAD>(std::move(*(it.curr())));
                } else {
                    result.template node<Allocator::INSERT_TAIL>(std::move(*(it.curr())));
                }
            }
        }

        return result;
    }


}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_UNION_H
