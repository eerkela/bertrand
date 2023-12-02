// include guard: BERTRAND_STRUCTS_LINKED_DICT_H
#ifndef BERTRAND_STRUCTS_LINKED_DICT_H
#define BERTRAND_STRUCTS_LINKED_DICT_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <ostream>  // std::ostream
#include <sstream>  // std::ostringstream
#include <type_traits>  // std::conditional_t
#include <utility>  // std::pair
#include <variant>  // std::variant
#include <Python.h>  // CPython API
#include "../util/args.h"  // PyArgs
#include "../util/except.h"  // throw_python()
#include "../util/ops.h"  // eq(), lexical_lt(), etc.
#include "core/view.h"  // DictView
#include "base.h"  // LinkedBase
#include "list.h"  // PyListInterface
#include "set.h"  // PySetInterface

#include "algorithms/add.h"
#include "algorithms/contains.h"
#include "algorithms/count.h"
#include "algorithms/discard.h"
#include "algorithms/distance.h"
#include "algorithms/get.h"
#include "algorithms/index.h"
#include "algorithms/insert.h"
#include "algorithms/map.h"
#include "algorithms/move.h"
#include "algorithms/pop.h"
#include "algorithms/position.h"
// #include "algorithms/relative.h"
#include "algorithms/remove.h"
#include "algorithms/repr.h"
#include "algorithms/reverse.h"
#include "algorithms/rotate.h"
#include "algorithms/setdefault.h"
#include "algorithms/set_compare.h"
#include "algorithms/slice.h"
#include "algorithms/sort.h"
#include "algorithms/swap.h"
#include "algorithms/union.h"
#include "algorithms/update.h"


namespace bertrand {
namespace structs {
namespace linked {


namespace dict_config {

    /* Apply default config flags for C++ LinkedLists. */
    static constexpr unsigned int defaults(unsigned int flags) {
        unsigned int result = flags;
        if (!(result & (Config::DOUBLY_LINKED | Config::SINGLY_LINKED | Config::XOR))) {
            result |= Config::DOUBLY_LINKED;  // default to doubly-linked
        }
        if (!(result & (Config::DYNAMIC | Config::FIXED_SIZE))) {
            result |= Config::DYNAMIC;  // default to dynamic allocator
        }
        return result;
    }

    /* Determine the corresponding node type for the given config flags. */
    template <typename Key, typename Value, unsigned int Flags>
    using NodeSelect = std::conditional_t<
        !!(Flags & Config::DOUBLY_LINKED),
        Mapped<DoubleNode<Key>, Value>,
        Mapped<SingleNode<Key>, Value>
    >;

}


/* Forward declarations for dict proxies. */
template <typename Dict>
class KeysProxy;
template <typename Dict>
class ValuesProxy;
template <typename Dict>
class ItemsProxy;


/* A n ordered dictionary based on a combined linked list and hash table. */
template <
    typename K,
    typename V,
    unsigned int Flags = Config::DEFAULT,
    typename Lock = BasicLock
>
class LinkedDict : public LinkedBase<
    linked::DictView<
        dict_config::NodeSelect<K, V, dict_config::defaults(Flags)>,
        dict_config::defaults(Flags)
    >,
    Lock
> {
    using Base = LinkedBase<
        linked::DictView<
            dict_config::NodeSelect<K, V, dict_config::defaults(Flags)>,
            dict_config::defaults(Flags)
        >,
        Lock
    >;

public:
    using View = typename Base::View;
    using Node = typename Base::Node;
    using Key = K;
    using Value = V;

    template <linked::Direction dir>
    using Iterator = typename Base::template Iterator<dir>;
    template <linked::Direction dir>
    using ConstIterator = typename Base::template ConstIterator<dir>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    // inherit constructors from LinkedBase
    using Base::Base;
    using Base::operator=;

    //////////////////////////////
    ////    DICT INTERFACE    ////
    //////////////////////////////

    template <typename Container>
    inline static LinkedDict fromkeys(Container&& keys, const Value& value) {
        // TODO: account for different flag configurations.  If LinkedDict is
        // fixed-size, then we can't just default-construct it here, since we're
        // likely to hit the size limit.  We also can't preallocate ahead of
        // time, since we don't know how many duplicates there will be.
        LinkedDict result;

        // TODO: allocate a new view, then pass it to linked::fromkeys(), then
        // move it into a new LinkedDict

        return linked::fromkeys<View, LinkedDict>(
            std::forward<Container>(keys),
            value
        );
    }

    // TODO: add() may not be necessary.  Just use the normal index operator [] or
    // map() proxy instead.


    // /* Add an item to the end of the dictionary if it is not already present. */
    // template <typename Pair>
    // inline void add(const Pair& item) {
    //     linked::add(this->view, item);
    // }

    // /* Add a key-value pair to the end of the dictionary if it is not already
    // present. */
    // inline void add(const Key& key, const Value& value) {
    //     linked::add(this->view, key, value);
    // }

    // /* Add an item to the beginning of the dictionary if it is not already present. */
    // template <typename Pair>
    // inline void add_left(const Pair& item) {
    //     linked::add_left(this->view, item);
    // }

    // /* Add a key-value pair to the beginning of the dictionary if it is not already
    // present. */
    // inline void add_left(const Key& key, const Value& value) {
    //     linked::add_left(this->view, key, value);
    // }

    // /* Add an item to the front of the dictionary, evicting the last item if
    // necessary and moving items that are already present. */
    // template <typename Pair>
    // inline void lru_add(const Pair& item) {
    //     linked::lru_add(this->view, item);
    // }

    // /* Add a key-value pair to the front of the dictionary, evicting the last item if
    // necessary and moving items that are already present. */
    // inline void lru_add(const Key& key, const Value& value) {
    //     linked::lru_add(this->view, key, value);
    // }


    // TODO: insert() could only accept a key-value pair, not a general Pair type.

    // /* Insert an item at a specific index of the dictionary. */
    // template <typename Pair>
    // inline void insert(long long index, const Pair& item) {
    //     linked::insert(this->view, index, item);
    // }

    /* Insert a key-value pair at a specific index of the dictionary. */
    inline void insert(long long index, const Key& key, const Value& value) {
        linked::insert(this->view, index, key, value);
    }

    /* Get the index of a key within the dictionary. */
    inline size_t index(
        const Key& key,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return linked::index(this->view, key, start, stop);
    }

    /* Count the number of occurrences of a key within the dictionary. */
    inline size_t count(
        const Key& key,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return linked::count(this->view, key, start, stop);
    }

    /* Check if the dictionary contains a certain key. */
    inline bool contains(const Key& key) const {
        return linked::contains(this->view, key);
    }

    /* Check if the dictionary contains a certain key and move it to the front of the
    dictionary if so. */
    inline bool lru_contains(const Key& key) {
        return linked::lru_contains(this->view, key);
    }

    /* Remove a key from the dictionary. */
    inline void remove(const Key& key) {
        linked::remove(this->view, key);
    }

    /* Remove a key from the dictionary if it is present. */
    inline void discard(const Key& key) {
        linked::discard(this->view, key);
    }

    /* Remove a key from the dictionary and return its value. */
    inline Value pop(
        const Key& key,
        std::optional<Value> default_value = std::nullopt
    ) {
        return linked::pop(this->view, key, default_value);
    }

    /* Remove and return a key, value pair from the dictionary. */
    inline std::pair<Key, Value> popitem(long long index = -1) {
        return linked::pop(this->view, index);
    }

    /* Remove all elements from the dictionary. */
    inline void clear() {
        this->view.clear();
    }

    /* Get a value from the dictionary using an optional default. */
    inline std::optional<Value> get(
        const Key& key,
        std::optional<Value> default_value = std::nullopt
    ) const {
        return linked::get(this->view, key, default_value);
    }

    /* Get a value from the dictionary using an optional default, moving the key to
    the front of the dictionary if it is found. */
    inline std::optional<Value> lru_get(
        const Key& key,
        std::optional<Value> default_value = std::nullopt
    ) {
        return linked::lru_get(this->view, key, default_value);
    }

    /* Set a value within the dictionary or insert it if it is not already present. */
    inline Value& setdefault(const Key& key, const Value& default_value) {
        return linked::setdefault(this->view, key, default_value);
    }

    /* Set a value within the dictionary or insert it at the front of the dictionary
    if it is not already present. */
    inline Value& setdefault_left(const Key& key, const Value& default_value) {
        return linked::setdefault_left(this->view, key, default_value);
    }

    /* Set a value within the dictionary and move it to the front of the dictionary,
    or insert it there if it is not already present.  Evicts the last element to make
    room if necessary. */
    inline Value& lru_setdefault(const Key& key, const Value& default_value) {
        return linked::lru_setdefault(this->view, key, default_value);
    }

    /* Return a shallow copy of the dictionary. */
    inline LinkedDict copy() const {
        return LinkedDict(this->view.copy());
    }

    /* Sort the keys within the dictionary in-place according to an optional key
    func. */
    template <typename Func>
    inline void sort(Func key = nullptr, bool reverse=false) {
        linked::sort<linked::MergeSort>(this->view, key, reverse);
    }

    /* Reverse the order of keys within the dictionary in-place. */
    inline void reverse() {
        linked::reverse(this->view);
    }

    /* Shift all keys in the dictionary to the right by the specified number of
    steps. */
    inline void rotate(long long steps = 1) {
        linked::rotate(this->view, steps);
    }

    /* Return a new dictionary with elements from this dictionary and all other
    containers. */
    template <typename... Containers>
    inline LinkedDict union_(Containers&&... items) const {
        return LinkedDict(
            linked::union_(this->view, std::forward<Containers>(items)...)
        );
    }

    /* Return a new dictionary with elements from this dictionary and all other
    containers.  Appends to the head of the dictionary rather than the tail. */
    template <typename... Containers>
    inline LinkedDict union_left(Containers&&... items) const {
        return LinkedDict(
            linked::union_left(this->view, std::forward<Containers>(items)...)
        );
    }

    /* Extend a dictionary by adding elements from one or more iterables that are not
    already present. */
    template <typename... Containers>
    inline void update(Containers&&... items) {
        (linked::update(this->view, std::forward<Containers>(items)), ...);
    }

    /* Extend a dictionary by left-adding elements from one or more iterables that are
    not already present. */
    template <typename... Containers>
    inline void update_left(Containers&&... items) {
        (linked::update_left(this->view, std::forward<Containers>(items)), ...);
    }

    /* Extend a dictionary by adding or moving items to the head of the dictionary and
    possibly evicting the tail to make room. */
    template <typename... Containers>
    inline void lru_update(Containers&&... items) {
        (linked::lru_update(this->view, std::forward<Containers>(items)), ...);
    }

    /* Return a new dictionary with elements common to this dictionary and all other
    containers. */
    template <typename... Containers>
    inline LinkedDict intersection(Containers&&... items) const {
        return LinkedDict(
            linked::intersection(this->view, std::forward<Containers>(items)...)
        );
    }

    /* Remove elements from a dictionary that are contained in one or more iterables. */
    template <typename... Containers>
    inline void intersection_update(Containers&&... items) {
        (linked::intersection_update(this->view, std::forward<Containers>(items)), ...);
    }

    /* Return a new dictionary with elements from this dictionary that are common to
    any other containers. */
    template <typename... Containers>
    inline LinkedDict difference(Containers&&... items) const {
        return LinkedDict(
            linked::difference(this->view, std::forward<Containers>(items)...)
        );
    }

    /* Remove elements from a dictionary that are contained in one or more iterables. */
    template <typename... Containers>
    inline void difference_update(Containers&&... items) {
        (linked::difference_update(this->view, std::forward<Containers>(items)), ...);
    }

    /* Return a new dictionary with elements in either this dictionary or another
    container, but not both. */
    template <typename Container>
    inline LinkedDict symmetric_difference(Container&& items) const {
        return LinkedDict(
            linked::symmetric_difference(
                this->view, std::forward<Container>(items)
            )
        );
    }

    /* Return a new dictionary with elements in either this dictionary or another
    container, but not both.  Appends to the head of the dictionary rather than the
    tail. */
    template <typename Container>
    inline LinkedDict symmetric_difference_left(Container&& items) const {
        return LinkedDict(
            linked::symmetric_difference_left(
                this->view, std::forward<Container>(items)
            )
        );
    }

    /* Update a dictionary, keeping only elements found in either the dictionary or the
    given container, but not both. */
    template <typename Container>
    inline void symmetric_difference_update(Container&& items) {
        linked::symmetric_difference_update(
            this->view, std::forward<Container>(items)
        );
    }

    /* Update a dictionary, keeping only elements found in either the dictionary or the
    given container, but not both.  Appends to the head of the dictionary rather than
    the tail. */
    template <typename Container>
    inline void symmetric_difference_update_left(Container&& items) {
        linked::symmetric_difference_update_left(
            this->view, std::forward<Container>(items)
        );
    }

    /* Get the linear distance between two keys within the dictionary. */
    inline long long distance(const Key& from, const Key& to) const {
        return linked::distance(this->view, from, to);
    }

    /* Swap the positions of two keys within the dictionary. */
    inline void swap(const Key& key1, const Key& key2) {
        linked::swap(this->view, key1, key2);
    }

    /* Move a key within the dictionary by the specified number of steps. */
    inline void move(const Key& key, long long steps) {
        linked::move(this->view, key, steps);
    }

    /* Move a key within the dictionary to a specific index. */
    inline void move_to_index(const Key& key, long long index) {
        linked::move_to_index(this->view, key, index);
    }

    ///////////////////////
    ////    PROXIES    ////
    ///////////////////////

    /* Proxies allow access to particular elements or slices of a dictionary, allowing
     * for convenient, Python-like syntax for dictionary operations.
     *
     * MapProxies are returned by the normal index operator [] when given a possible
     * key within the dictionary.  The key does not have to be present, and merely
     * obtaining a proxy will not modify the dictionary in any way (unlike
     * std::unordered_map).  The proxy itself delays searches until one of its methods
     * is called, as follows:
     *
     *      Value get(): search the dictionary for this key and return its value.
     *      Value lru_get(): search for this key and move it to the front of the
     *          dictionary.
     *      void set(Value& value): set the value for this key or insert at the end of
     *          the dictionary if it is not already present.
     *      void set_left(Value& value): set the value for this key or insert at the
     *          front of the dictionary if it is not already present.
     *      void lru_set(Value& value): set the value for this key and move it to the
     *          front of the dictionary, evicting the last item to make room if
     *          necessary.
     *      void del(): remove this key from the dictionary.
     *      void operator=(Value& value): syntactic sugar for set(value).
     *      operator Value(): implicit conversion operator.  Syntactic sugar for get().
     *
     * KeysProxies, ValuesProxies, and ItemsProxies are returned by the keys(),
     * values(), and items() methods, respectively.  They follow the same semantics as
     * Python's built-in dict views, and can be used to iterate over and compare the
     * keys and values within the dictionary.  See the comments below for details on
     * how they work.
     *
     * Finally, ElementProxies and SliceProxies work the same as for lists, allowing
     * positional access to elements within the dictionary.  See the corresponding
     * documentation in linked/list.h for more information.
     */

    /* Get a read-only, setlike proxy for the keys within this dictionary. */
    inline const KeysProxy<LinkedDict> keys() const {
        return KeysProxy<LinkedDict>(*this);
    }

    /* Get a read-only, setlike proxy for the values within this dictionary. */
    inline const ValuesProxy<LinkedDict> values() const {
        return ValuesProxy<LinkedDict>(*this);
    }

    /* Get a read-only, setlike proxy for the key-value pairs within this dictionary. */
    inline const ItemsProxy<LinkedDict> items() const {
        return ItemsProxy<LinkedDict>(*this);
    }

    /* Get a proxy for a particular key within the dictionary. */
    inline linked::MapProxy<View> map(const Key& key) {
        return linked::map(this->view, key);
    }

    /* Get a const proxy for a particular key within a const dictionary. */
    inline const linked::MapProxy<const View> map(const Key& key) const {
        return linked::map(this->view, key);
    }

    /* Get a proxy for a key at a particular index of the dictionary. */
    inline linked::ElementProxy<View> position(long long index) {
        return linked::position(this->view, index);
    }

    /* Get a const proxy for a key at a particular index of a const dictionary. */
    inline const linked::ElementProxy<const View> position(long long index) const {
        return linked::position(this->view, index);
    }

    /* Get a proxy for a slice within the dictionary. */
    template <typename... Args>
    inline linked::SliceProxy<View, LinkedDict> slice(Args&&... args) {
        return linked::slice<View, LinkedDict>(
            this->view,
            std::forward<Args>(args)...
        );
    }

    /* Get a const proxy for a slice within a const dictionary. */
    template <typename... Args>
    inline auto slice(Args&&... args) const
        -> const linked::SliceProxy<const View, const LinkedDict>
    {
        return linked::slice<const View, const LinkedDict>(
            this->view,
            std::forward<Args>(args)...
        );
    }

    //////////////////////////////////
    ////    OPERATOR OVERLOADS    ////
    //////////////////////////////////

    /* NOTE: operators are implemented as non-member functions for commutativity.
     * The supported operators are as follows:
     *      (|, |=)     union, union update
     *      (&, &=)     intersection, intersection update
     *      (-, -=)     difference, difference update
     *      (^, ^=)     symmetric difference, symmetric difference update
     *      (==)        equality
     *      (!=)        inequality
     *      (<<)        string stream representation (equivalent to Python repr())
     *
     * These all work similarly to their Python counterparts except that they can
     * accept any iterable container in either C++ or Python as the other operand, and
     * cover a wider range of operations than the Python dict operators.  Intersections
     * and differences, in particular, are not normally supported by Python dicts, but
     * are implemented here for consistency with the set interface.  They can be used
     * to easily filter dictionaries based on a set of keys:
     *
     *      >>> d = LinkedDict({"a": 1, "b": 2, "c": 3, "d": 4})
     *      >>> d - {"a", "b"}
     *      LinkedDict({"c": 3, "d": 4})
     *      >>> d & {"a", "b"}
     *      LinkedDict({"a": 1, "b": 2})
     *
     * Note that if a mapping is provided to either of these operators, only the keys
     * will be used for comparison.
     *
     * The symmetric difference operator (^) is also supported, but only if it is
     * given another mapping type as the other operand.  In this case, it will remove
     * those keys that are common to both mappings and insert those that are unique
     * with their corresponding values.  For example:
     *
     *      >>> d = LinkedDict({"a": 1, "b": 2, "c": 3, "d": 4})
     *      >>> d ^ {"a": 1, "b": 2, "e": 5}
     *      LinkedDict({"c": 3, "d": 4, "e": 5})
     *
     * Finally, set comparison operators (<=, <, >=, >) are not supported for the
     * dictionary itself, but are supported for the proxies returned by keys() and
     * potentially items() if the values are also hashable (in which case it acts as a
     * set of key-value pairs).  For example:
     *
     *      >>> d = LinkedDict({"a": 1, "b": 2, "c": 3, "d": 4})
     *      >>> d.keys() <= {"a", "b", "c", "d"}
     *      True
     *      >>> d.items() <= {("a", 1), ("b", 2), ("c", 3), ("d", 4)}
     *      True
     */

    /* Overload the index operator[] to return MapProxies for elements within this
    dictionary. */
    inline linked::MapProxy<View> operator[](const Key& key) {
        return map(key);
    }

};


//////////////////////////////
////    DICT OPERATORS    ////
//////////////////////////////


/* Override the << operator to print the abbreviated contents of a dictionary to an
output stream (equivalent to Python repr()). */
template <typename K, typename V, unsigned int Flags, typename... Ts>
inline std::ostream& operator<<(
    std::ostream& stream,
    const LinkedDict<K, V, Flags, Ts...>& dict
) {
    stream << linked::build_repr(
        dict.view,
        "LinkedDict",
        "{",
        "}",
        64
    );
    return stream;
}


/* NOTE: dictionaries can be combined via the same set arithmetic operators as
 * LinkedSet (|, |=, -, -=, &, &=, ^, ^=), but only when the other operand is a
 * sequence of key-value pairs or another mapping type.  The same is true for
 * comparison operators, which are restricted to == and !=.
 */


/* Get the union between a LinkedDict and an arbitrary container. */
template <typename Map, typename K, typename V, unsigned int Flags, typename... Ts>
inline LinkedDict<K, V, Flags, Ts...> operator|(
    const LinkedDict<K, V, Flags, Ts...>& dict,
    const Map& other
) {
    return dict.union_(other);
}


/* Update a LinkedDict in-place, replacing it with the union of it and an arbitrary
container. */
template <typename Map, typename K, typename V, unsigned int Flags, typename... Ts>
inline LinkedDict<K, V, Flags, Ts...>& operator|=(
    LinkedDict<K, V, Flags, Ts...>& dict,
    const Map& other
) {
    dict.update(other);
    return dict;
}


/* Get the difference between a LinkedDict and an arbitrary container. */
template <typename Map, typename K, typename V, unsigned int Flags, typename... Ts>
inline LinkedDict<K, V, Flags, Ts...> operator-(
    const LinkedDict<K, V, Flags, Ts...>& dict,
    const Map& other
) {
    return dict.difference(other);
}


/* Update a LinkedDict in-place, replacing it with the difference between it and an
arbitrary container. */
template <typename Map, typename K, typename V, unsigned int Flags, typename... Ts>
inline LinkedDict<K, V, Flags, Ts...>& operator-=(
    LinkedDict<K, V, Flags, Ts...>& dict,
    const Map& other
) {
    dict.difference_update(other);
    return dict;
}


/* Get the intersection between a LinkedDict and an arbitrary container. */
template <typename Map, typename K, typename V, unsigned int Flags, typename... Ts>
inline LinkedDict<K, V, Flags, Ts...> operator&(
    const LinkedDict<K, V, Flags, Ts...>& dict,
    const Map& other
) {
    return dict.intersection(other);
}


/* Update a LinkedDict in-place, replacing it with the intersection between it and an
arbitrary container. */
template <typename Map, typename K, typename V, unsigned int Flags, typename... Ts>
inline LinkedDict<K, V, Flags, Ts...>& operator&=(
    LinkedDict<K, V, Flags, Ts...>& dict,
    const Map& other
) {
    dict.intersection_update(other);
    return dict;
}


/* Get the symmetric difference between a LinkedDict and an arbitrary container. */
template <typename Map, typename K, typename V, unsigned int Flags, typename... Ts>
inline LinkedDict<K, V, Flags, Ts...> operator^(
    const LinkedDict<K, V, Flags, Ts...>& dict,
    const Map& other
) {
    return dict.symmetric_difference(other);
}


/* Update a LinkedDict in-place, replacing it with the symmetric difference between it
and an arbitrary container. */
template <typename Map, typename K, typename V, unsigned int Flags, typename... Ts>
inline LinkedDict<K, V, Flags, Ts...>& operator^=(
    LinkedDict<K, V, Flags, Ts...>& dict,
    const Map& other
) {
    dict.symmetric_difference_update(other);
    return dict;
}


// TODO: implement proper == operators, accounting for both keys and values of this
// and the other container.  The other container should be any mapping type
// (python dict, std::unordered_map, etc.) or a sequence of key-value pairs.
// -> use SFINAE to determine whether an iterator over the other container
// dereferences to a key-value pair.
// -> Iterators over python dictionaries only dereference to keys, so we need to
// have a special case that calls PyDict_Next() to get key-value pairs.


/* Check whether a LinkedDict is equal to another mapping or container of pairs. */
template <typename Map, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator==(
    const LinkedDict<K, V, Flags, Ts...>& dict,
    const Map& other
) {
    // TODO: compare both keys and values
    return linked::set_equal(dict.view, other);
}


/* Check whether a LinkedDict is not equal to an arbitrary container. */
template <typename Map, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator!=(
    const LinkedDict<K, V, Flags, Ts...>& dict,
    const Map& other
) {
    // TODO: compare both keys and values
    return linked::set_not_equal(dict.view, other);
}


// TODO: reverse equivalents for ==, !=


//////////////////////
////    keys()    ////
//////////////////////


// TODO: Python wrappers for keys(), values(), items()


/* A read-only proxy for a dictionary's keys, in the same style as Python's
`dict.keys()` accessor. */
template <typename Dict>
class KeysProxy {
    friend Dict;
    const Dict& dict;

    KeysProxy(const Dict& dict) : dict(dict) {}

public:
    using View = typename Dict::View;
    using Key = typename Dict::Key;

    /* Get a read-only reference to the dictionary. */
    inline const Dict& mapping() const {
        return dict;
    }

    /* Get the index of a key within the dictionary. */
    inline size_t index(
        const Key& key,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return mapping().index(key, start, stop);
    }

    /* Count the number of occurrences of a key within the dictionary. */
    inline size_t count(
        const Key& key,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return mapping().count(key, start, stop);
    }

    /* Check if the referenced dictionary contains the given key. */
    inline bool contains(Key& key) const {
        return dict.contains(key);
    }

    /* Check whether the referenced dictionary has no keys in common with another
    container. */
    template <typename Container>
    inline bool isdisjoint(Container&& items) const {
        return linked::isdisjoint(dict.view, std::forward<Container>(items));
    }

    /* Check whether all the keys of the referenced dictionary are also present in
    another container. */
    template <typename Container>
    inline bool issubset(Container&& items) const {
        return linked::issubset(
            dict.view, std::forward<Container>(items), false
        );
    }

    /* Check whether all the keys within another container are also present in the
    referenced dictionary. */
    template <typename Container>
    inline bool issuperset(Container&& items) const {
        return linked::issuperset(
            dict.view, std::forward<Container>(items), false
        );
    }

    /* Get the total number of keys stored in the referenced dictionary. */
    inline size_t size() const {
        return dict.size();
    }

    /////////////////////////
    ////    ITERATORS    ////
    /////////////////////////

    /* Iterate through each key in the referenced dictionary. */
    inline auto begin() const { return dict.begin(); }
    inline auto end() const { return dict.end(); }
    inline auto cbegin() const { return dict.cbegin(); }
    inline auto cend() const { return dict.cend(); }
    inline auto rbegin() const { return dict.rbegin(); }
    inline auto rend() const { return dict.rend(); }
    inline auto crbegin() const { return dict.crbegin(); }
    inline auto crend() const { return dict.crend(); }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////


    /* Get a read-only proxy for a key at a certain index of the referenced
    dictionary. */
    inline const ElementProxy<View> position(long long index) const {
        return dict.position(index);
    }

    // TODO: have to consider the return value here.  Since the slice is const,
    // only the get() method is enabled, but that returns another DictView.
    // Instead, we want dict.keys()[:] to return another KeysProxy for the
    // same dictionary.

    // /* Get a read-only proxy for a slice of the referenced dictionary. */
    // template <typename... Args>
    // inline const SliceProxy<View, Dict> slice(Args&&... args) const {
    //     return dict.slice(std::forward<Args>(args)...);
    // }

    //////////////////////////////////
    ////    OPERATOR OVERLOADS    ////
    //////////////////////////////////

    /* NOTE: KeysProxies support set comparisons using the <, <=, ==, !=, >=, and >
     * operators,  even though the underlying dictionary does not.  They also support
     * the |, &, -, and ^ operators for set arithmetic, but not their in-place
     * equivalents (|=, &=, -=, ^=).  This is consistent with Python, which enforces
     * the same restrictions for its built-in dict type.  See the non-member operator
     * overloads below for more details.
     */

    /* Get the key at a particular index of the referenced dictionary. */
    inline const ElementProxy<View> operator[](long long index) const {
        return position(index);
    }

};


/* NOTE: set comparisons are only exposed for the keys() proxy of a LinkedDict
 * instance.  This is consistent with the Python API, which enforces the same rule for
 * its built-in dict type.  It's more explicit than comparing the dictionary directly,
 * since it is immediately clear that values are not included in the comparison.
 */


/* Check whether the keys in a LinkedDict form a proper subset of an arbitrary
container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator<(
    const KeysProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return linked::issubset(proxy.mapping().view, other, true);
}


/* Apply a reversed < comparison. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator<(
    const Container& other,
    const KeysProxy<LinkedDict<K, V, Flags, Ts...>>& proxy
) {
    return linked::issuperset(proxy.mapping().view, other, true);
}


/* Check whether the keys in a LinkedDict form a subset of an arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator<=(
    const KeysProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return linked::issubset(proxy.mapping().view, other, false);
}


/* Apply a reversed <= comparison. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator<=(
    const Container& other,
    const KeysProxy<LinkedDict<K, V, Flags, Ts...>>& proxy
) {
    return linked::issuperset(proxy.mapping().view, other, false);
}


/* Check whether the keys in a LinkedDict are equal to an arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator==(
    const KeysProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return linked::set_equal(proxy.mapping().view, other);
}


/* Apply a reversed == comparison. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator==(
    const Container& other,
    const KeysProxy<LinkedDict<K, V, Flags, Ts...>>& proxy
) {
    return linked::set_equal(proxy.mapping().view, other);
}


/* Check whether the keys in a LinkedDict are not equal to an arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator!=(
    const KeysProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return linked::set_not_equal(proxy.mapping().view, other);
}


/* Apply a reversed != comparison. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator!=(
    const Container& other,
    const KeysProxy<LinkedDict<K, V, Flags, Ts...>>& proxy
) {
    return linked::set_not_equal(proxy.mapping().view, other);
}


/* Check whether the keys in a LinkedDict form a superset of an arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator>=(
    const KeysProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return linked::issuperset(proxy.mapping().view, other, false);
}


/* Apply a reversed >= comparison. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator>=(
    const Container& other,
    const KeysProxy<LinkedDict<K, V, Flags, Ts...>>& proxy
) {
    return linked::issubset(proxy.mapping().view, other, false);
}


/* Check whether the keys in a LinkedDict form a proper superset of an arbitrary
container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator>(
    const KeysProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return linked::issuperset(proxy.mapping().view, other, true);
}


/* Apply a reversed > comparison. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator>(
    const Container& other,
    const KeysProxy<LinkedDict<K, V, Flags, Ts...>>& proxy
) {
    return linked::issubset(proxy.mapping().view, other, true);
}


////////////////////////
////    values()    ////
////////////////////////


/* A read-only proxy for a dictionary's values in the same style as Python's
`dict.values()` accessor. */
template <typename Dict>
class ValuesProxy {
    friend Dict;
    const Dict& dict;

    ValuesProxy(const Dict& dict) : dict(dict) {}

public:
    using Node = typename Dict::Node;  // used in index(), count() implementations
    using Value = typename Dict::Value;

    /* Get a read-only reference to the proxied dictionary. */
    inline const Dict& mapping() const {
        return dict;
    }

    /* Get the index of a key within the dictionary. */
    inline size_t index(
        const Value& key,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return linked::_listlike_index(*this, key, start, stop);
    }

    /* Count the number of occurrences of a key within the dictionary. */
    inline size_t count(
        const Value& key,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return linked::_listlike_count(*this, key, start, stop);
    }

    /* Check if the referenced dictionary contains the given value. */
    inline bool contains(const Value& value) const {
        for (auto val : *this) if (eq(val, value)) return true;
        return false;
    }

    /* Get the total number of values stored in the proxied dictionary. */
    inline size_t size() const {
        return dict.size();
    }

    /* A custom iterator that dereferences to the value associated with each key,
    rather than the keys themselves. */
    template <Direction dir>
    struct Iterator {
        using Iter = typename Dict::template Iterator<dir>;
        using Value = typename Dict::Value;
        Iter iter;

        /* Iterator traits. */
        using iterator_category     = std::forward_iterator_tag;
        using difference_type       = std::ptrdiff_t;
        using value_type            = std::remove_reference_t<Value>;
        using pointer               = value_type*;
        using reference             = value_type&;

        Iterator(const Iter&& base) : iter(std::move(base)) {}

        /* Dereference to the mapped value rather than the key. */
        inline Value operator*() const noexcept {
            return iter->curr()->mapped();
        }

        /* Advance to the next node. */
        inline Iterator& operator++() noexcept {
            ++iter;
            return *this;
        }

        /* Terminate the sequence. */
        template <Direction T>
        inline bool operator!=(const Iterator<T>& other) const noexcept {
            return iter != other.iter;
        }

    };

    /* Iterate through the values of the referenced dictionary. */
    inline auto begin() const { return Iterator<Direction::forward>(dict.begin()); }
    inline auto end() const { return Iterator<Direction::forward>(dict.end()); }
    inline auto cbegin() const { return Iterator<Direction::forward>(dict.cbegin()); }
    inline auto cend() const { return Iterator<Direction::forward>(dict.cend()); }
    inline auto rbegin() const { return Iterator<Direction::backward>(dict.rbegin()); }
    inline auto rend() const { return Iterator<Direction::backward>(dict.rend()); }
    inline auto crbegin() const { return Iterator<Direction::backward>(dict.crbegin()); }
    inline auto crend() const { return Iterator<Direction::backward>(dict.crend()); }

};


/* NOTE: Since LinkedDicts are fundamentally ordered, they support lexical comparisons
 * between their values() and arbitrary containers just like LinkedLists.  These will
 * be applied to the values in the same order as they appear in the dictionary.
 */


/* Check whether the values in a LinkedDict are lexically less than those of an
arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator<(
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return lexical_lt(proxy, other);
}


/* Apply a reversed < comparison. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator<(
    const Container& other,
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy
) {
    return lexical_lt(other, proxy);
}


/* Check whether the values in a LinkedDict are lexically less than or equal to those
of an arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator<=(
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return lexical_le(proxy, other);
}


/* Apply a reversed <= comparison. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator<=(
    const Container& other,
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy
) {
    return lexical_le(other, proxy);
}


/* Check whether the values in a LinkedDict are lexically equal to an arbitrary
container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator==(
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return lexical_eq(proxy, other);
}


/* Apply a reversed == comparison. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator==(
    const Container& other,
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy
) {
    return lexical_eq(other, proxy);
}


/* Check whether the values in a LinkedDict are not lexically equivalent to an
arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator!=(
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return !lexical_eq(proxy, other);
}


/* Apply a reversed != comparison. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator!=(
    const Container& other,
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy
) {
    return !lexical_eq(other, proxy);
}


/* Check whether the values in a LinkedDict are lexically greater than or equal to
those of an arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator>=(
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return lexical_ge(proxy, other);
}


/* Apply a reversed >= comparison. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator>=(
    const Container& other,
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy
) {
    return lexical_ge(other, proxy);
}


/* Check whether the values in a LinkedDict are lexically greater than those of an
arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator>(
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return lexical_gt(proxy, other);
}


/* Apply a reversed > comparison. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator>(
    const Container& other,
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy
) {
    return lexical_gt(other, proxy);
}


///////////////////////
////    items()    ////
///////////////////////


// TODO: this one's a doozy.  Dealing with sets of tuples is kind of a nightmare.


/* A read-only proxy for a dictionary's items, in the same style as Python's
`dict.items()` accessor. */
template <typename Dict>
class ItemsProxy {
    friend Dict;
    const Dict& dict;

    ItemsProxy(const Dict& dict) : dict(dict) {}

    /* Check whether the value type is hashable. */
    template <typename T, typename U = void>
    struct is_hashable : std::false_type {};
    template <typename T>
    struct is_hashable<T, std::void_t<decltype(std::hash<T>{}(std::declval<T>()))>> :
        std::true_type
    {};

public:
    using Key = typename Dict::Key;
    using Value = typename Dict::Value;
    static constexpr bool hashable = is_hashable<Value>::value;

    /* Get a read-only reference to the proxied dictionary. */
    inline const Dict& mapping() const {
        return dict;
    }

    /* Get the index of a key within the dictionary. */
    inline size_t index(
        const Key& key,
        const Value& value,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return linked::index(dict.view, key, value, start, stop);
    }

    /* Apply an index() check using a C++ pair. */
    inline size_t index(
        const std::pair<Key, Value> item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return index(item.first, item.second, start, stop);
    }

    /* Apply an index() check using a C++ tuple of size 2. */
    inline size_t index(
        const std::tuple<Key, Value>& item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return index(std::get<0>(item), std::get<1>(item), start, stop);
    }

    /* Apply an index() check using a Python tuple of size 2. */
    inline size_t index(
        PyObject* item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        if (!PyTuple_Check(item) || PyTuple_GET_SIZE(item) != 2) {
            throw TypeError("expected a tuple of size 2");
        }
        return index(
            PyTuple_GET_ITEM(item, 0),
            PyTuple_GET_ITEM(item, 1),
            start,
            stop
        );
    }

    /* Count the number of occurrences of a key within the dictionary. */
    inline size_t count(
        const Key& key,
        const Value& value,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return linked::count(dict.view, key, value, start, stop);
    }

    /* Apply a count() check using a C++ pair. */
    inline size_t count(
        const std::pair<Key, Value> item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return count(item.first, item.second, start, stop);
    }

    /* Apply a count() check using a C++ tuple of size 2. */
    inline size_t count(
        const std::tuple<Key, Value>& item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return count(std::get<0>(item), std::get<1>(item), start, stop);
    }

    /* Apply a count() check using a Python tuple of size 2. */
    inline size_t count(
        PyObject* item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        if (!PyTuple_Check(item) || PyTuple_GET_SIZE(item) != 2) {
            throw TypeError("expected a tuple of size 2");
        }
        return count(
            PyTuple_GET_ITEM(item, 0),
            PyTuple_GET_ITEM(item, 1),
            start,
            stop
        );
    }

    /* Check if the referenced dictionary contains the given key-value pair. */
    inline bool contains(const Key& key, const Value& value) const {
        typename Dict::Node* node = dict.view.search(key);
        return node != nullptr && eq(node->mapped(), value);
    }

    /* Apply a contains() check using a C++ pair. */
    inline bool contains(const std::pair<Key, Value> item) const {
        return contains(item.first, item.second);
    }

    /* Apply a contains() check using a C++ tuple of size 2. */
    inline bool contains(const std::tuple<Key, Value>& item) const {
        return contains(std::get<0>(item), std::get<1>(item));
    }

    /* Apply a contains() check using a Python tuple of size 2. */
    inline bool contains(PyObject* item) const {
        if (!PyTuple_Check(item) || PyTuple_GET_SIZE(item) != 2) {
            throw TypeError("expected a tuple of size 2");
        }
        return contains(PyTuple_GET_ITEM(item, 0), PyTuple_GET_ITEM(item, 1));
    }

    /* Get the total number of items stored in the proxied dictionary. */
    inline size_t size() const {
        return dict.size();
    }

    /* A custom iterator that dereferences to the value associated with each key,
    rather than the keys themselves. */
    template <Direction dir>
    struct Iterator {
        using Iter = typename Dict::template Iterator<dir>;
        using Key = typename Dict::Key;
        using Value = typename Dict::Value;
        Iter iter;

        /* Iterator traits. */
        using iterator_category     = std::forward_iterator_tag;
        using difference_type       = std::ptrdiff_t;
        using value_type            = std::remove_reference_t<Value>;
        using pointer               = value_type*;
        using reference             = value_type&;

        Iterator(const Iter&& base) : iter(std::move(base)) {}

        /* Dereference to the mapped value rather than the key. */
        inline std::pair<Key, Value> operator*() const noexcept {
            return std::make_pair(*iter, iter->curr()->mapped());
        }

        /* Advance to the next node. */
        inline Iterator& operator++() noexcept {
            ++iter;
            return *this;
        }

        /* Terminate the sequence. */
        template <Direction T>
        inline bool operator!=(const Iterator<T>& other) const noexcept {
            return iter != other.iter;
        }

    };

    /* Iterate through the values of the referenced dictionary. */
    inline auto begin() const { return Iterator<Direction::forward>(dict.begin()); }
    inline auto end() const { return Iterator<Direction::forward>(dict.end()); }
    inline auto cbegin() const { return Iterator<Direction::forward>(dict.cbegin()); }
    inline auto cend() const { return Iterator<Direction::forward>(dict.cend()); }
    inline auto rbegin() const { return Iterator<Direction::backward>(dict.rbegin()); }
    inline auto rend() const { return Iterator<Direction::backward>(dict.rend()); }
    inline auto crbegin() const { return Iterator<Direction::backward>(dict.crbegin()); }
    inline auto crend() const { return Iterator<Direction::backward>(dict.crend()); }

};


/* NOTE: items() proxies also support setlike comparisons just like keys(), but only
 * if all of the values are also hashable.  It can then be treated like a set of
 * key-value pairs, and comparisons will be based on both the keys and values of the
 * dictionary.  If the values are not hashable, then only == and != are supported.
 */


/* Check whether a LinkedDict is equal to an arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator==(
    const ItemsProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return proxy.mapping() == other;  // same as LinkedDict
}


/* Check whether a LinkedDict is not equal to an arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator!=(
    const ItemsProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return proxy.mapping() != other;  // same as LinkedDict
}


//////////////////////////////
////    PYTHON WRAPPER    ////
//////////////////////////////


/* CRTP mixin class containing the Python dict interface for a linked data structure. */
template <typename Derived>
class PyDictInterface {
public:

    // TODO: figure out how to implement fromkeys() as a class method.  Basically,
    // this is an alternate constructor without any of the __init__ flags.  It should
    // either return a dynamic dict or offer optional keyword arguments for the flags.

    // /* Implement `LinkedDict.fromkeys()` in Python. */
    // static PyObject* fromkeys(PyObject* type, PyObject* const* args, Py_ssize_t nargs) {
    //     using bertrand::util::PyArgs;
    //     using bertrand::util::CallProtocol;
    //     static constexpr std::string_view meth_name{"fromkeys"};
    //     try {
    //         // parse arguments
    //         PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
    //         PyObject* keys = pyargs.parse("keys");
    //         std::optional<PyObject*> value = pyargs.parse(
    //             "value", nullptr, std::optional<PyObject*>(Py_None)
    //         );
    //
    //         // invoked equivalent C++ method
    //         std::visit(
    //             [&keys, &value](auto& dict) {
    //                 dict.fromkeys(keys, value);
    //             },
    //             self->variant
    //         );
    //
    //     // translate C++ errors into Python exceptions
    //     } catch (...) {
    //         throw_python();
    //         return nullptr;
    //     }
    // }

    /* Implement `LinkedDict.insert()` in Python. */
    static PyObject* insert(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        using bertrand::util::PyArgs;
        using bertrand::util::CallProtocol;
        static constexpr std::string_view meth_name{"distance"};
        try {
            // parse arguments
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            PyObject* value = pyargs.parse("value");
            pyargs.finalize();

            // invoked equivalent C++ method
            std::visit(
                [&key, &value](auto& dict) {
                    dict.insert(key, value);
                },
                self->variant
            );
            Py_RETURN_NONE;

        // translate C++ errors into Python exceptions
        } catch (...) {
            throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedDict.pop()` in Python. */
    static PyObject* pop(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        using bertrand::util::PyArgs;
        using bertrand::util::CallProtocol;
        static constexpr std::string_view meth_name{"pop"};
        try {
            // parse arguments
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            std::optional<PyObject*> default_ = pyargs.parse(
                "default", nullptr, std::optional<PyObject*>()
            );
            pyargs.finalize();

            // invoked equivalent C++ method
            return std::visit(
                [&key, &default_](auto& dict) {
                    return dict.pop(key, default_);
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedDict.popitem()` in Python. */
    static PyObject* popitem(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        using bertrand::util::PyArgs;
        using bertrand::util::CallProtocol;
        using bertrand::util::parse_int;
        static constexpr std::string_view meth_name{"pop"};
        try {
            // parse arguments
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            long long index = pyargs.parse("index", parse_int, (long long)-1);
            pyargs.finalize();

            // invoke equivalent C++ method
            return std::visit(
                [&index](auto& dict) {
                    return dict.popitem(index);  // returns new reference
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedDict.get()` in Python. */
    static PyObject* get(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        using bertrand::util::PyArgs;
        using bertrand::util::CallProtocol;
        static constexpr std::string_view meth_name{"get"};
        try {
            // parse arguments
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            std::optional<PyObject*> default_ = pyargs.parse(
                "default", nullptr, std::optional<PyObject*>()
            );
            pyargs.finalize();

            // invoke equivalent C++ method
            return std::visit(
                [&key, &default_](auto& dict) {
                    return dict.get(key, default_);  // returns new reference
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedDict.lru_get()` in Python. */
    static PyObject* lru_get(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        using bertrand::util::PyArgs;
        using bertrand::util::CallProtocol;
        static constexpr std::string_view meth_name{"lru_get"};
        try {
            // parse arguments
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            std::optional<PyObject*> default_ = pyargs.parse(
                "default", nullptr, std::optional<PyObject*>()
            );
            pyargs.finalize();

            // invoke equivalent C++ method
            return std::visit(
                [&key, &default_](auto& dict) {
                    return dict.lru_get(key, default_);  // returns new reference
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedDict.setdefault()` in Python. */
    static PyObject* setdefault(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        using bertrand::util::PyArgs;
        using bertrand::util::CallProtocol;
        static constexpr std::string_view meth_name{"setdefault"};
        try {
            // parse arguments
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            PyObject* value = pyargs.parse("value");
            pyargs.finalize();

            // invoke equivalent C++ method
            return std::visit(
                [&key, &value](auto& dict) {
                    return dict.setdefault(key, value);  // returns new reference
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedDict.setdefault_left()` in Python. */
    static PyObject* setdefault_left(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        using bertrand::util::PyArgs;
        using bertrand::util::CallProtocol;
        static constexpr std::string_view meth_name{"setdefault_left"};
        try {
            // parse arguments
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            PyObject* value = pyargs.parse("value");
            pyargs.finalize();

            // invoke equivalent C++ method
            return std::visit(
                [&key, &value](auto& dict) {
                    return dict.setdefault_left(key, value);  // returns new reference
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedDict.lru_setdefault()` in Python. */
    static PyObject* lru_setdefault(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        using bertrand::util::PyArgs;
        using bertrand::util::CallProtocol;
        static constexpr std::string_view meth_name{"lru_setdefault"};
        try {
            // parse arguments
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            PyObject* value = pyargs.parse("value");
            pyargs.finalize();

            // invoke equivalent C++ method
            return std::visit(
                [&key, &value](auto& dict) {
                    return dict.lru_setdefault(key, value);  // returns new reference
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            throw_python();
            return nullptr;
        }
    }

    // TODO: thoroughly check these

    /* Implement `LinkedDict.keys()` in Python. */
    static PyObject* keys(Derived* self, PyObject* /* ignored */) {
        try {
            // invoke equivalent C++ method
            return std::visit(
                [](auto& dict) {
                    using Proxy = typename std::decay_t<decltype(dict.keys())>;
                    return Proxy::PyType.construct(dict.keys());
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedDict.values()` in Python. */
    static PyObject* values(Derived* self, PyObject* /* ignored */) {
        try {
            // invoke equivalent C++ method
            return std::visit(
                [](auto& dict) {
                    using Proxy = typename std::decay_t<decltype(dict.values())>;
                    return Proxy::PyType.construct(dict.values());
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedDict.items()` in Python. */
    static PyObject* items(Derived* self, PyObject* /* ignored */) {
        try {
            // invoke equivalent C++ method
            return std::visit(
                [](auto& dict) {
                    using Proxy = typename std::decay_t<decltype(dict.items())>;
                    return Proxy::PyType.construct(dict.items());
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedDict.__getitem__()` in Python. */
    static PyObject* __getitem__(Derived* self, PyObject* key) {
        try {
            // invoke equivalent C++ method
            return std::visit(
                [&key](auto& dict) {
                    return Py_XNewRef(dict[key].get());
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedDict.__setitem__()/__delitem__()` in Python. */
    static int __setitem__(Derived* self, PyObject* key, PyObject* value) {
        try {
            // invoke equivalent C++ method
            std::visit(
                [&key, &value](auto& dict) {
                    dict[key] = value;
                },
                self->variant
            );
            return 0;

        // translate C++ errors into Python exceptions
        } catch (...) {
            throw_python();
            return -1;
        }
    }

    /* Implement `LinkedList.__lt__()/__le__()/__eq__()/__ne__()/__ge__()/__gt__()` in
    Python. */
    static PyObject* __richcompare__(Derived* self, PyObject* other, int cmp) {
        try {
            bool result = std::visit(
                [&other, &cmp](auto& list) {
                    switch (cmp) {
                        case Py_EQ:
                            return list == other;
                        case Py_NE:
                            return list != other;
                        default:
                            throw ValueError("invalid comparison operator");
                    }
                },
                self->variant
            );
            return PyBool_FromLong(result);
        } catch (...) {
            throw_python();
            return nullptr;
        }
    }

protected:

    /* docstrings for public Python attributes. */
    struct docs {

        static constexpr std::string_view fromkeys {R"doc(
Create a new ``LinkedDict`` with keys from ``iterable`` and values set to
``value``.

This is a class method.

Parameters
----------
iterable : Iterable[Hashable]
    An iterable of keys to use for the new ``LinkedDict``.
value : Any, default None
    The value to use for all keys in ``iterable``.
    
Returns
-------
LinkedDict
    A new ``LinkedDict`` with keys from ``iterable`` and values set to
    ``value``.

Notes
-----
Note that all keys will refer to just a singly instance of ``value``.  This can
lead to unexpected behavior if ``value`` is a mutable object, such as a list.
To get distinct values, use a generator expression in the ``LinkedDict``
constructor itself.
)doc"
        };

        static constexpr std::string_view insert {R"doc(
Insert a new key-value pair into the ``LinkedDict`` at the specified index.

Parameters
----------
index : int
    The index at which to insert the new key-value pair.  This can be negative,
    following the same syntax as Python lists.
key : Hashable
    The key to insert.  Must be unique.
value : Any
    The value to insert.

Raises
------
KeyError
    If ``key`` is already present in the ``LinkedDict``.
)doc"
        };

        static constexpr std::string_view pop {R"doc(
Remove the specified key from the dictionary and return its value or an
optional default.

Parameters
----------
key : Hashable
    The key to remove.
default : Any, optional
    The value to return if ``key`` is not present in the ``LinkedDict``.  If
    not provided, a ``KeyError`` will be raised instead.

Returns
-------
Any
    The value associated with ``key`` if it was present in the ``LinkedDict``,
    otherwise ``default``.

Raises
------
KeyError
    If ``key`` is not present in the ``LinkedDict`` and ``default`` was not
    provided.

Notes
-----
Pops are O(1) for doubly-linked dictionaries and O(n) for singly-linked ones.
This is due to the need to traverse the entire dictionary in order to find the
previous node.
)doc"
        };

        static constexpr std::string_view popitem {R"doc(
Remove the key at the specified index and return its value.

Parameters
----------
index : int, default -1
    The index of the key to remove.  If not specified, the last key will be
    removed.  This can be negative, following the same syntax as Python lists.

Returns
-------
Any
    The value associated with the key at ``index``.

Raises
------
IndexError
    If the dictionary is empty or if ``index`` is out of bounds.

Notes
-----
``popitem()`` is analogous to :meth:`LinkedList.pop()`, and has the same
behavior.  It is consistent with the built-in :meth:`dict.popitem()` method,
except that it takes an optional index rather than always popping in LIFO
order.

Users should note that pops have different performance characteristics based on
whether they occur at the front or back of the dictionary.  Popping from the
front of a dictionary is O(1) for both singly- and doubly-linked dictionaries.
Popping from the back, however, is only O(1) for doubly-linked dictionaries.
It is O(n) for singly-linked dictionaries because the whole dictionary must be
traversed to find the new tail.

Pops towards the middle of the dictionary are O(n) in both cases.
)doc"
        };

        static constexpr std::string_view get {R"doc(
Return the value associated with a given key, or an optional default if it is
not present.

Parameters
----------
key : Hashable
    The key to look up.
default : Any, default None
    The value to return if ``key`` is not present in the ``LinkedDict``.

Returns
-------
Any
    The value associated with ``key`` if it was present in the ``LinkedDict``,
    otherwise ``default``.

Notes
-----
Gets are always O(1).
)doc"
        };

        static constexpr std::string_view lru_get {R"doc(
Return the value associated with a given key and move it to the front of the
dictionary if it is present.  Otherwise, return an optional default.

Parameters
----------
key : Hashable
    The key to look up.
default : Any, default None
    The value to return if ``key`` is not present in the ``LinkedDict``.

Returns
-------
Any
    The value associated with ``key`` if it was present in the ``LinkedDict``,
    otherwise ``default``.

Notes
-----
LRU gets are O(1) for doubly-linked dictionaries and O(n) on average for
singly-linked ones if the key is already prsent.  This is due to the need to
traverse the dictionary in order to find the previous node.
)doc"
        };

        static constexpr std::string_view setdefault {R"doc(
Get the value associated with a given key, or insert a new key-value pair if
it is not present.

Parameters
----------
key : Hashable
    The key to look up.
default : Any, default None
    The value to insert if ``key`` is not present in the dictionary.

Returns
-------
Any
    The value associated with ``key`` if it was present in the ``LinkedDict``,
    otherwise ``default``.

Notes
-----
``setdefault()`` is always O(1).
)doc"
        };

        static constexpr std::string_view setdefault_left {R"doc(
Get the value associated with a given key, or left-insert a new key-value pair
if it is not present.

Parameters
----------
key : Hashable
    The key to look up.
default : Any, default None
    The value to insert if ``key`` is not present in the dictionary.

Returns
-------
Any
    The value associated with ``key`` if it was present in the ``LinkedDict``,
    otherwise ``default``.

Notes
-----
``setdefault_left()`` is always O(1).
)doc"
        };

        static constexpr std::string_view lru_setdefault {R"doc(
Get the value associated with a given key and move it to the front of the
dictionary, or left-insert a new key-value pair and evict the tail node to make
room if necessary.

Parameters
----------
key : Hashable
    The key to look up.
default : Any, default None
    The value to insert if ``key`` is not present in the dictionary.

Returns
-------
Any
    The value associated with ``key`` if it was present in the ``LinkedDict``,
    otherwise ``default``.

Notes
-----
LRU setdefaults are O(1) for doubly-linked dictionaries and O(n) on average for
singly-linked ones if the key is already present.  This is due to the need to
traverse the dictionary in order to find the previous node.
)doc"
        };

        static constexpr std::string_view keys {R"doc(
Return a setlike, read-only proxy for the keys within the dictionary.

Returns
-------
KeysProxy
    A proxy for the keys within the dictionary.  This behaves like a read-only
    ``LinkedSet``, and supports many of the same operations.

Notes
-----
The proxy returned by this method can be iterated over, indexed, and compared
with other sets or setlike objects using the standard set operators.
)doc"
        };

        static constexpr std::string_view values {R"doc(
Return a listlike, read-only proxy for the values within the dictionary.

Returns
-------
ValuesProxy
    A proxy for the values within the dictionary.  This behaves like a read-only
    ``LinkedList``, and supports many of the same operations.

Notes
-----
The proxy returned by this method can be iterated over, indexed, and compared
with other lists or listlike objects using the standard list operators.
)doc"
        };

        static constexpr std::string_view items {R"doc(
Return a read-only proxy for the key-value pairs within the dictionary.

Returns
-------
ItemsProxy
    A proxy for the key-value pairs within the dictionary.  This behaves like a
    read-only set of pairs.

Notes
-----
The proxy returned by this method can be iterated over and indexed like a list.
It also supports setlike comparisons with other sets or setlike objects if and
only if the dictionary's values are also hashable.
)doc"
        };

    };

};


/* A discriminated union of templated `LinkedDict` types that can be used from
Python. */
class PyLinkedDict :
    public PyLinkedBase<PyLinkedDict>,
    public PyListInterface<PyLinkedDict>,
    public PySetInterface<PyLinkedDict>,
    public PyDictInterface<PyLinkedDict>
{
    using Base = PyLinkedBase<PyLinkedDict>;
    using IList = PyListInterface<PyLinkedDict>;
    using ISet = PySetInterface<PyLinkedDict>;
    using IDict = PyDictInterface<PyLinkedDict>;

    /* A std::variant representing all of the LinkedDict implementations that are
    constructable from Python. */
    template <unsigned int Flags>
    using DictConfig = linked::LinkedDict<PyObject*, PyObject*, Flags, BasicLock>;
    using Variant = std::variant<
        DictConfig<Config::DOUBLY_LINKED | Config::DYNAMIC>
        // DictConfig<Config::DOUBLY_LINKED | Config::DYNAMIC | Config::PACKED>,
        // DictConfig<Config::DOUBLY_LINKED | Config::DYNAMIC | Config::STRICTLY_TYPED>,
        // DictConfig<Config::DOUBLY_LINKED | Config::DYNAMIC | Config::PACKED | Config::STRICTLY_TYPED>,
        // DictConfig<Config::DOUBLY_LINKED | Config::FIXED_SIZE>,
        // DictConfig<Config::DOUBLY_LINKED | Config::FIXED_SIZE | Config::PACKED>,
        // DictConfig<Config::DOUBLY_LINKED | Config::FIXED_SIZE | Config::STRICTLY_TYPED>,
        // DictConfig<Config::DOUBLY_LINKED | Config::FIXED_SIZE | Config::PACKED | Config::STRICTLY_TYPED>,
        // DictConfig<Config::SINGLY_LINKED | Config::DYNAMIC>,
        // DictConfig<Config::SINGLY_LINKED | Config::DYNAMIC | Config::PACKED>,
        // DictConfig<Config::SINGLY_LINKED | Config::DYNAMIC | Config::STRICTLY_TYPED>,
        // DictConfig<Config::SINGLY_LINKED | Config::DYNAMIC | Config::PACKED | Config::STRICTLY_TYPED>,
        // DictConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE>,
        // DictConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::PACKED>,
        // DictConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::STRICTLY_TYPED>
        // DictConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::PACKED | Config::STRICTLY_TYPED>
    >;

    friend Base;
    friend IList;
    friend ISet;
    friend IDict;
    Variant variant;

    /* Construct a PyLinkedDict around an existing C++ LinkedDict. */
    template <typename Dict>
    inline void from_cpp(Dict&& dict) {
        new (&variant) Variant(std::forward<Dict>(dict));
    }

    /* Construct a particular alternative stored in the variant. */
    template <size_t I>
    inline static void alt(
        PyLinkedDict* self,
        PyObject* iterable,
        std::optional<size_t> max_size,
        PyObject* spec,
        bool reverse
    ) {
        using Alt = typename std::variant_alternative_t<I, Variant>;
        if (iterable == nullptr) {
            new (&self->variant) Variant(Alt(max_size, spec));
        } else { \
            new (&self->variant) Variant(Alt(iterable, max_size, spec, reverse));
        }
    }

    /* Construct a PyLinkedDict from scratch using the given constructor arguments. */
    static void construct(
        PyLinkedDict* self,
        PyObject* iterable,
        std::optional<size_t> max_size,
        PyObject* spec,
        bool reverse,
        bool singly_linked,
        bool packed,
        bool strictly_typed
    ) {
        unsigned int code = (
            Config::SINGLY_LINKED * singly_linked |
            Config::FIXED_SIZE * max_size.has_value() |
            Config::PACKED * packed |
            Config::STRICTLY_TYPED * strictly_typed
        );
        switch (code) {
            case (Config::DEFAULT):
                alt<0>(self, iterable, max_size, spec, reverse);
                break;
            // // case (Config::PACKED):
            // //     alt<1>(self, iterable, max_size, spec, reverse);
            // //     break;
            // case (Config::STRICTLY_TYPED):
            //     alt<1>(self, iterable, max_size, spec, reverse);
            //     break;
            // // case (Config::PACKED | Config::STRICTLY_TYPED):
            // //     alt<3>(self, iterable, max_size, spec, reverse);
            // //     break;
            // case (Config::FIXED_SIZE):
            //     alt<2>(self, iterable, max_size, spec, reverse);
            //     break;
            // // case (Config::FIXED_SIZE | Config::PACKED):
            // //     alt<5>(self, iterable, max_size, spec, reverse);
            // //     break;
            // case (Config::FIXED_SIZE | Config::STRICTLY_TYPED):
            //     alt<3>(self, iterable, max_size, spec, reverse);
            //     break;
            // // case (Config::FIXED_SIZE | Config::PACKED | Config::STRICTLY_TYPED):
            // //     alt<7>(self, iterable, max_size, spec, reverse);
            // //     break;
            // case (Config::SINGLY_LINKED):
            //     alt<4>(self, iterable, max_size, spec, reverse);
            //     break;
            // // case (Config::SINGLY_LINKED | Config::PACKED):
            // //     alt<9>(self, iterable, max_size, spec, reverse);
            // //     break;
            // case (Config::SINGLY_LINKED | Config::STRICTLY_TYPED):
            //     alt<5>(self, iterable, max_size, spec, reverse);
            //     break;
            // // case (Config::SINGLY_LINKED | Config::PACKED | Config::STRICTLY_TYPED):
            // //     alt<11>(self, iterable, max_size, spec, reverse);
            // //     break;
            // case (Config::SINGLY_LINKED | Config::FIXED_SIZE):
            //     alt<6>(self, iterable, max_size, spec, reverse);
            //     break;
            // // case (Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::PACKED):
            // //     alt<13>(self, iterable, max_size, spec, reverse);
            // //     break;
            // case (Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::STRICTLY_TYPED):
            //     alt<7>(self, iterable, max_size, spec, reverse);
            //     break;
            // // case (Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::PACKED | Config::STRICTLY_TYPED):
            // //     alt<15>(self, iterable, max_size, spec, reverse);
            // //     break;
            default:
                throw ValueError("invalid argument configuration");
        }
    }

public:

    /* Initialize a LinkedDict instance from Python. */
    static int __init__(PyLinkedDict* self, PyObject* args, PyObject* kwargs) {
        using bertrand::util::PyArgs;
        using bertrand::util::CallProtocol;
        using bertrand::util::parse_int;
        using bertrand::util::none_to_null;
        using bertrand::util::is_truthy;
        static constexpr std::string_view meth_name{"__init__"};
        try {
            // parse arguments
            PyArgs<CallProtocol::KWARGS> pyargs(meth_name, args, kwargs);
            PyObject* iterable = pyargs.parse(
                "iterable", none_to_null, (PyObject*) nullptr
            );
            std::optional<size_t> max_size = pyargs.parse(
                "max_size",
                [](PyObject* obj) -> std::optional<size_t> {
                    if (obj == Py_None) return std::nullopt;
                    long long result = parse_int(obj);
                    if (result < 0) throw ValueError("max_size cannot be negative");
                    return std::make_optional(static_cast<size_t>(result));
                },
                std::optional<size_t>()
            );
            PyObject* spec = pyargs.parse("spec", none_to_null, (PyObject*) nullptr);
            bool reverse = pyargs.parse("reverse", is_truthy, false);
            bool singly_linked = pyargs.parse("singly_linked", is_truthy, false);
            bool packed = pyargs.parse("packed", is_truthy, false);
            pyargs.finalize();

            // initialize
            construct(
                self, iterable, max_size, spec, reverse, singly_linked, packed, false
            );

            // exit normally
            return 0;

        // translate C++ exceptions into Python eerrors
        } catch (...) {
            throw_python();
            return -1;
        }
    }

    /* Implement `LinkedDict.__str__()` in Python. */
    static PyObject* __str__(PyLinkedDict* self) {
        try {
            std::ostringstream stream;
            stream << "{";
            std::visit(
                [&stream](auto& dict) {
                    auto it = dict.begin();
                    auto end = dict.end();
                    if (it != end) {
                        stream << repr(*it) << ": ";
                        stream << repr(it.curr()->mapped());
                        ++it;
                    }
                    for (; it != end; ++it) {
                        stream << ", " << repr(*it) << ": ";
                        stream << repr(it.curr()->mapped());
                    }
                },
                self->variant
            );
            stream << "}";
            auto str = stream.str();
            return PyUnicode_FromStringAndSize(str.c_str(), str.size());

        // translate C++ errors into Python exceptions
        } catch (...) {
            throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedDict.__repr__()` in Python. */
    static PyObject* __repr__(PyLinkedDict* self) {
        try {
            std::ostringstream stream;
            std::visit(
                [&stream](auto& dict) {
                    stream << dict;
                },
                self->variant
            );
            auto str = stream.str();
            return PyUnicode_FromStringAndSize(str.c_str(), str.size());

        // translate C++ errors into Python exceptions
        } catch (...) {
            throw_python();
            return nullptr;
        }
    }

private:

    /* docstrings for public Python attributes. */
    struct docs {

        static constexpr std::string_view LinkedDict {R"doc(
A modular, ordered dictionary based on a linked list available in both Python
and C++.

This class is a drop-in replacement for a built-in :class:`dict`, supporting
all the same operations, plus some from the :class:`set` and :class:`list`
interfaces, as well as extras leveraging the ordered nature of the dictionary.
It is also available as a C++ type under the same name, with identical
semantics.

Parameters
----------
items : Iterable[Any], optional
    The key-value pairs to initialize the dictionary with.  This can be any
    mapping type or iterable of pairs of the form ``(key, value)``.  If not
    specified, the dictionary will be empty.
max_size : int, optional
    The maximum number of keys that the dictionary can hold.  If not specified,
    the dictionary will be unbounded.
spec : Any, optional
    A specific type to enforce for elements of the dictionary, allowing the
    creation of type-safe containers.  This can be in any format recognized by
    :func:`isinstance() <python:isinstance>`, and can be provided as a
    :class:`slice` to specialize the key and value types separately.  By
    default, only the keys will be specialized.  The default is ``None``, which
    disables type checking for the dictionary.  See the :meth:`specialize()`
    method for more details.
reverse : bool, default False
    If True, reverse the order of ``items`` during dictionary construction.
    This is more efficient than calling :meth:`reverse()` after construction.
singly_linked : bool, default False
    If True, use a singly-linked dictionary instead of a doubly-linked one.
    This trades some performance in certain operations for increased memory
    efficiency.  Regardless of this setting, the dictionary will still support
    all the same operations.
packed : bool, default False
    If True, use a packed allocator that does not pad its contents to the
    system's preferred alignment.  This can free between 2 and 6 bytes per
    node at the cost of slightly reduced performance (depending on the system).
    Regardless of this setting, the dictionary will still support all the same
    operations.

Notes
-----
These data structures are highly optimized, and offer performance that is
generally on par with the built-in :class:`dict` type.  They have slightly more
overhead due to handling the links between each node, but users should not
notice a significant difference on average.

The data structure itself is implemented entirely in C++, and can be used
equivalently at the C++ level.  In fact, the Python wrapper is just a
discriminated union of C++ templates, and can be thought of as directly emitting
equivalent C++ code at runtime.  As such, each variation of this data structure
is available as a C++ type under the same name, with identical semantics and
only superficial syntax differences related to both languages.  Here's an
example:

.. code-block:: cpp

    #include <bertrand/structs/linked/list.h>

    int main() {
        using Item = std::pair<std::string, int>
        std::vector<Item> items = {
            {"a", 1}, {"b", 2}, {"c", 3}, {"d", 4}, {"e", 5}
        };
        bertrand::LinkedDict<std::string, int> dict(items);

        dict["f"] = 6;
        dict.update(std::vector<Item>{{"g", 7}, {"h", 8}, {"i", 9}});
        int x = dict.pop("i");
        dict.rotate(4);
        dict["i"] = x;
        for (Item i : dict.items()) {
            // ...
        }

        std::cout << dict;
        // LinkedDict({"f": 6, "g": 7, "h": 8, "a": 1, "b": 2, "c": 3, "d": 4, "i": 9})
        return 0;
    }

This makes it significantly easier to port code that relies on this data
structure between the two languages.  In fact, doing so provides significant
benefits, allowing users to take advantage of static C++ types and completely
bypass the Python interpreter, increasing performance by orders of magnitude
in some cases.
)doc"
        };

    };

    ////////////////////////////////
    ////    PYTHON INTERNALS    ////
    ////////////////////////////////

    #define BASE_PROPERTY(NAME) \
        { #NAME, (getter) Base::NAME, NULL, PyDoc_STR(Base::docs::NAME.data()) } \

    #define BASE_METHOD(NAME, ARG_PROTOCOL) \
        { #NAME, (PyCFunction) Base::NAME, ARG_PROTOCOL, PyDoc_STR(Base::docs::NAME.data()) } \

    #define LIST_METHOD(NAME, ARG_PROTOCOL) \
        { #NAME, (PyCFunction) IList::NAME, ARG_PROTOCOL, PyDoc_STR(IList::docs::NAME.data()) } \

    #define SET_METHOD(NAME, ARG_PROTOCOL) \
        { #NAME, (PyCFunction) ISet::NAME, ARG_PROTOCOL, PyDoc_STR(ISet::docs::NAME.data()) } \

    #define DICT_METHOD(NAME, ARG_PROTOCOL) \
        { #NAME, (PyCFunction) IDict::NAME, ARG_PROTOCOL, PyDoc_STR(IDict::docs::NAME.data()) } \

    /* Vtable containing Python @property definitions for the LinkedDict */
    inline static PyGetSetDef properties[] = {
        BASE_PROPERTY(SINGLY_LINKED),
        BASE_PROPERTY(DOUBLY_LINKED),
        // BASE_PROPERTY(XOR),  // not yet implemented
        BASE_PROPERTY(DYNAMIC),
        BASE_PROPERTY(PACKED),
        BASE_PROPERTY(STRICTLY_TYPED),
        BASE_PROPERTY(lock),
        BASE_PROPERTY(capacity),
        BASE_PROPERTY(max_size),
        BASE_PROPERTY(frozen),
        BASE_PROPERTY(nbytes),
        BASE_PROPERTY(specialization),
        {NULL}  // sentinel
    };

    /* Vtable containing Python method definitions for the LinkedDict. */
    inline static PyMethodDef methods[] = {
        BASE_METHOD(reserve, METH_FASTCALL),
        BASE_METHOD(defragment, METH_NOARGS),
        BASE_METHOD(specialize, METH_O),
        BASE_METHOD(__reversed__, METH_NOARGS),
        BASE_METHOD(__class_getitem__, METH_CLASS | METH_O),
        LIST_METHOD(index, METH_FASTCALL),
        LIST_METHOD(count, METH_FASTCALL),
        LIST_METHOD(clear, METH_NOARGS),
        LIST_METHOD(copy, METH_NOARGS),
        LIST_METHOD(sort, METH_FASTCALL | METH_KEYWORDS),
        LIST_METHOD(reverse, METH_NOARGS),
        LIST_METHOD(rotate, METH_FASTCALL),
        SET_METHOD(remove, METH_O),
        SET_METHOD(discard, METH_O),
        SET_METHOD(lru_contains, METH_O),
        {
            "union",  // renamed
            (PyCFunction) union_,
            METH_FASTCALL,
            PyDoc_STR(ISet::docs::union_.data())
        },
        SET_METHOD(union_left, METH_FASTCALL),
        SET_METHOD(update, METH_FASTCALL),
        SET_METHOD(update_left, METH_FASTCALL),
        SET_METHOD(lru_update, METH_FASTCALL),
        SET_METHOD(difference, METH_FASTCALL),
        SET_METHOD(difference_update, METH_FASTCALL),
        SET_METHOD(intersection, METH_FASTCALL),
        SET_METHOD(intersection_update, METH_FASTCALL),
        SET_METHOD(symmetric_difference, METH_O),
        SET_METHOD(symmetric_difference_left, METH_O),
        SET_METHOD(symmetric_difference_update, METH_O),
        SET_METHOD(symmetric_difference_update_left, METH_O),
        SET_METHOD(isdisjoint, METH_O),
        SET_METHOD(issubset, METH_O),
        SET_METHOD(issuperset, METH_O),
        SET_METHOD(distance, METH_FASTCALL),
        SET_METHOD(swap, METH_FASTCALL),
        SET_METHOD(move, METH_FASTCALL),
        SET_METHOD(move_to_index, METH_FASTCALL),
        DICT_METHOD(fromkeys, METH_FASTCALL | METH_CLASS),
        DICT_METHOD(insert, METH_FASTCALL),
        DICT_METHOD(pop, METH_FASTCALL),
        DICT_METHOD(popitem, METH_FASTCALL),
        DICT_METHOD(get, METH_FASTCALL),
        DICT_METHOD(lru_get, METH_FASTCALL),
        DICT_METHOD(setdefault, METH_FASTCALL),
        DICT_METHOD(setdefault_left, METH_FASTCALL),
        DICT_METHOD(lru_setdefault, METH_FASTCALL),
        DICT_METHOD(keys, METH_NOARGS),
        DICT_METHOD(values, METH_NOARGS),
        DICT_METHOD(items, METH_NOARGS),
        {NULL}  // sentinel
    };

    #undef BASE_PROPERTY
    #undef BASE_METHOD
    #undef LIST_METHOD
    #undef SET_METHOD
    #undef DICT_METHOD

    /* Vtable containing special methods related to Python's mapping protocol. */
    inline static PyMappingMethods mapping = [] {
        PyMappingMethods slots;
        slots.mp_length = (lenfunc) Base::__len__;
        slots.mp_subscript = (binaryfunc) IDict::__getitem__;
        slots.mp_ass_subscript = (objobjargproc) IDict::__setitem__;
        return slots;
    }();

    /* Vtable containing special methods related to Python's sequence protocol. */
    inline static PySequenceMethods sequence = [] {
        PySequenceMethods slots;
        slots.sq_length = (lenfunc) Base::__len__;
        slots.sq_item = (ssizeargfunc) IList::__getitem_scalar__;
        slots.sq_ass_item = (ssizeobjargproc) IList::__setitem_scalar__;
        slots.sq_contains = (objobjproc) IList::__contains__;
        return slots;
    }();

    /* Vtable containing special methods related to Python's number protocol. */
    inline static PyNumberMethods number = [] {
        PyNumberMethods slots;
        slots.nb_or = (binaryfunc) ISet::__or__;
        slots.nb_inplace_or = (binaryfunc) ISet::__ior__;
        slots.nb_subtract = (binaryfunc) ISet::__sub__;
        slots.nb_inplace_subtract = (binaryfunc) ISet::__isub__;
        slots.nb_and = (binaryfunc) ISet::__and__;
        slots.nb_inplace_and = (binaryfunc) ISet::__iand__;
        slots.nb_xor = (binaryfunc) ISet::__xor__;
        slots.nb_inplace_xor = (binaryfunc) ISet::__ixor__;
        return slots;
    }();

    /* Initialize a PyTypeObject to represent the set in Python. */
    static PyTypeObject build_type() {
        return {
            .ob_base = PyObject_HEAD_INIT(NULL)
            .tp_name = "bertrand.LinkedDict",
            .tp_basicsize = sizeof(PyLinkedDict),
            .tp_itemsize = 0,
            .tp_dealloc = (destructor) Base::__dealloc__,
            .tp_repr = (reprfunc) __repr__,
            .tp_as_number = &number,
            .tp_as_sequence = &sequence,
            .tp_as_mapping = &mapping,
            .tp_hash = (hashfunc) PyObject_HashNotImplemented,  // not hashable
            .tp_str = (reprfunc) __str__,
            .tp_flags = (
                Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC |
                Py_TPFLAGS_IMMUTABLETYPE | Py_TPFLAGS_SEQUENCE
                // add Py_TPFLAGS_MANAGED_WEAKREF for Python 3.12+
            ),
            .tp_doc = PyDoc_STR(docs::LinkedDict.data()),
            .tp_traverse = (traverseproc) Base::__traverse__,
            .tp_clear = (inquiry) Base::__clear__,
            .tp_richcompare = (richcmpfunc) IDict::__richcompare__,
            .tp_iter = (getiterfunc) Base::__iter__,
            .tp_methods = methods,
            .tp_getset = properties,
            .tp_init = (initproc) __init__,
            .tp_new = (newfunc) Base::__new__,
        };
    };

public:

    /* The final Python type as a PyTypeObject. */
    inline static PyTypeObject Type = build_type();

    /* Check whether another PyObject* is of this type. */
    inline static bool typecheck(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &Type);
        if (result == -1) throw catch_python();
        return static_cast<bool>(result);
    }

};


/* Python module definition. */
static struct PyModuleDef module_dict = {
    PyModuleDef_HEAD_INIT,
    .m_name = "dict",
    .m_doc = (
        "This module contains an optimized LinkedDict data structure for use "
        "in Python.  The exact same data structure is also available in C++ "
        "under the same header path (bertrand/structs/linked/dict.h)."
    ),
    .m_size = -1,
};


/* Python import hook. */
PyMODINIT_FUNC PyInit_dict(void) {
    // initialize type objects
    if (PyType_Ready(&PyLinkedDict::Type) < 0) return nullptr;

    // initialize module
    PyObject* mod = PyModule_Create(&module_dict);
    if (mod == nullptr) return nullptr;

    // link type to module
    Py_INCREF(&PyLinkedDict::Type);
    if (PyModule_AddObject(mod, "LinkedDict", (PyObject*) &PyLinkedDict::Type) < 0) {
        Py_DECREF(&PyLinkedDict::Type);
        Py_DECREF(mod);
        return nullptr;
    }
    return mod;
}


}  // namespace linked
}  // namespace structs


/* Export to base namespace. */
using structs::linked::LinkedDict;
using structs::linked::PyLinkedDict;


}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_DICT_H
