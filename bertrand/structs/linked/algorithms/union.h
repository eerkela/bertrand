#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_UNION_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_UNION_H

#include <type_traits>  // std::enable_if_t<>
#include <unordered_map>  // std::unordered_map
#include <unordered_set>  // std::unordered_set
#include "../../util/iter.h"  // iter()
#include "../../util/ops.h"  // len()
#include "../../util/python.h"  // python::Dict
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits
#include "update.h"  // update()


namespace bertrand {
namespace linked {


    namespace union_config {

        template <typename View>
        using EnableIfSetlike = std::enable_if_t<
            ViewTraits<View>::setlike,
            typename ViewTraits<View>::As::DYNAMIC
        >;

        template <typename View, Yield yield>
        using EnableIfDictlike = std::enable_if_t<
            ViewTraits<View>::dictlike,
            std::conditional_t<
                yield == Yield::KEY,
                typename ViewTraits<
                    typename ViewTraits<View>::As::DYNAMIC
                >::As::template Set<Yield::KEY>,
                typename ViewTraits<View>::As::DYNAMIC
            >
        >;

    }


    template <bool left = false, typename View, typename Container>
    auto union_(const View& view, const Container& items)
        -> union_config::EnableIfSetlike<View>
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


    template <Yield yield = Yield::KEY, bool left = false, typename View, typename Container>
    auto union_(const View& view, const Container& items)
        -> union_config::EnableIfDictlike<View, yield>
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
            using Set = typename ViewTraits<DynamicView>::As::template Set<Yield::KEY>;

            Set result(view.size(), view.specialization());
            auto it = view.template begin<Yield::KEY>();
            auto end = view.template end<Yield::KEY>();
            while (it != end) {
                result.template node<Allocator::INSERT_TAIL>(*it);
                ++it;
            }

            for (const auto& item : iter(items)) {
                result.template node<flags>(item);
            }
            return result;

        } else {
            DynamicView result(view.size(), view.specialization());
            auto it = view.template begin();
            auto end = view.template end();
            while (it != end) {
                result.template node<Allocator::INSERT_TAIL>(*(it.curr()));
                ++it;
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


    template <typename View, typename Container>
    auto difference(const View& view, const Container& items)
        -> union_config::EnableIfSetlike<View>
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


    template <Yield yield = Yield::KEY, typename View, typename Container>
    auto difference(const View& view, const Container& items)
        -> union_config::EnableIfDictlike<View, yield>
    {
        static_assert(
            yield != Yield::VALUE,
            "cannot perform set comparisons on dictionary values: use listlike "
            "operators instead"
        );
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using Node = typename View::Node;

        if constexpr (yield == Yield::KEY) {
            using Set = typename ViewTraits<DynamicView>::As::template Set<Yield::KEY>;

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
            while (it != end) {
                if (found.find(it.curr()) == found.end()) {
                    result.template node<View::Allocator::INSERT_TAIL>(*it);
                }
                ++it;
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


    template <typename View, typename Container>
    auto intersection(const View& view, const Container& items)
        -> union_config::EnableIfSetlike<View>
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


    template <Yield yield = Yield::KEY, typename View, typename Container>
    auto intersection(const View& view, const Container& items)
        -> union_config::EnableIfDictlike<View, yield>
    {
        static_assert(
            yield != Yield::VALUE,
            "cannot perform set comparisons on dictionary values: use listlike "
            "operators instead"
        );
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using Node = typename View::Node;

        if constexpr (yield == Yield::KEY) {
            using Set = typename ViewTraits<DynamicView>::As::template Set<Yield::KEY>;

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
            while (it != end) {
                if (found.find(it.curr()) != found.end()) {
                    result.template node<View::Allocator::INSERT_TAIL>(*it);
                }
                ++it;
            }
            return result;

        } else {
            std::unordered_map<const Node*, typename View::MappedValue> found;
            if constexpr (is_pyobject<Container>) {
                if (PyDict_Check(items)) {
                    python::Dict<python::Ref::BORROW> dict(items);
                    for (const auto& item : iter(dict)) {
                        const Node* node = view.search(std::get<0>(item));
                        if (node != nullptr) {
                            found.insert({node, std::get<1>(item)});
                        }
                    }
                } else {
                    for (const auto& item : iter(items)) {
                        const Node* node = view.search(item);
                        if (node != nullptr) {
                            found.insert({node, node->mapped()});
                        }
                    }
                }
            } else {
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
            }

            DynamicView result(found.size(), view.specialization());
            auto it = view.template begin<Yield::KEY>();
            auto end = view.template end<Yield::KEY>();
            while (it != end) {
                auto found_it = found.find(it.curr());
                if (found_it != found.end()) {
                    result.template node<View::Allocator::INSERT_TAIL>(
                        *it, found_it->second
                    );
                }
                ++it;
            }
            return result;
        }
    }


    template <bool left = false, typename View, typename Container>
    auto symmetric_difference(const View& view, const Container& items)
        -> union_config::EnableIfSetlike<View>
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


    template <Yield yield = Yield::KEY, bool left = false, typename View, typename Container>
    auto symmetric_difference(const View& view, const Container& items)
        -> union_config::EnableIfDictlike<View, yield>
    {
        static_assert(
            yield != Yield::VALUE,
            "cannot perform set comparisons on dictionary values: use listlike "
            "operators instead"
        );
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using Allocator = typename View::Allocator;

        if constexpr (yield == Yield::KEY) {
            using Set = typename ViewTraits<DynamicView>::As::template Set<Yield::KEY>;
            using Temp = typename ViewTraits<Set>::template Reconfigure<
                Config::SINGLY_LINKED
            >;

            Temp other(items, std::nullopt, nullptr, false);
            Set result(std::nullopt, view.specialization());

            for (auto it = view.begin(), end = view.end(); it != end; ++it) {
                if (other.search(it.curr()) == nullptr) {
                    result.template node<Allocator::INSERT_TAIL>(*(it.curr()));
                }
            }

            for (auto it = other.begin(), end = other.end(); it != end; ++it) {
                if (view.search(it.curr()) == nullptr) {
                    if constexpr (left) {
                        result.template node<Allocator::INSERT_HEAD>(
                            std::move(*(it.curr()))
                        );
                    } else {
                        result.template node<Allocator::INSERT_TAIL>(
                            std::move(*(it.curr()))
                        );
                    }
                }
            }

            return result;

        } else {
            using Temp = typename ViewTraits<DynamicView>::template Reconfigure<
                Config::SINGLY_LINKED
            >;

            Temp other(items, std::nullopt, nullptr, false);
            DynamicView result(std::nullopt, view.specialization());

            for (auto it = view.begin(), end = view.end(); it != end; ++it) {
                if (other.search(it.curr()) == nullptr) {
                    result.template node<Allocator::INSERT_TAIL>(*(it.curr()));
                }
            }

            for (auto it = other.begin(), end = other.end(); it != end; ++it) {
                if (view.search(it.curr()) == nullptr) {
                    if constexpr (left) {
                        result.template node<Allocator::INSERT_HEAD>(
                            std::move(*(it.curr()))
                        );
                    } else {
                        result.template node<Allocator::INSERT_TAIL>(
                            std::move(*(it.curr()))
                        );
                    }
                }
            }

            return result;
        }
    }


}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_UNION_H
