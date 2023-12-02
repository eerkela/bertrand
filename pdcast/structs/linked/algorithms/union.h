#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_UNION_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_UNION_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/container.h"  // PyDict
#include "../../util/iter.h"  // iter()
#include "../../util/ops.h"  // len()
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits
#include "update.h"  // update()


namespace bertrand {
namespace structs {
namespace linked {


    ///////////////////////
    ////    PRIVATE    ////
    ///////////////////////


    /* Container-independent implementation for union_(). */
    template <typename View, typename Container, bool left>
    View union_impl(const View& view, const Container& items) {
        using DynamicView = typename ViewTraits<View>::As::Dynamic;
        using FixedView = typename ViewTraits<View>::As::FixedSize;
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED |
            (left ? Allocator::INSERT_HEAD : Allocator::INSERT_TAIL)
        );

        // copy existing view
        DynamicView copy(view.size(), view.specialization());
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            copy.template node<Allocator::INSERT_TAIL>(*(it.curr()));
        }

        // allow copy to grow dynamically
        for (auto item : iter(items)) {
            copy.template node<flags>(item);
        }

        // if original view was not dynamic, move into new view of fixed size
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
        using DynamicView = typename ViewTraits<View>::As::Dynamic;
        using FixedView = typename ViewTraits<View>::As::FixedSize;
        using Allocator = typename View::Allocator;
        using Node = typename View::Node;

        // unpack items into temporary view
        TempView temp_view(
            items,
            std::nullopt,  // capacity: dynamic
            nullptr,  // specialization: generic
            false  // reverse: false
        );

        // allocate dynamic view to store result
        DynamicView copy(std::nullopt, view.specialization());

        // add all elements from view that are not in temp view
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            Node* node = temp_view.search(it.curr());
            if (node == nullptr) {
                copy.template node<Allocator::INSERT_TAIL>(*(it.curr()));
            }
        }

        // add all elements from temp view that are not in view
        for (auto it = temp_view.begin(), end = temp_view.end(); it != end; ++it) {
            Node* node = view.search(it.curr());
            if (node == nullptr) {
                if constexpr (left) {
                    copy.template node<Allocator::INSERT_HEAD>(std::move(*(it.curr())));
                } else {
                    copy.template node<Allocator::INSERT_TAIL>(std::move(*(it.curr())));
                }
            }
        }

        // if original view was not dynamic, move into new view of fixed size
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
        return union_impl<View, Container, false>(view, items);
    }


    /* Wrap Python dictionaries to yield key-value pairs during iteration. */
    template <typename View>
    inline auto union_(const View& view, const PyObject* items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, View>
    {
        if (PyDict_Check(items)) {
            PyDict dict(items);
            return union_impl<View, PyDict, false>(view, dict);
        }
        return union_impl<View, PyObject*, false>(view, items);
    }


    /* Get the union between a linked set or dictionary and an arbitrary Python
    iterable.  This method appends elements to the head of the set rather than the
    tail. */
    template <typename View, typename Container>
    inline auto union_left(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, View>
    {
        return union_impl<View, Container, true>(view, items);
    }


    /* Wrap Python dictionaries to yield key-value pairs during iteration. */
    template <typename View>
    inline auto union_left(const View& view, const PyObject* items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, View>
    {
        if (PyDict_Check(items)) {
            PyDict dict(items);
            return union_impl<View, PyDict, true>(view, dict);
        }
        return union_impl<View, PyObject*, true>(view, items);
    }


    /* Get the difference between a linked set or dictionary and an arbitrary Python
    iterable. */
    template <typename View, typename Container>
    auto difference(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, View>
    {
        using Allocator = typename View::Allocator;
        using Node = typename View::Node;

        // iterate over items and mark all found nodes
        std::unordered_set<const Node*> found;
        for (auto item : iter(items)) {
            Node* node = view.search(item);
            if (node != nullptr) found.insert(node);
        }

        // add all elements that were not found
        View copy(view.size() - found.size(), view.specialization());
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            if (found.find(it.curr()) == found.end()) {
                copy.template node<Allocator::INSERT_TAIL>(*(it.curr()));
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
        using Allocator = typename View::Allocator;
        using Node = typename View::Node;

        // iterate over items and mark all found nodes
        std::unordered_set<const Node*> found;
        for (auto item : iter(items)) {
            Node* node = view.search(item);
            if (node != nullptr) found.insert(node);
        }

        // add all elements that were found
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
    template <typename View, typename Container, bool left = false>
    inline auto symmetric_difference(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, View>
    {
        return symmetric_difference_impl<View, Container, left>(view, items);
    }


    /* Wrap Python dictionaries to yield key-value pairs during iteration. */
    template <typename View>
    inline auto symmetric_difference(const View& view, const PyObject* items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, View>
    {
        if (PyDict_Check(items)) {
            PyDict dict(items);
            return symmetric_difference_impl<View, PyDict, false>(view, dict);
        }
        return symmetric_difference_impl<View, PyObject*, false>(view, items);
    }


    /* Get the symmetric difference between a linked set or dictionary and an arbitrary
    Python iterable.  This method appends elements to the head of the set rather than
    the tail. */
    template <typename View, typename Container>
    auto symmetric_difference_left(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, View>
    {
        return symmetric_difference_impl<View, Container, true>(view, items);
    }


    /* Wrap Python dictionaries to yield key-value pairs during iteration. */
    template <typename View>
    inline auto symmetric_difference_left(const View& view, const PyObject* items)
        -> std::enable_if_t<ViewTraits<View>::dictlike, View>
    {
        if (PyDict_Check(items)) {
            PyDict dict(items);
            return symmetric_difference_impl<View, PyDict, true>(view, dict);
        }
        return symmetric_difference_impl<View, PyObject*, true>(view, items);
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_UNION_H
