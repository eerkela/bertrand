// include guard: BERTRAND_STRUCTS_LINKED_TRAITS_H
#ifndef BERTRAND_STRUCTS_LINKED_TRAITS_H
#define BERTRAND_STRUCTS_LINKED_TRAITS_H


#include <type_traits>  // std::false_type, std::true_type


namespace bertrand {
namespace structs {
namespace linked {


/* A collection of SFINAE traits for inspecting node types at compile time. */
template <typename NodeType>
class NodeTraits {

    /* Detects whether the templated type has a prev() method. */
    struct _has_prev {
        /* Specialization for types that have a prev() method. */
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->prev(), std::true_type());

        /* Fallback for types that don't have a prev() method. */
        template <typename T>
        static constexpr auto test(...) -> std::false_type;

        static constexpr bool value = decltype(test<NodeType>(nullptr))::value;
    };

    /* Detects whether the templated type has a node() method. */
    struct _has_node {
        /* Specialization for types that have a node() method. */
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->node(), std::true_type());

        /* Fallback for types that don't have a node() method. */
        template <typename T>
        static constexpr auto test(...) -> std::false_type;

        static constexpr bool value = decltype(test<NodeType>(nullptr))::value;
    };

    /* Detects whether the templated type has a hash() method. */
    struct _has_hash {
        /* Specialization for types that have a hash() method. */
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->hash(), std::true_type());

        /* Fallback for types that don't have a hash() method. */
        template <typename T>
        static constexpr auto test(...) -> std::false_type;

        static constexpr bool value = decltype(test<NodeType>(nullptr))::value;
    };

    /* Detects whether the templated type has a mapped() accessor. */
    struct _has_mapped {
        /* Specialization for types that have a mapped() method. */
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->mapped(), std::true_type());

        /* Fallback for types that don't have a mapped() method. */
        template <typename T>
        static constexpr auto test(...) -> std::false_type;

        static constexpr bool value = decltype(test<NodeType>(nullptr))::value;
    };

    /* Detects whether the templated type has a count() accessor. */
    struct _has_count {
        /* Specialization for types that have a count() method. */
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->count(), std::true_type());

        /* Fallback for types that don't have a count() method. */
        template <typename T>
        static constexpr auto test(...) -> std::false_type;

        static constexpr bool value = decltype(test<NodeType>(nullptr))::value;
    };

public:
    static constexpr bool has_prev = _has_prev::value;
    static constexpr bool has_node = _has_node::value;
    static constexpr bool has_hash = _has_hash::value;
    static constexpr bool has_mapped = _has_mapped::value;
    static constexpr bool has_count = _has_count::value;
};


/* A collection of SFINAE traits for inspecting view types at compile time. */
template <typename ViewType>
class ViewTraits {

    /* Detects whether the templated type has a search(Value&) method, indicating
    set-like behavior. */
    struct _is_setlike {
        /* Specialization for types that have a search(Value&) method. */
        template <typename T, typename Value = typename T::Value>
        static constexpr auto test(T* t) -> decltype(
            t->search(std::declval<Value>()),
            std::true_type()
        );

        /* Fallback for types that don't have a search() method. */
        template <typename T>
        static constexpr auto test(...) -> std::false_type;

        static constexpr bool value = decltype(test<ViewType>(nullptr))::value;
    };

    /* Detects whether the templated type has a lookup(Value&) method, indicating
    dict-like behavior. */
    struct _is_dictlike {
        /* Specialization for types that have a lookup(Value&) method. */
        template <typename T, typename Value = typename T::Value>
        static constexpr auto test(T* t) -> decltype(
            t->lookup(std::declval<Value>()),
            std::true_type()
        );

        /* Fallback for types that don't have a lookup() method. */
        template <typename T>
        static constexpr auto test(...) -> std::false_type;

        static constexpr bool value = decltype(test<ViewType>(nullptr))::value;
    };

public:
    static constexpr bool is_setlike = _is_setlike::value;
    static constexpr bool is_dictlike = _is_dictlike::value;
};





/* A collection of SFINAE traits for inspecting linked containers at compile time. */
template <typename ContainerType>
class ContainerTraits {
    using View = typename ContainerType::View;
    using Node = typename ContainerType::Node;


public:
    static constexpr bool doubly_linked = NodeTraits<Node>::has_prev;


};






}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_TRAITS_H include guard
