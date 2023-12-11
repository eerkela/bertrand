#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_UNION_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_UNION_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/container.h"  // PyDict
#include "../../util/iter.h"  // iter()
#include "../../util/ops.h"  // len()
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits
#include "update.h"  // update()


// TODO: intersection should use value from other container if it is dictlike?

// TODO: work in Yield::VALUE/ITEM for dictlike views.


namespace bertrand {
namespace linked {


    ///////////////////////
    ////    PRIVATE    ////
    ///////////////////////


    /* Container-independent implementation for union_(). */
    template <typename View, typename Container, bool left>
    View union_impl(const View& view, const Container& items) {
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using FixedView = typename ViewTraits<View>::As::FIXED_SIZE;
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED |
            (left ? Allocator::INSERT_HEAD : Allocator::INSERT_TAIL)
        );

        DynamicView copy(view.size(), view.specialization());
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            copy.template node<Allocator::INSERT_TAIL>(*(it.curr()));
        }

        for (const auto& item : iter(items)) {
            copy.template node<flags>(item);
        }

        if constexpr (ViewTraits<View>::FIXED_SIZE) {
            FixedView result(copy.size(), view.specialization());
            for (auto it = copy.begin(), end = copy.end(); it != end; ++it) {
                result.template node<Allocator::INSERT_TAIL>(std::move(*(it.curr())));
            }
            return result;
        } else {
            return copy;
        }
    }


    /* Container-independent implementation for symmetric_difference(). */
    template <typename View, typename Container, bool left>
    View symmetric_difference_impl(const View& view, const Container& items) {
        using TempView = typename View::template Reconfigure<
            Config::SINGLY_LINKED | Config::DYNAMIC
        >;
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using FixedView = typename ViewTraits<View>::As::FIXED_SIZE;
        using Allocator = typename View::Allocator;
        using Node = typename View::Node;

        TempView temp_view(
            items,
            std::nullopt,  // capacity: dynamic
            nullptr,  // specialization: generic
            false  // reverse: false
        );

        DynamicView copy(std::nullopt, view.specialization());

        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            Node* node = temp_view.search(it.curr());
            if (node == nullptr) {
                copy.template node<Allocator::INSERT_TAIL>(*(it.curr()));
            }
        }

        for (auto it = temp_view.begin(), end = temp_view.end(); it != end; ++it) {
            const Node* node = view.search(it.curr());
            if (node == nullptr) {
                if constexpr (left) {
                    copy.template node<Allocator::INSERT_HEAD>(std::move(*(it.curr())));
                } else {
                    copy.template node<Allocator::INSERT_TAIL>(std::move(*(it.curr())));
                }
            }
        }

        if constexpr (ViewTraits<View>::FIXED_SIZE) {
            FixedView result(copy.size(), view.specialization());
            for (auto it = copy.begin(), end = copy.end(); it != end; ++it) {
                result.template node<Allocator::INSERT_TAIL>(std::move(*(it.curr())));
            }
            return result;
        } else {
            return copy;
        }
    }


    //////////////////////
    ////    PUBLIC    ////
    //////////////////////


    /* Get the union between a linked set or dictionary and an arbitrary Python
    iterable. */
    template <typename View, typename Container>
    inline auto union_(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, View>
    {
        if constexpr (ViewTraits<View>::dictlike && is_pyobject<Container>) {
            if (PyDict_Check(items)) {
                PyDict dict(items);
                return union_impl<View, PyDict, false>(view, dict);
            }
        }
        return union_impl<View, Container, false>(view, items);
    }


    /* Get the union between a linked set or dictionary and an arbitrary Python
    iterable.  This method appends elements to the head of the set rather than the
    tail. */
    template <typename View, typename Container>
    inline auto union_left(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, View>
    {
        if constexpr (ViewTraits<View>::dictlike && is_pyobject<Container>) {
            if (PyDict_Check(items)) {
                PyDict dict(items);
                return union_impl<View, PyDict, true>(view, dict);
            }
        }
        return union_impl<View, Container, true>(view, items);
    }


    /* Get the difference between a linked set or dictionary and an arbitrary Python
    iterable. */
    template <typename View, typename Container>
    auto difference(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, View>
    {
        using Allocator = typename View::Allocator;
        using Node = typename View::Node;

        std::unordered_set<const Node*> found;
        for (const auto& item : iter(items)) {
            const Node* node = view.search(item);
            if (node != nullptr) {
                found.insert(node);
            }
        }

        View copy(view.size() - found.size(), view.specialization());
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            if (found.find(it.curr()) == found.end()) {
                copy.template node<Allocator::INSERT_TAIL>(*(it.curr()));
            }
        }
        return copy;
    }


    // TODO: if yield = Yield::ITEM, then the result should use values from the
    // other container if it is dictlike.


    /* Get the intersection between a linked set or dictionary and an arbitrary Python
    iterable. */
    template <typename View, typename Container>
    auto intersection(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, View>
    {
        using Allocator = typename View::Allocator;
        using Node = typename View::Node;

        std::unordered_set<const Node*> found;
        for (const auto& item : iter(items)) {
            const Node* node = view.search(item);
            if (node != nullptr) {
                found.insert(node);
            }
        }

        View copy(found.size(), view.specialization());
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            if (found.find(it.curr()) != found.end()) {
                copy.template node<Allocator::INSERT_TAIL>(*(it.curr()));
            }
        }
        return copy;
    }


    /* Get the symmetric difference between a linked set or dictionary and an arbitrary
    Python iterable. */
    template <typename View, typename Container>
    inline auto symmetric_difference(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, View>
    {
        if constexpr (ViewTraits<View>::dictlike && is_pyobject<Container>) {
            if (PyDict_Check(items)) {
                PyDict dict(items);
                return symmetric_difference_impl<View, PyDict, false>(view, dict);
            }
        }
        return symmetric_difference_impl<View, Container, false>(view, items);
    }


    /* Get the symmetric difference between a linked set or dictionary and an arbitrary
    Python iterable.  This method appends elements to the head of the set rather than
    the tail. */
    template <typename View, typename Container>
    inline auto symmetric_difference_left(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, View>
    {
        if constexpr (ViewTraits<View>::dictlike && is_pyobject<Container>) {
            if (PyDict_Check(items)) {
                PyDict dict(items);
                return symmetric_difference_impl<View, PyDict, true>(view, dict);
            }
        }
        return symmetric_difference_impl<View, Container, true>(view, items);
    }


}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_UNION_H
