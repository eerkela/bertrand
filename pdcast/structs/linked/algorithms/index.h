#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_INDEX_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_INDEX_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <type_traits>  // std::enable_if_t<>
#include "../../util/base.h"  // is_pairlike<>
#include "../../util/ops.h"  // eq(), repr()
#include "../core/view.h"  // ViewTraits
#include "slice.h"  // normalize_slice()


namespace bertrand {
namespace linked {


    template <Yield yield = Yield::KEY, typename View, typename Item>
    auto index(
        const View& view,
        const Item& item,
        std::optional<long long> start,
        std::optional<long long> stop
    ) -> std::enable_if_t<ViewTraits<View>::listlike || yield == Yield::VALUE, size_t>
    {
        auto not_found = [](const Item& item) {
            return KeyError(repr(item));
        };

        if (view.size() == 0) {
            throw not_found(item);
        }

        SliceIndices<const View> indices = normalize_slice(view, start, stop);
        size_t norm_start = indices.start;
        size_t norm_stop = indices.stop;
        if (norm_start == norm_stop) {
            throw not_found(item);
        } else if (norm_start > norm_stop) {
            throw IndexError("start index cannot be greater than stop index");
        }

        // NOTE: if list is doubly-linked and stop is closer to tail than start is to
        // head, then we iterate backward from the tail
        if constexpr (NodeTraits<typename View::Node>::has_prev) {
            if (indices.backward) {
                size_t idx = view.size() - 1;
                auto it = view.template rbegin<yield>();
                for (; idx > norm_stop; --idx, ++it);

                bool found = false;
                size_t last_observed;
                for (; idx >= norm_start; --idx, ++it) {
                    if (eq(*it, item)) {
                        found = true;
                        last_observed = idx;
                    }
                }
                if (found) {
                    return last_observed;
                }
                throw not_found(item);
            }
        }

        // otherwise, we have to iterate forward from head
        size_t idx = 0;
        auto it = view.template begin<yield>();
        for (; idx < norm_start; ++idx, ++it);
        for (; idx < norm_stop; ++idx, ++it) {
            if (eq(*it, item)) {
                return idx;
            }
        }
        throw not_found(item);
    }


    template <Yield yield = Yield::KEY, typename View, typename Item>
    auto index(
        const View& view,
        const Item& item,
        std::optional<long long> start,
        std::optional<long long> stop
    ) -> std::enable_if_t<ViewTraits<View>::hashed && yield != Yield::VALUE, size_t>
    {
        using Node = typename View::Node;

        auto not_found = [](const Item& item) {
            if constexpr (yield == Yield::ITEM) {
                return KeyError(repr(std::get<0>(item)));
            } else {
                return KeyError(repr(item));
            }
        };

        if (view.size() == 0) {
            throw not_found(item);
        }

        SliceIndices<const View> indices = normalize_slice(view, start, stop);
        size_t norm_start = indices.start;
        size_t norm_stop = indices.stop;
        if (norm_start == norm_stop) {
            throw not_found(item);
        } else if (norm_start > norm_stop) {
            throw IndexError("start index cannot be greater than stop index");
        }

        const Node* node;
        if constexpr (yield == Yield::ITEM) {
            static_assert(
                is_pairlike<Item>,
                "item must be pair-like (e.g. std::pair or std::tuple of size 2)"
            );

            node = view.search(std::get<0>(item));
            if (node == nullptr) {
                throw not_found(item);
            } else if (!eq(node->mapped(), std::get<1>(item))) {
                std::ostringstream msg;
                msg << "value mismatch for key " << repr(std::get<0>(item)) << " (";
                msg << repr(node->mapped()) << " != " << repr(std::get<1>(item));
                msg << ")";
                throw KeyError(msg.str());
            }
        } else {
            node = view.search(item);
            if (node == nullptr) {
                throw not_found(item);
            }
        }

        // NOTE: if list is doubly-linked and stop is closer to tail than start is to
        // head, then we iterate backward from the tail
        if constexpr (NodeTraits<Node>::has_prev) {
            if (indices.backward) {
                size_t idx = view.size() - 1;
                auto it = view.template rbegin<yield>();
                for (; idx > norm_start; --idx, ++it);
                for (; idx >= norm_stop; --idx, ++it) {
                    if (it.curr() == node) {
                        return idx;
                    }
                }
                throw not_found(item);
            }
        }

        // otherwise, we have to iterate forward from the head
        size_t idx = 0;
        auto it = view.template begin<yield>();
        for (; idx < norm_start; ++idx, ++it) {
            if (it.curr() == node) {
                throw not_found(item);
            }
        }
        for (; idx < norm_stop; ++idx, ++it) {
            if (it.curr() == node) {
                return idx;
            }
        }
        throw not_found(item);
    }


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_INDEX_H
