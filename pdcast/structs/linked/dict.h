// include guard: BERTRAND_STRUCTS_LINKED_DICT_H
#ifndef BERTRAND_STRUCTS_LINKED_DICT_H
#define BERTRAND_STRUCTS_LINKED_DICT_H

#include <cstddef>  // size_t
#include <iterator>
#include <optional>  // std::optional
#include <ostream>  // std::ostream
#include <sstream>  // std::ostringstream
#include <type_traits>
#include <utility>  // std::pair
#include <variant>  // std::variant
#include <Python.h>  // CPython API
#include "../util/args.h"  // PyArgs
#include "../util/except.h"  // throw_python()
#include "algorithms/map.h"
#include "core/view.h"  // DictView
#include "base.h"  // LinkedBase
#include "list.h"  // PyListInterface
#include "set.h"  // PySetInterface

#include "algorithms/add.h"
#include "algorithms/contains.h"
#include "algorithms/count.h"
#include "algorithms/discard.h"
#include "algorithms/distance.h"
#include "algorithms/index.h"
#include "algorithms/insert.h"
#include "algorithms/lexical_compare.h"
#include "algorithms/move.h"
#include "algorithms/pop.h"
#include "algorithms/position.h"
// #include "algorithms/relative.h"
#include "algorithms/remove.h"
#include "algorithms/repr.h"
#include "algorithms/reverse.h"
#include "algorithms/rotate.h"
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
    typename Lock = util::BasicLock
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
        // TODO: allocate a new view, then pass it to linked::fromkeys(), then
        // move it into a new LinkedDict

        return linked::fromkeys<View, LinkedDict>(
            std::forward<Container>(keys),
            value
        );
    }

    /* Add an item to the end of the dictionary if it is not already present. */
    template <typename Pair>
    inline void add(const Pair& item) {
        linked::add(this->view, item);
    }

    /* Add a key-value pair to the end of the dictionary if it is not already
    present. */
    inline void add(const Key& key, const Value& value) {
        linked::add(this->view, key, value);
    }

    /* Add an item to the beginning of the dictionary if it is not already present. */
    template <typename Pair>
    inline void add_left(const Pair& item) {
        linked::add_left(this->view, item);
    }

    /* Add a key-value pair to the beginning of the dictionary if it is not already
    present. */
    inline void add_left(const Key& key, const Value& value) {
        linked::add_left(this->view, key, value);
    }

    /* Add an item to the front of the dictionary, evicting the last item if
    necessary and moving items that are already present. */
    template <typename Pair>
    inline void lru_add(const Pair& item) {
        linked::lru_add(this->view, item);
    }

    /* Add a key-value pair to the front of the dictionary, evicting the last item if
    necessary and moving items that are already present. */
    inline void lru_add(const Key& key, const Value& value) {
        linked::lru_add(this->view, key, value);
    }

    /* Insert an item at a specific index of the dictionary. */
    template <typename Pair>
    inline void insert(long long index, const Pair& item) {
        linked::insert(this->view, index, item);
    }

    /* Insert a key-value pair at a specific index of the dictionary. */
    inline void insert(long long index, const Key& key, const Value& value) {
        linked::insert(this->view, index, key, value);
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

    /* Remove elements from a dictionary that are contained in one or more iterables. */
    template <typename... Containers>
    inline void difference_update(Containers&&... items) {
        (linked::difference_update(this->view, std::forward<Containers>(items)), ...);
    }

    /* Remove elements from a dictionary that are contained in one or more iterables. */
    template <typename... Containers>
    inline void intersection_update(Containers&&... items) {
        (linked::intersection_update(this->view, std::forward<Containers>(items)), ...);
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

    // TODO: popitem should be able to take an index which defaults to -1 just like
    // pop() for the other containers

    /* Remove and return a key, value pair from the dictionary. */
    inline std::pair<Key, Value> popitem() {
        return linked::popitem(this->view);
    }

    /* Remove all elements from the dictionary. */
    inline void clear() {
        linked::clear(this->view);
    }

    /* Get a value from the dictionary using an optional default. */
    inline Value get(
        const Key& key,
        std::optional<Value> default_value = std::nullopt
    ) const {
        return linked::get(this->view, key, default_value);
    }

    /* Set a value within the dictionary or insert it if it is not already present. */
    inline void setdefault(const Key& key, const Value& default_value) {
        linked::setdefault(this->view, key, default_value);
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

    /* Return a new dictionary with elements common to this dictionary and all other
    containers. */
    template <typename... Containers>
    inline LinkedDict intersection(Containers&&... items) const {
        return LinkedDict(
            linked::intersection(this->view, std::forward<Containers>(items)...)
        );
    }

    /* Return a new dictionary with elements from this dictionary that are common to
    any other containers. */
    template <typename... Containers>
    inline LinkedDict difference(Containers&&... items) const {
        return LinkedDict(
            linked::difference(this->view, std::forward<Containers>(items)...)
        );
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
    inline KeysProxy<LinkedDict> keys() const {
        return KeysProxy<LinkedDict>(*this);
    }

    /* Get a read-only, setlike proxy for the values within this dictionary. */
    inline ValuesProxy<LinkedDict> values() const {
        return ValuesProxy<LinkedDict>(*this);
    }

    /* Get a read-only, setlike proxy for the key-value pairs within this dictionary. */
    inline ItemsProxy<LinkedDict> items() const {
        return ItemsProxy<LinkedDict>(*this);
    }

    /* Get a proxy for a particular key within the dictionary. */
    inline linked::MapProxy<View> map(const Key& key) {
        return linked::map(this->view, key);
    }

    /* Get a proxy for a value at a particular index of the dictionary. */
    inline linked::ElementProxy<View> position(long long index) {
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
    stream << linked::repr(
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


/* Get the union between a LinkedDict and an arbitrary container. */
template <typename Map, typename K, typename V, unsigned int Flags, typename... Ts>
inline LinkedDict<K, V, Flags, Ts...> operator|(
    const LinkedDict<K, V, Flags, Ts...>& dict,
    const Map& other
) {
    return dict.union_(other);
}


/* Get the difference between a LinkedDict and an arbitrary container. */
template <typename Map, typename K, typename V, unsigned int Flags, typename... Ts>
inline LinkedDict<K, V, Flags, Ts...> operator-(
    const LinkedDict<K, V, Flags, Ts...>& dict,
    const Map& other
) {
    return dict.difference(other);
}


/* Get the intersection between a LinkedDict and an arbitrary container. */
template <typename Map, typename K, typename V, unsigned int Flags, typename... Ts>
inline LinkedDict<K, V, Flags, Ts...> operator&(
    const LinkedDict<K, V, Flags, Ts...>& dict,
    const Map& other
) {
    return dict.intersection(other);
}


/* Get the symmetric difference between a LinkedDict and an arbitrary container. */
template <typename Map, typename K, typename V, unsigned int Flags, typename... Ts>
inline LinkedDict<K, V, Flags, Ts...> operator^(
    const LinkedDict<K, V, Flags, Ts...>& dict,
    const Map& other
) {
    return dict.symmetric_difference(other);
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


////////////////////////
////    KEY VIEW    ////
////////////////////////


/* A read-only proxy for a dictionary's keys, in the same style as Python's
`dict.keys()` accessor. */
template <typename Dict>
class KeysProxy {
    friend Dict;
    const Dict& dict;

    KeysProxy(const Dict& dict) : dict(dict) {}

public:
    using Key = typename Dict::Key;

    /* Get a read-only reference to the dictionary. */
    inline const Dict& mapping() const {
        return dict;
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

    /* Iterate through the keys of the referenced dictionary. */
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

    // TODO: position(), slice() ?

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
    inline const ElementProxy<typename Dict::View> operator[](long long index) const {
        return dict.position(index);
    }

};


/* NOTE: set comparisons are only exposed for the keys() proxy of a LinkedDict
 * instance.  This is consistent with the Python API, which enforces the same rule for
 * its built-in dict type.  It's more explicit than comparing the dictionary directly,
 * since it is immediately clear that values are not included in the comparison.
 */


/* Check whether the keys in a LinkedDict are equal to an arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator==(
    const KeysProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
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


/* Check whether the keys in a LinkedDict form a proper subset of an arbitrary
container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator<(
    const KeysProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return linked::issubset(proxy.mapping().view, other, true);
}


/* Check whether the keys in a LinkedDict form a subset of an arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator<=(
    const KeysProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return linked::issubset(proxy.mapping().view, other, false);
}


/* Check whether the keys in a LinkedDict form a superset of an arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator>=(
    const KeysProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return linked::issuperset(proxy.mapping().view, other, false);
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


//////////////////////////
////    VALUE VIEW    ////
//////////////////////////


/* A read-only proxy for a dictionary's values in the same style as Python's
`dict.values()` accessor. */
template <typename Dict>
class ValuesProxy {
    friend Dict;
    const Dict& dict;

    ValuesProxy(const Dict& dict) : dict(dict) {}

public:

    /* Get a read-only reference to the proxied dictionary. */
    inline const Dict& mapping() const {
        return dict;
    }

    /* Check if the referenced dictionary contains the given value. */
    inline bool contains(const typename Dict::Value& value) const {
        return dict.contains(value);  // TODO: replicate behavior of LinkedList.contains().
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


/* Check whether the values in a LinkedDict are lexically equal to an arbitrary
container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator==(
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return linked::lexical_eq(proxy, other);
}


/* Check whether the values in a LinkedDict are not lexically equivalent to an
arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator!=(
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return !linked::lexical_eq(proxy, other);
}


/* Check whether the values in a LinkedDict are lexically less than those of an
arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator<(
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return linked::lexical_lt(proxy, other);
}


/* Check whether the values in a LinkedDict are lexically less than or equal to those
of an arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator<=(
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return linked::lexical_le(proxy, other);
}


/* Check whether the values in a LinkedDict are lexically greater than or equal to
those of an arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator>=(
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return linked::lexical_ge(proxy, other);
}


/* Check whether the values in a LinkedDict are lexically greater than those of an
arbitrary container. */
template <typename Container, typename K, typename V, unsigned int Flags, typename... Ts>
inline bool operator>(
    const ValuesProxy<LinkedDict<K, V, Flags, Ts...>>& proxy,
    const Container& other
) {
    return linked::lexical_gt(proxy, other);
}


/////////////////////////
////    ITEM VIEW    ////
/////////////////////////


/* A read-only proxy for a dictionary's items, in the same style as Python's
`dict.items()` accessor. */
template <typename Dict>
class ItemsProxy {
    friend Dict;
    const Dict& dict;

    ItemsProxy(const Dict& dict) : dict(dict) {}

    /* Check whether std::hash<> is specialized for the given value type. */
    template <typename T, typename = void>
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

    /* Check if the referenced dictionary contains the given value. */
    inline bool contains(const std::pair<Key, Value> item) const {
        typename Dict::Node* node = dict.view.search(item.first);
        return node != nullptr && util::eq(node->mapped(), item.second);
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



protected:

    /* docstrings for public Python attributes. */
    struct docs {

        // TODO: add() overloads for key-value pairs?
        // -> maybe these should just be on the map() proxy itself.
        // dict[key].lru_get()
        // dict[key].lru_set()
        // dict[key].set_left()

        static constexpr std::string_view fromkeys {R"doc(

)doc"
        };

        static constexpr std::string_view pop {R"doc(

)doc"
        };

        static constexpr std::string_view popitem {R"doc(

)doc"
        };

        static constexpr std::string_view get {R"doc(

)doc"
        };

        static constexpr std::string_view setdefault {R"doc(

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
    using DictConfig = linked::LinkedDict<PyObject*, PyObject*, Flags, util::BasicLock>;
    using Variant = std::variant<
        DictConfig<Config::DOUBLY_LINKED | Config::DYNAMIC>,
        // DictConfig<Config::DOUBLY_LINKED | Config::DYNAMIC | Config::PACKED>,
        DictConfig<Config::DOUBLY_LINKED | Config::DYNAMIC | Config::STRICTLY_TYPED>,
        // DictConfig<Config::DOUBLY_LINKED | Config::DYNAMIC | Config::PACKED | Config::STRICTLY_TYPED>,
        DictConfig<Config::DOUBLY_LINKED | Config::FIXED_SIZE>,
        // DictConfig<Config::DOUBLY_LINKED | Config::FIXED_SIZE | Config::PACKED>,
        DictConfig<Config::DOUBLY_LINKED | Config::FIXED_SIZE | Config::STRICTLY_TYPED>,
        // DictConfig<Config::DOUBLY_LINKED | Config::FIXED_SIZE | Config::PACKED | Config::STRICTLY_TYPED>,
        DictConfig<Config::SINGLY_LINKED | Config::DYNAMIC>,
        // DictConfig<Config::SINGLY_LINKED | Config::DYNAMIC | Config::PACKED>,
        DictConfig<Config::SINGLY_LINKED | Config::DYNAMIC | Config::STRICTLY_TYPED>,
        // DictConfig<Config::SINGLY_LINKED | Config::DYNAMIC | Config::PACKED | Config::STRICTLY_TYPED>,
        DictConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE>,
        // DictConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::PACKED>,
        DictConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::STRICTLY_TYPED>
        // DictConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::PACKED | Config::STRICTLY_TYPED>
    >;
    template <size_t I>
    using Alternative = typename std::variant_alternative_t<I, Variant>;

    friend Base;
    friend IList;
    friend ISet;
    friend IDict;
    Variant variant;

    /* Construct a PyLinkedDict around an existing C++ LinkedDict. */
    template <typename Set>
    inline void from_cpp(Set&& set) {
        new (&variant) Variant(std::forward<Set>(set));
    }

    #define CONSTRUCT(IDX) \
        if (iterable == nullptr) { \
            new (&self->variant) Variant(Alternative<IDX>(max_size, spec)); \
        } else { \
            new (&self->variant) Variant( \
                Alternative<IDX>(iterable, max_size, spec, reverse) \
            ); \
        } \
        break; \

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
                CONSTRUCT(0)
            // case (Config::PACKED):
            //     CONSTRUCT(1)
            case (Config::STRICTLY_TYPED):
                CONSTRUCT(1)
            // case (Config::PACKED | Config::STRICTLY_TYPED):
            //     CONSTRUCT(3)
            case (Config::FIXED_SIZE):
                CONSTRUCT(2)
            // case (Config::FIXED_SIZE | Config::PACKED):
            //     CONSTRUCT(5)
            case (Config::FIXED_SIZE | Config::STRICTLY_TYPED):
                CONSTRUCT(3)
            // case (Config::FIXED_SIZE | Config::PACKED | Config::STRICTLY_TYPED):
            //     CONSTRUCT(7)
            case (Config::SINGLY_LINKED):
                CONSTRUCT(4)
            // case (Config::SINGLY_LINKED | Config::PACKED):
            //     CONSTRUCT(9)
            case (Config::SINGLY_LINKED | Config::STRICTLY_TYPED):
                CONSTRUCT(5)
            // case (Config::SINGLY_LINKED | Config::PACKED | Config::STRICTLY_TYPED):
            //     CONSTRUCT(11)
            case (Config::SINGLY_LINKED | Config::FIXED_SIZE):
                CONSTRUCT(6)
            // case (Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::PACKED):
            //     CONSTRUCT(13)
            case (Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::STRICTLY_TYPED):
                CONSTRUCT(7)
            // case (Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::PACKED | Config::STRICTLY_TYPED):
            //     CONSTRUCT(15)
            default:
                throw util::ValueError("invalid argument configuration");
        }
    }

    #undef CONSTRUCT

public:

    /* Initialize a LinkedDict instance from Python. */
    static int __init__(PyLinkedDict* self, PyObject* args, PyObject* kwargs) {
        using Args = util::PyArgs<util::CallProtocol::KWARGS>;
        using util::ValueError;
        static constexpr std::string_view meth_name{"__init__"};
        try {
            // parse arguments
            Args pyargs(meth_name, args, kwargs);
            PyObject* iterable = pyargs.parse(
                "iterable", util::none_to_null, (PyObject*)nullptr
            );
            std::optional<size_t> max_size = pyargs.parse(
                "max_size",
                [](PyObject* obj) -> std::optional<size_t> {
                    if (obj == Py_None) return std::nullopt;
                    long long result = util::parse_int(obj);
                    if (result < 0) throw ValueError("max_size cannot be negative");
                    return std::make_optional(static_cast<size_t>(result));
                },
                std::optional<size_t>()
            );
            PyObject* spec = pyargs.parse("spec", util::none_to_null, (PyObject*) nullptr);
            bool reverse = pyargs.parse("reverse", util::is_truthy, false);
            bool singly_linked = pyargs.parse("singly_linked", util::is_truthy, false);
            pyargs.finalize();

            // initialize
            construct(self, iterable, max_size, spec, reverse, singly_linked);

            // exit normally
            return 0;

        // translate C++ exceptions into Python eerrors
        } catch (...) {
            util::throw_python();
            return -1;
        }
    }

    // TODO: account for mapped values in final string.

    /* Implement `LinkedDict.__str__()` in Python. */
    static PyObject* __str__(PyLinkedDict* self) {
        try {
            std::ostringstream stream;
            stream << "{";
            std::visit(
                [&stream](auto& set) {
                    auto it = set.begin();
                    if (it != set.end()) {
                        stream << util::repr(*it);
                        ++it;
                    }
                    for (; it != set.end(); ++it) {
                        stream << ", " << util::repr(*it);
                    }
                },
                self->variant
            );
            stream << "}";
            return PyUnicode_FromString(stream.str().c_str());

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedDict.__repr__()` in Python. */
    static PyObject* __repr__(PyLinkedDict* self) {
        try {
            std::ostringstream stream;
            std::visit(
                [&stream](auto& set) {
                    stream << set;
                },
                self->variant
            );
            return PyUnicode_FromString(stream.str().c_str());

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

private:

    /* docstrings for public Python attributes. */
    struct docs {

        static constexpr std::string_view LinkedDict {R"doc(

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

    // TODO: add dictionary-specific methods

    /* Vtable containing Python method definitions for the LinkedDict. */
    inline static PyMethodDef methods[] = {
        BASE_METHOD(reserve, METH_FASTCALL),
        BASE_METHOD(defragment, METH_NOARGS),
        BASE_METHOD(specialize, METH_O),
        BASE_METHOD(__reversed__, METH_NOARGS),
        BASE_METHOD(__class_getitem__, METH_CLASS | METH_O),
        LIST_METHOD(insert, METH_FASTCALL),
        LIST_METHOD(index, METH_FASTCALL),
        LIST_METHOD(count, METH_FASTCALL),
        LIST_METHOD(remove, METH_O),
        LIST_METHOD(pop, METH_FASTCALL),
        LIST_METHOD(clear, METH_NOARGS),
        LIST_METHOD(copy, METH_NOARGS),
        LIST_METHOD(sort, METH_FASTCALL | METH_KEYWORDS),
        LIST_METHOD(reverse, METH_NOARGS),
        LIST_METHOD(rotate, METH_FASTCALL),
        SET_METHOD(add, METH_O),
        SET_METHOD(add_left, METH_O),
        SET_METHOD(lru_add, METH_O),
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
        {NULL}  // sentinel
    };

    #undef BASE_PROPERTY
    #undef BASE_METHOD
    #undef LIST_METHOD
    #undef SET_METHOD
    #undef DICT_METHOD

    // TODO: use dictionary-specific mapping methods

    /* Vtable containing special methods related to Python's mapping protocol. */
    inline static PyMappingMethods mapping = [] {
        PyMappingMethods slots;
        slots.mp_length = (lenfunc) Base::__len__;
        slots.mp_subscript = (binaryfunc) IList::__getitem__;
        slots.mp_ass_subscript = (objobjargproc) IList::__setitem__;
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
            .tp_name = "bertrand.structs.LinkedDict",
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
            .tp_richcompare = (richcmpfunc) IList::__richcompare__,
            .tp_iter = (getiterfunc) Base::__iter__,
            .tp_methods = methods,
            .tp_getset = properties,
            .tp_init = (initproc) __init__,
            .tp_new = (newfunc) Base::__new__,
        };
    };

public:

    /* The final Python type. */
    inline static PyTypeObject Type = build_type();

    /* Check whether another PyObject* is of this type. */
    inline static bool typecheck(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &Type);
        if (result == -1) throw util::catch_python();
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
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_DICT_H
