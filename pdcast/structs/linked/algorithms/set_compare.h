// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H
#define BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H

#include <type_traits>  // std::enable_if_t<>
#include <unordered_set>  // std::unordered_set
#include "../../util/iter.h"  // iter()
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Check whether a linked set or dictionary has any elements in common with
    a given container. */
    template <typename View, typename Container>
    auto isdisjoint(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::setlike, bool>
    {
        for (auto item : util::iter(items)) {
            if (view.search(item) != nullptr) return false;
        }
        return true;
    }


    /* Check whether the elements of a linked set or dictionary represent a subset of a
    given container. */
    template <typename View, typename Container>
    auto issubset(const View& view, const Container& items, bool strict)
        -> std::enable_if_t<ViewTraits<View>::setlike, bool>
    {
        using Node = typename View::Node;

        // use auxiliary set to keep track of visited nodes as we iterate over items
        std::unordered_set<Node*> found;
        bool larger = false;
        for (auto item : util::iter(items)) {
            Node* node = view.search(item);
            if (node == nullptr) {
                larger = true;
            } else {
                found.insert(node);
            }
        }

        // check that we found all nodes in view
        if (found.size() != view.size()) return false;

        // if strict, assert that container has at least one extra element
        return !strict || larger;
    }


    /* Check whether the elements of a linked set or dictionary represent a superset of
    a given container. */
    template <typename View, typename Container>
    auto issuperset(const View& view, const Container& items, bool strict)
        -> std::enable_if_t<ViewTraits<View>::setlike, bool>
    {
        using Node = typename View::Node;

        // use auxiliary set to keep track of visited nodes as we iterate over items
        std::unordered_set<Node*> found;
        for (auto item : util::iter(items)) {
            Node* node = view.search(item);
            if (node == nullptr) return false;
            found.insert(node);
        }

        // if strict, assert that view has at least one extra element
        return !strict || found.size() < view.size();
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H include guard
