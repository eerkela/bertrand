#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_SET_COMPARE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_SET_COMPARE_H

#include <type_traits>  // std::enable_if_t<>
#include <unordered_set>  // std::unordered_set
#include "../../util/iter.h"  // iter()
#include "../core/view.h"  // ViewTraits


// TODO: write dict_compare.h that takes values into account?
// -> Use Yield::VALUE/ITEM for dictlike views.


namespace bertrand {
namespace linked {


    /* Check whether a linked set or dictionary has any elements in common with
    a given container. */
    template <typename View, typename Container>
    auto isdisjoint(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, bool>
    {
        for (const auto& item : iter(items)) {
            if (view.search(item) != nullptr) {
                return false;
            }
        }
        return true;
    }


    /* Check whether the elements of a linked set or dictionary are equal to those of
    a given container. */
    template <typename View, typename Container>
    auto set_equal(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, bool>
    {
        using Node = typename View::Node;

        std::unordered_set<const Node*> found;
        for (const auto& item : iter(items)) {
            const Node* node = view.search(item);
            if (node == nullptr) {
                return false;
            }
            found.insert(node);
        }

        return found.size() == view.size();
    }


    /* Check whether the elements of a linked set or dictionary are not equal to those
    of a given container. */
    template <typename View, typename Container>
    auto set_not_equal(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, bool>
    {
        using Node = typename View::Node;

        std::unordered_set<const Node*> found;
        for (const auto& item : iter(items)) {
            const Node* node = view.search(item);
            if (node != nullptr) {
                return false;
            }
            found.insert(node);
        }

        return found.size() != view.size();
    }


    /* Check whether the elements of a linked set or dictionary represent a subset of a
    given container. */
    template <typename View, typename Container>
    auto issubset(const View& view, const Container& items, bool strict)
        -> std::enable_if_t<ViewTraits<View>::hashed, bool>
    {
        using Node = typename View::Node;

        std::unordered_set<const Node*> found;
        bool larger = false;
        for (const auto& item : iter(items)) {
            const Node* node = view.search(item);
            if (node == nullptr) {
                larger = true;
            } else {
                found.insert(node);
            }
        }

        return found.size() == view.size() && (larger || !strict);
    }


    /* Check whether the elements of a linked set or dictionary represent a superset of
    a given container. */
    template <typename View, typename Container>
    auto issuperset(const View& view, const Container& items, bool strict)
        -> std::enable_if_t<ViewTraits<View>::hashed, bool>
    {
        using Node = typename View::Node;

        std::unordered_set<const Node*> found;
        for (const auto& item : iter(items)) {
            const Node* node = view.search(item);
            if (node == nullptr) {
                return false;
            }
            found.insert(node);
        }

        return found.size() < view.size() || !strict;
    }


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_SET_COMPARE_H
