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


    /* Get the union of keys held a linked set compared to those of an arbitrary
    container. */
    template <bool left = false, typename View, typename Container>
    auto union_(const View& view, const Container& items)
        -> std::enable_if_t<
            ViewTraits<View>::setlike,
            typename ViewTraits<View>::As::DYNAMIC
        >
    {
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | 
            (left ? Allocator::INSERT_HEAD : Allocator::INSERT_TAIL)
        );

        DynamicView result(view.size(), view.specialization());
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            result.template node<Allocator::INSERT_TAIL>(*(it.curr()));
        }

        for (const auto& item : iter(items)) {
            result.template node<flags>(item);
        }
        return result;
    }


    /* Get the union of keys or key-value pairs held in a linked dictionary compared to
    those of an arbitrary container. */
    template <Yield yield = Yield::KEY, bool left = false, typename View, typename Container>
    auto union_(const View& view, const Container& items)
        -> std::enable_if_t<
            ViewTraits<View>::dictlike,
            std::conditional_t<
                yield == Yield::KEY,
                typename ViewTraits<
                    typename ViewTraits<View>::As::DYNAMIC
                >::As::template Set<yield>,
                typename ViewTraits<View>::As::DYNAMIC
            >
        >
    {
        static_assert(
            yield != Yield::VALUE,
            "cannot perform set comparisons on dictionary values: use listlike "
            "operators instead"
        );
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | 
            (left ? Allocator::INSERT_HEAD : Allocator::INSERT_TAIL)
        );

        if constexpr (yield == Yield::KEY) {
            using Set = typename ViewTraits<DynamicView>::As::template Set<yield>;

            Set result(view.size(), view.specialization());
            auto it = view.template begin<Yield::KEY>();
            auto end = view.template end<Yield::KEY>();
            for (; it != end; ++it) {
                result.template node<Allocator::INSERT_TAIL>(*it);
            }

            for (const auto& item : iter(items)) {
                result.template node<flags>(item);
            }
            return result;

        } else {
            DynamicView result(view.size(), view.specialization());
            auto it = view.template begin();
            auto end = view.template end();
            for (; it != end; ++it) {
                result.template node<Allocator::INSERT_TAIL>(*(it.curr()));
            }

            if constexpr (is_pyobject<Container>) {
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
    }


    /* Get the difference of keys held in a linked set compared to those of an arbitrary
    container. */
    template <typename View, typename Container>
    auto difference(const View& view, const Container& items)
        -> std::enable_if_t<
            ViewTraits<View>::setlike,
            typename ViewTraits<View>::As::DYNAMIC
        >
    {
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using Node = typename View::Node;

        std::unordered_set<const Node*> found;
        for (const auto& item : iter(items)) {
            const Node* node = view.search(item);
            if (node != nullptr) {
                found.insert(node);
            }
        }

        DynamicView result(view.size() - found.size(), view.specialization());
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            if (found.find(it.curr()) == found.end()) {
                result.template node<View::Allocator::INSERT_TAIL>(*(it.curr()));
            }
        }
        return result;
    }


    /* Get the difference of keys held in a linked set compared to those of an arbitrary
    container. */
    template <Yield yield = Yield::KEY, typename View, typename Container>
    auto difference(const View& view, const Container& items)
        -> std::enable_if_t<
            ViewTraits<View>::dictlike,
            std::conditional_t<
                yield == Yield::KEY,
                typename ViewTraits<
                    typename ViewTraits<View>::As::DYNAMIC
                >::As::template Set<yield>,
                typename ViewTraits<View>::As::DYNAMIC
            >
        >
    {
        static_assert(
            yield != Yield::VALUE,
            "cannot perform set comparisons on dictionary values: use listlike "
            "operators instead"
        );
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using Node = typename View::Node;

        if constexpr (yield == Yield::KEY) {
            using Set = typename ViewTraits<DynamicView>::As::template Set<yield>;

            std::unordered_set<const Node*> found;
            for (const auto& item : iter(items)) {
                const Node* node = view.search(item);
                if (node != nullptr) {
                    found.insert(node);
                }
            }

            Set result(view.size() - found.size(), view.specialization());
            auto it = view.template begin<Yield::KEY>();
            auto end = view.template end<Yield::KEY>();
            for (; it != end; ++it) {
                if (found.find(it.curr()) == found.end()) {
                    result.template node<View::Allocator::INSERT_TAIL>(*it);
                }
            }
            return result;

        } else {
            std::unordered_set<const Node*> found;
            for (const auto& item : iter(items)) {
                if constexpr (is_pairlike<std::decay_t<decltype(item)>>) {
                    const Node* node = view.search(std::get<0>(item));
                    if (node != nullptr) {
                        found.insert(node);
                    }
                } else {
                    const Node* node = view.search(item);
                    if (node != nullptr) {
                        found.insert(node);
                    }
                }
            }

            DynamicView result(view.size() - found.size(), view.specialization());
            for (auto it = view.begin(), end = view.end(); it != end; ++it) {
                if (found.find(it.curr()) == found.end()) {
                    result.template node<View::Allocator::INSERT_TAIL>(*(it.curr()));
                }
            }
            return result;
        }
    }


    /* Get the intersection of keys held in a linked set compared to those of an
    arbitrary container. */
    template <typename View, typename Container>
    auto intersection(const View& view, const Container& items)
        -> std::enable_if_t<
            ViewTraits<View>::setlike,
            typename ViewTraits<View>::As::DYNAMIC
        >
    {
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
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
                result.template node<View::Allocator::INSERT_TAIL>(*(it.curr()));
            }
        }
        return result;
    }


    /* Get the intersection keys or key-value pairs held in a linked dictionary
    compared to those of an arbitrary container. */
    template <Yield yield = Yield::KEY, typename View, typename Container>
    auto intersection(const View& view, const Container& items)
        -> std::enable_if_t<
            ViewTraits<View>::dictlike,
            std::conditional_t<
                yield == Yield::KEY,
                typename ViewTraits<
                    typename ViewTraits<View>::As::DYNAMIC
                >::As::template Set<yield>,
                typename ViewTraits<View>::As::DYNAMIC
            >
        >
    {
        static_assert(
            yield != Yield::VALUE,
            "cannot perform set comparisons on dictionary values: use listlike "
            "operators instead"
        );
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using Node = typename View::Node;

        if constexpr (yield == Yield::KEY) {
            using Set = typename ViewTraits<DynamicView>::As::template Set<yield>;

            std::unordered_set<const Node*> found;
            for (const auto& item : iter(items)) {
                const Node* node = view.search(item);
                if (node != nullptr) {
                    found.insert(node);
                }
            }

            Set result(found.size(), view.specialization());
            auto it = view.template begin<Yield::KEY>();
            auto end = view.template end<Yield::KEY>();
            for (; it != end; ++it) {
                if (found.find(it.curr()) != found.end()) {
                    result.template node<View::Allocator::INSERT_TAIL>(*it);
                }
            }
            return result;

        } else {
            std::unordered_map<const Node*, typename View::MappedValue> found;
            for (const auto& item : iter(items)) {
                if constexpr (is_pairlike<std::decay_t<decltype(item)>>) {
                    const Node* node = view.search(std::get<0>(item));
                    if (node != nullptr) {
                        found.insert({node, std::get<1>(item)});
                    }
                } else {
                    const Node* node = view.search(item);
                    if (node != nullptr) {
                        found.insert({node, node->mapped()});
                    }
                }
            }

            DynamicView result(found.size(), view.specialization());
            auto it = view.template begin<Yield::KEY>();
            auto end = view.template end<Yield::KEY>();
            for (; it != end; ++it) {
                auto found_it = found.find(it.curr());
                if (found_it != found.end()) {
                    result.template node<View::Allocator::INSERT_TAIL>(
                        *it, found_it->second
                    );
                }
            }
            return result;
        }
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
