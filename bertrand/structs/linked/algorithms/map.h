#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_MAP_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_MAP_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/base.h"  // is_pyobject<>
#include "../../util/except.h"  // KeyError
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace linked {


    template <typename View>
    class MapProxy;


    template <typename View, typename Key>
    auto map(View& view, const Key& key)
        -> std::enable_if_t<ViewTraits<View>::dictlike, MapProxy<View>>
    {
        return MapProxy<View>(view, key);
    }


    template <typename View, typename Key>
    auto map(const View& view, const Key& key)
        -> std::enable_if_t<ViewTraits<View>::dictlike, const MapProxy<const View>>
    {
        return MapProxy<const View>(view, key);
    }


    template <typename View>
    class MapProxy {
        using Node = typename View::Node;
        using Key = typename Node::Value;
        using Value = typename Node::MappedValue;

        View& view;
        const Key& key;

        template <typename _View, typename _Key>
        friend auto map(_View& view, const _Key& key)
            -> std::enable_if_t<ViewTraits<_View>::dictlike, MapProxy<_View>>;

        template <typename _View, typename _Key>
        friend auto map(const _View& view, const _Key& key)
            -> std::enable_if_t<
                ViewTraits<_View>::dictlike,
                const MapProxy<const _View>
            >;

        MapProxy(View& view, const Key& key) : view(view), key(key) {}

    public:
        MapProxy(const MapProxy&) = delete;
        MapProxy(MapProxy&&) = delete;
        MapProxy& operator=(const MapProxy&) = delete;
        MapProxy& operator=(MapProxy&&) = delete;

        inline const Value& get() const {
            Node* node = view.search(key);
            if (node == nullptr) {
                throw KeyError(repr(key));
            }
            return node->mapped();
        }

        inline void set(const Value& value) {
            using Allocator = typename View::Allocator;
            static constexpr unsigned int flags = (
                Allocator::EXIST_OK | Allocator::REPLACE_MAPPED |
                Allocator::INSERT_TAIL
            );
            view.template node<flags>(key, value);
        }

        inline void del() {
            view.template recycle<View::Allocator::UNLINK>(key);
        }

        inline void operator=(const Value& value) {
            set(value);
        }

        inline operator const Value&() const {
            return get();
        }

    };


}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_MAP_H
