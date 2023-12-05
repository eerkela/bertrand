#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_MAP_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_MAP_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/base.h"  // is_pyobject<>
#include "../../util/except.h"  // KeyError
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Forward declaration for proxy object. */
    template <typename View>
    class MapProxy;


    /* Get a proxy for a value that may or may not be stored within a linked
    dictionary. */
    template <typename View, typename Key>
    auto map(View& view, const Key& key)
        -> std::enable_if_t<ViewTraits<View>::dictlike, MapProxy<View>>
    {
        return MapProxy<View>(view, key);
    }


    /* Get a proxy for a value that may or may not be stored within a linked
    dictionary. */
    template <typename View, typename Key>
    auto map(const View& view, const Key& key)
        -> std::enable_if_t<ViewTraits<View>::dictlike, const MapProxy<View>>
    {
        return MapProxy<View>(view, key);
    }


    /* A proxy for an entry in the dictionary, as returned by the [] operator. */
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

        MapProxy(View& view, const Key& key) : view(view), key(key) {}

    public:
        /* Disallow ElementProxies from being stored as lvalues. */
        MapProxy(const MapProxy&) = delete;
        MapProxy(MapProxy&&) = delete;
        MapProxy& operator=(const MapProxy&) = delete;
        MapProxy& operator=(MapProxy&&) = delete;

        /* Get the value stored with this key or raise an error if it is not found. */
        inline const Value& get() const {
            Node* node = view.search(key);
            if (node == nullptr) {
                throw KeyError(repr(key));
            }
            return node->mapped();
        }

        /* Set the value stored with this key.  Inserts the key with the specified
        value if it is not already present. */
        inline void set(const Value& value) {
            using Allocator = typename View::Allocator;
            static constexpr unsigned int flags = (
                Allocator::EXIST_OK | Allocator::REPLACE_MAPPED |
                Allocator::INSERT_TAIL
            );
            view.template node<flags>(key, value);
        }

        /* Delete the value stored with this key, or raise an error if it is not
        found. */
        inline void del() {
            using Allocator = typename View::Allocator;
            view.template recycle<Allocator::UNLINK>(key);
        }

        /* Allow simple assignment to the value stored under this key.  This is
        syntactic sugar for map(key).set(value). */
        inline void operator=(const Value& value) {
            set(value);
        }

        /* Implicitly convert this proxy into its current value.  This is syntactic
        sugar for map(key).get(), allowing the proxy to be passed in contexts where the
        value type is expected. */
        inline operator const Value&() const {
            return get();
        }

    };


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_MAP_H
