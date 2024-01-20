#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_SET_COMPARE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_SET_COMPARE_H

#include <type_traits>  // std::enable_if_t<>
#include <unordered_set>  // std::unordered_set
#include "../../util/iter.h"  // iter()
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace linked {


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


    template <bool equal, typename View, typename Container>
    auto set_equal(const View& view, const Container& items)
        -> std::enable_if_t<ViewTraits<View>::hashed, bool>
    {
        using Node = typename View::Node;

        std::unordered_set<const Node*> found;
        for (const auto& item : iter(items)) {
            const Node* node = view.search(item);
            if (node == nullptr) {
                return !equal;
            }
            found.insert(node);
        }

        if constexpr (equal) {
            return found.size() == view.size();
        } else {
            return found.size() != view.size();
        }
    }


    template <bool strict, typename View, typename Container>
    auto issubset(const View& view, const Container& items)
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

        if constexpr (strict) {
            return found.size() == view.size() && larger;
        } else {
            return found.size() == view.size();
        }
    }


    template <bool strict, typename View, typename Container>
    auto issuperset(const View& view, const Container& items)
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

        if constexpr (strict) {
            return found.size() < view.size();
        } else {
            return found.size() <= view.size();
        }
    }


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_SET_COMPARE_H
