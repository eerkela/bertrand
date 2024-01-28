#ifndef BERTRAND_STRUCTS_LINKED_SET_H
#define BERTRAND_STRUCTS_LINKED_SET_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <ostream>  // std::ostream
#include <sstream>  // std::ostringstream
#include <variant>  // std::variant
#include <Python.h>  // CPython API
#include "../util/args.h"  // PyArgs
#include "../util/except.h"  // throw_python()
#include "../util/ops.h"  // repr()
#include "core/allocate.h"  // Config
#include "core/view.h"  // SetView
#include "base.h"  // LinkedBase
#include "list.h"  // PyListInterface

#include "algorithms/add.h"
#include "algorithms/contains.h"
#include "algorithms/count.h"
#include "algorithms/discard.h"
#include "algorithms/distance.h"
#include "algorithms/index.h"
#include "algorithms/insert.h"
#include "algorithms/move.h"
#include "algorithms/pop.h"
#include "algorithms/position.h"
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
namespace linked {


template <typename K, unsigned int Flags, typename Lock>
class LinkedSet;


namespace set_config {

    template <typename T, unsigned int Flags>
    using NodeSelect = std::conditional_t<
        !!(Flags & Config::SINGLY_LINKED),
        SingleNode<T>,
        DoubleNode<T>
    >;

}


/////////////////////////
////    LINKEDSET    ////
/////////////////////////


/* An ordered set based on a combined linked list and hash table. */
template <
    typename K,
    unsigned int Flags = Config::DEFAULT,
    typename Lock = BasicLock
>
class LinkedSet : public LinkedBase<
    linked::SetView<set_config::NodeSelect<K, Flags>, Flags>,
    Lock
> {
    using Base = LinkedBase<
        linked::SetView<set_config::NodeSelect<K, Flags>, Flags>,
        Lock
    >;
    using DynamicSet = LinkedSet<K, Flags & ~Config::FIXED_SIZE, Lock>;

    DynamicSet as_dynamic() const {
        DynamicSet result(this->size(), this->specialization());
        for (auto it = this->begin(), end = this->end(); it != end; ++it) {
            result.view.template node<Base::Allocator::INSERT_TAIL>(*(it.curr()));
        }
        return result;
    }

public:
    using View = typename Base::View;
    using Key = K;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    #if defined(__GNUC__) && !defined(__clang__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
    #elif defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wuninitialized"
    #elif defined(_MSC_VER)
        #pragma warning(push)
        #pragma warning(disable: 26494)  // VAR_USE_BEFORE_INIT
        #pragma warning(disable: 26495)  // MEMBER_UNINIT
    #endif

    LinkedSet(
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr
    ) : Base(
        [&] {
            if constexpr (DEBUG) {
                LOGGER.address(this);
                LOGGER("LinkedSet(", repr(max_size), ", ", repr(spec), ")");
                LOGGER.indent();
            }
            return max_size;
        }(),
        spec
    ) {
        if constexpr (DEBUG) {
            LOGGER.unindent();
        }
    }

    template <typename Container>
    LinkedSet(
        Container&& iterable,
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr,
        bool reverse = false
    ) : Base(
        [&] {
            if constexpr (DEBUG) {
                LOGGER.address(this);
                LOGGER(
                    "LinkedSet(", repr(iterable), ", ", repr(max_size), ", ",
                    repr(spec), ", ", reverse, ")"
                );
                LOGGER.indent();
            }
            return std::forward<Container>(iterable);
        }(),
        max_size,
        spec,
        reverse
    ) {
        if constexpr (DEBUG) {
            LOGGER.unindent();
        }
    }

    template <typename Iterator>
    LinkedSet(
        Iterator&& begin,
        Iterator&& end,
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr,
        bool reverse = false
    ) : Base(
        [&] {
            if constexpr (DEBUG) {
                LOGGER.address(this);
                LOGGER(
                    "LinkedSet(", repr(begin), ", ", repr(end), ", ",
                    repr(max_size), ", ", repr(spec), ", ", reverse, ")"
                );
                LOGGER.indent();
            }
            return begin;
        }(),
        end,
        max_size,
        spec,
        reverse
    ) {
        if constexpr (DEBUG) {
            LOGGER.unindent();
        }
    }

    template <typename X>
    LinkedSet(
        std::initializer_list<X> init,
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr,
        bool reverse = false
    ) : Base(
        [&] {
            if constexpr (DEBUG) {
                LOGGER.address(this);
                LOGGER(
                    "LinkedSet(", repr(init), ", ", repr(max_size), ", ",
                    repr(spec), ", ", reverse, ")"
                );
                LOGGER.indent();
            }
            return init;
        }(),
        max_size,
        spec,
        reverse
    ) {
        if constexpr (DEBUG) {
            LOGGER.unindent();
        }
    }

    LinkedSet(View&& view) : Base([&] {
        if constexpr (DEBUG) {
            LOGGER.address(this);
            LOGGER("LinkedSet(", repr(view), ")  # from view");
            LOGGER.indent();
        }
        return std::move(view);
    }()) {
        if constexpr (DEBUG) {
            LOGGER.unindent();
        }
    }

    LinkedSet(const LinkedSet& other) : Base([&] {
        if constexpr (DEBUG) {
            LOGGER.address(this);
            LOGGER("LinkedSet(", &other, ")  # copy");
            LOGGER.indent();
        }
        return other.view;
    }()) {
        if constexpr (DEBUG) {
            LOGGER.unindent();
        }
    }

    LinkedSet(LinkedSet&& other) : Base([&] {
        if constexpr (DEBUG) {
            LOGGER.address(this);
            LOGGER("LinkedSet(", &other, ")  # move");
            LOGGER.indent();
        }
        return std::move(other.view);
    }()) {
        if constexpr (DEBUG) {
            LOGGER.unindent();
        }
    }

    LinkedSet& operator=(const LinkedSet& other) {
        if (this == &other) {
            return *this;
        }
        LOG_CONTEXT(this, "LinkedSet = ", &other, "  # copy");
        Base::operator=(other);
        return *this;
    }

    LinkedSet& operator=(LinkedSet&& other) {
        if (this == &other) {
            return *this;
        }
        LOG_CONTEXT(this, "LinkedSet = ", &other, "  # move");
        Base::operator=(std::move(other));
        return *this;
    }

    ~LinkedSet() {
        if constexpr (DEBUG) {
            LOGGER.address(this);
            LOGGER("~LinkedSet");
            LOGGER.indent();  // indent to be closed in Allocator::~BaseAllocator()
        }
    }

    #if defined(__GNUC__) && !defined(__clang__)
        #pragma GCC diagnostic pop
    #elif defined(__clang__)
        // #pragma clang diagnostic pop
    #elif defined(_MSC_VER)
        #pragma warning(pop)
    #endif

    /////////////////////////////
    ////    SET INTERFACE    ////
    /////////////////////////////

    /* LinkedSets implement the full Python set interface with equivalent semantics to
     * the built-in Python set type.  They are also ordered just like LinkedLists, and
     * inherit many of the same operations.  The following are some notable differences
     * from the Python set interface:
     *
     *      1.  The add(), union()/update(), and symmetric_difference()/
     *          symmetric_difference_update() methods have corresponding *_left()
     *          and lru_*() counterparts.  These methods append to the head of the set
     *          rather than the tail.  The lru_*() methods also move existing items to
     *          the head of the set and evict the tail to make room if necessary.
     *      2.  Similarly, the lru_contains() method can be used to check if a key is
     *          present in the set and move it to the head of the set if so.
     *      3.  The set supports the index(), count(), sort(), reverse(), and rotate()
     *          methods from the LinkedList interface, which all behave identically to
     *          their list equivalents.
     *      4.  The insert() method, which inserts a key at a specific index, will
     *          raise an error if the given key is already present within the set.
     *      5.  The pop() method accepts an optional index argument, which pops the
     *          element at a specific index rather than always popping from the tail.
     *      6.  The set supports additional methods not found on unordered sets, such
     *          as:
     *              distance(key1, key2):
     *                  get the number of indices from key1 to key2.
     *              swap(key1, key2):
     *                  swap the positions of key1 and key2.  
     *              move(key, steps):
     *                  move a key by the specified number of steps relative to its
     *                  current position.
     *              move_to_index(key, index):
     *                  move key to the specified index relative to the start of the
     *                  set.
     *      7.  Sets can be positionally sliced and accessed just like lists.  If a
     *          non-unique key is inserted, an error will be thrown and the set will
     *          return to its original state.
     *      8.  Lastly, at the C++ level, Python's set.union() method is renamed to
     *          union_() to prevent a naming conflict with the C++ union keyword.
     *
     * Most of these are related to the fact that LinkedSets are fundamentally ordered
     * and thus equivalent in many respects to LinkedLists.  They can therefore be used
     * as both sets and lists with constant-time access to each element.  The interface
     * supports both use cases, with similar performance to Python's built-in set type.
     */

    /* Add a key to the end of the set if it is not already present. */
    inline void add(const Key& key) {
        LOG_CONTEXT(this,  "LinkedSet::add(", repr(key), ")");
        linked::add(this->view, key);
    }

    /* Add a key to the beginning of the set if it is not already present. */
    inline void add_left(const Key& key) {
        LOG_CONTEXT(this,  "LinkedSet::add_left(", repr(key), ")");
        linked::add_left(this->view, key);
    }

    /* Add a key to the set if it is not already present and move it to the front of
    the set, evicting the last element to make room if necessary. */
    inline void lru_add(const Key& key) {
        LOG_CONTEXT(this,  "LinkedSet::lru_add(", repr(key), ")");
        linked::lru_add(this->view, key);
    }

    /* Insert a key at a specific index of the set. */
    inline void insert(long long index, const Key& key) {
        LOG_CONTEXT(this,  "LinkedSet::insert(", index, ", ", repr(key), ")");
        linked::insert(this->view, index, key);
    }

    /* Get the index of a key within the set. */
    inline size_t index(
        const Key& key,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        LOG_CONTEXT(
            this,  "LinkedSet::index(", repr(key), ", ", repr(start), ", ",
            repr(stop), ")"
        );
        return linked::index(this->view, key, start, stop);
    }

    /* Count the number of occurrences of a key within the set. */
    inline size_t count(
        const Key& key,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        LOG_CONTEXT(
            this,  "LinkedSet::count(", repr(key), ", ", repr(start), ", ",
            repr(stop), ")"
        );
        return linked::count(this->view, key, start, stop);
    }

    /* Check if the set contains a certain key. */
    inline bool contains(const Key& key) const {
        LOG_CONTEXT(this,  "LinkedSet::contains(", repr(key), ")");
        return linked::contains(this->view, key);
    }

    /* Check if the set contains a certain key and move it to the front of the set
    if so. */
    inline bool lru_contains(const Key& key) {
        LOG_CONTEXT(this,  "LinkedSet::lru_contains(", repr(key), ")");
        return linked::lru_contains(this->view, key);
    }

    /* Remove a key from the set. */
    inline void remove(const Key& key) {
        LOG_CONTEXT(this,  "LinkedSet::remove(", repr(key), ")");
        linked::remove(this->view, key);
    }

    /* Remove a key from the set if it is present. */
    inline void discard(const Key& key) {
        LOG_CONTEXT(this,  "LinkedSet::discard(", repr(key), ")");
        linked::discard(this->view, key);
    }

    /* Remove a key from the set and return its value. */
    inline Key pop(long long index = -1) {
        LOG_CONTEXT(this,  "LinkedSet::pop(", index, ")");
        return linked::pop(this->view, index);
    }

    /* Remove all elements from the set. */
    inline void clear() {
        LOG_CONTEXT(this,  "LinkedSet::clear()");
        this->view.clear();
    }

    /* Return a shallow copy of the set. */
    inline LinkedSet copy() const {
        LOG_CONTEXT(this,  "LinkedSet::copy()");
        return LinkedSet(this->view.copy());
    }

    /* Sort the set in-place according to an optional key func. */
    template <typename Func>
    inline void sort(Func key = nullptr, bool reverse = false) {
        LOG_CONTEXT(this,  "LinkedSet::sort(", repr(key), ", ", reverse, ")");
        linked::sort<linked::MergeSort>(this->view, key, reverse);
    }

    /* Reverse the order of elements in the set in-place. */
    inline void reverse() {
        LOG_CONTEXT(this,  "LinkedSet::reverse()");
        linked::reverse(this->view);
    }

    /* Shift all elements in the set to the right by the specified number of steps. */
    inline void rotate(long long steps = 1) {
        LOG_CONTEXT(this,  "LinkedSet::rotate(", steps, ")");
        linked::rotate(this->view, steps);
    }

    /* Return a new set with elements from this set and all other containers. */
    inline DynamicSet union_() const {
        LOG_CONTEXT(this,  "LinkedSet::union()");
        return as_dynamic();
    }

    /* Return a new set with elements from this set and all other containers. */
    template <typename First, typename... Rest>
    inline DynamicSet union_(First&& first, Rest&&... rest) const {
        LOG_CONTEXT(
            this,  "LinkedSet::union(", repr(first), ", ", "... (+",
            sizeof...(Rest), "))"
        );
        DynamicSet result = linked::union_<false>(
            this->view, std::forward<First>(first)
        );
        if constexpr (sizeof...(Rest) > 0) {
            result.update(std::forward<Rest>(rest)...);
        }
        return result;
    }

    /* Return a new set with elements from this set and all other containers.  Appends
    to the head of the set rather than the tail. */
    inline DynamicSet union_left() const {
        LOG_CONTEXT(this,  "LinkedSet::union_left()");
        return as_dynamic();
    }

    /* Return a new set with elements from this set and all other containers.  Appends
    to the head of the set rather than the tail. */
    template <typename First, typename... Rest>
    inline DynamicSet union_left(First&& first, Rest&&... rest) const {
        LOG_CONTEXT(
            this,  "LinkedSet::union_left(", repr(first), ", ", "... (+",
            sizeof...(Rest), "))"
        );
        DynamicSet result = linked::union_<true>(
            this->view, std::forward<First>(first)
        );
        if constexpr (sizeof...(Rest) > 0) {
            result.update_left(std::forward<Rest>(rest)...);
        }
        return result;
    }

    /* Extend a set by adding elements from one or more iterables that are not already
    present. */
    template <typename... Containers>
    inline void update(Containers&&... items) {
        LOG_CONTEXT(this,  "LinkedSet::update(... (+", sizeof...(Containers), "))");
        (
            linked::update<false>(
                this->view, std::forward<Containers>(items)
            ),
            ...
        );
    }

    /* Extend a set by left-adding elements from one or more iterables that are not
    already present. */
    template <typename... Containers>
    inline void update_left(Containers&&... items) {
        LOG_CONTEXT(
            this,  "LinkedSet::update_left(... (+", sizeof...(Containers), "))"
        );
        (
            linked::update<true>(
                this->view, std::forward<Containers>(items)
            ),
            ...
        );
    }

    /* Extend a set by adding or moving items to the head of the set and possibly
    evicting the tail to make room. */
    template <typename... Containers>
    inline void lru_update(Containers&&... items) {
        LOG_CONTEXT(
            this,  "LinkedSet::lru_update(... (+", sizeof...(Containers), "))"
        );
        (
            linked::lru_update(
                this->view, std::forward<Containers>(items)
            ),
            ...
        );
    }

    /* Return a new set with elements common to this set and all other containers. */
    inline DynamicSet intersection() const {
        LOG_CONTEXT(this,  "LinkedSet::intersection()");
        return as_dynamic();
    }

    /* Return a new set with elements common to this set and all other containers. */
    template <typename First, typename... Rest>
    inline DynamicSet intersection(First&& first, Rest&&... rest) const {
        LOG_CONTEXT(
            this,  "LinkedSet::intersection(", repr(first), ", ", "... (+",
            sizeof...(Rest), "))"
        );
        DynamicSet result = linked::intersection(
            this->view, std::forward<First>(first)
        );
        if constexpr (sizeof...(Rest) > 0) {
            result.intersection_update(std::forward<Rest>(rest)...);
        }
        return result;
    }

    /* Removal elements from a set that are not contained in one or more iterables. */
    template <typename... Containers>
    inline void intersection_update(Containers&&... items) {
        LOG_CONTEXT(
            this,  "LinkedSet::intersection_update(... (+", sizeof...(Containers), "))"
        );
        (
            linked::intersection_update(
                this->view, std::forward<Containers>(items)
            ),
            ...
        );
    }

    /* Return a new set with elements from this set that are not common to any other
    containers. */
    inline DynamicSet difference() const {
        LOG_CONTEXT(this,  "LinkedSet::difference()");
        return as_dynamic();
    }

    /* Return a new set with elements from this set that are not common to any other
    containers. */
    template <typename First, typename... Rest>
    inline DynamicSet difference(First&& first, Rest&&... rest) const {
        LOG_CONTEXT(
            this,  "LinkedSet::difference(", repr(first), ", ", "... (+",
            sizeof...(Rest), "))"
        );
        DynamicSet result = linked::difference(
            this->view, std::forward<First>(first)
        );
        if constexpr (sizeof...(Rest) > 0) {
            result.difference_update(std::forward<Rest>(rest)...);
        }
        return result;
    }

    /* Remove elements from a set that are contained in one or more iterables. */
    template <typename... Containers>
    inline void difference_update(Containers&&... items) {
        LOG_CONTEXT(
            this,  "LinkedSet::difference_update(... (+", sizeof...(Containers), "))"
        );
        (
            linked::difference_update(
                this->view, std::forward<Containers>(items)
            ),
            ...
        );
    }

    /* Return a new set with elements in either this set or another container, but not
    both. */
    template <typename Container>
    inline DynamicSet symmetric_difference(Container&& items) const {
        LOG_CONTEXT(
            this,  "LinkedSet::symmetric_difference(", repr(items), ")"
        );
        return DynamicSet(
            linked::symmetric_difference<false>(
                this->view, std::forward<Container>(items)
            )
        );
    }

    /* Return a new set with elements in either this set or another container, but not
    both.  Appends to the head of the set rather than the tail. */
    template <typename Container>
    inline DynamicSet symmetric_difference_left(Container&& items) const {
        LOG_CONTEXT(
            this,  "LinkedSet::symmetric_difference_left(", repr(items), ")"
        );
        return DynamicSet(
            linked::symmetric_difference<true>(
                this->view, std::forward<Container>(items)
            )
        );
    }

    /* Update a set, keeping only elements found in either the set or the given
    container, but not both. */
    template <typename Container>
    inline void symmetric_difference_update(Container&& items) {
        LOG_CONTEXT(
            this,  "LinkedSet::symmetric_difference_update(", repr(items), ")"
        );
        linked::symmetric_difference_update<false>(
            this->view, std::forward<Container>(items)
        );
    }

    /* Update a set, keeping only elements found in either the set or the given
    container, but not both.  Appends to the head of the set rather than the tail. */
    template <typename Container>
    inline void symmetric_difference_update_left(Container&& items) {
        LOG_CONTEXT(
            this,  "LinkedSet::symmetric_difference_update_left(", repr(items), ")"
        );
        linked::symmetric_difference_update<true>(
            this->view, std::forward<Container>(items)
        );
    }

    /* Check whether the set has no elements in common with another container. */
    template <typename Container>
    inline bool isdisjoint(Container&& items) const {
        LOG_CONTEXT(this,  "LinkedSet::isdisjoint(", repr(items), ")");
        return linked::isdisjoint(
            this->view, std::forward<Container>(items)
        );
    }

    /* Check whether all items within the set are also present in another container. */
    template <typename Container>
    inline bool issubset(Container&& items) const {
        LOG_CONTEXT(this,  "LinkedSet::issubset(", repr(items), ")");
        return linked::issubset<false>(
            this->view, std::forward<Container>(items)
        );
    }

    /* Check whether the set contains all items within another container. */
    template <typename Container>
    inline bool issuperset(Container&& items) const {
        LOG_CONTEXT(this,  "LinkedSet::issuperset(", repr(items), ")");
        return linked::issuperset<false>(
            this->view, std::forward<Container>(items)
        );
    }

    /* Get the linear distance between two elements within the set. */
    inline long long distance(const Key& from, const Key& to) const {
        LOG_CONTEXT(this,  "LinkedSet::distance(", repr(from), ", ", repr(to), ")");
        return linked::distance(this->view, from, to);
    }

    /* Swap the positions of two elements within the set. */
    inline void swap(const Key& key1, const Key& key2) {
        LOG_CONTEXT(this,  "LinkedSet::swap(", repr(key1), ", ", repr(key2), ")");
        linked::swap(this->view, key1, key2);
    }

    /* Move a key within the set by the specified number of steps. */
    inline void move(const Key& key, long long steps) {
        LOG_CONTEXT(this,  "LinkedSet::move(", repr(key), ", ", steps, ")");
        linked::move(this->view, key, steps);
    }

    /* Move a key within the set to the specified index. */
    inline void move_to_index(const Key& key, long long index) {
        LOG_CONTEXT(this,  "LinkedSet::move_to_index(", repr(key), ", ", index, ")");
        linked::move_to_index(this->view, key, index);
    }

    ///////////////////////
    ////    PROXIES    ////
    ///////////////////////

    /* Proxies allow access to a particular element or slice of a set, allowing
     * convenient, Python-like syntax for set operations.
     *
     * Since LinkedSets are fundamentally ordered, they implement the same positional
     * proxies as LinkedLists.  This means that set elements can be accessed using
     * integer indices and slices, which is not possible for Python sets or
     * std::unordered_set.  Insertions are guaranteed never to break the set invariants,
     * and will throw errors otherwise.  See structs/linked/list.h for more information.
     */

    inline auto position(long long index)
        -> linked::ElementProxy<View, Yield::KEY>
    {
        LOG_CONTEXT(this,  "LinkedSet::position(", index, ")");
        return linked::position<Yield::KEY>(this->view, index);
    }

    inline auto position(long long index) const
        -> const linked::ElementProxy<const View, Yield::KEY>
    {
        LOG_CONTEXT(this,  "LinkedSet::position(", index, ")");
        return linked::position<Yield::KEY>(this->view, index);
    }

    inline linked::SliceProxy<View, DynamicSet, Yield::KEY> slice(
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt,
        std::optional<long long> step = std::nullopt
    ) {
        LOG_CONTEXT(
            this, "LinkedSet::slice(", repr(start), ", ", repr(stop), ", ",
            repr(step), ")"
        );
        return linked::slice<DynamicSet, Yield::KEY>(
            this->view, start, stop, step
        );
    }

    inline linked::SliceProxy<View, DynamicSet, Yield::KEY> slice(
        PyObject* py_slice
    ) {
        LOG_CONTEXT(this, "LinkedSet::slice(", repr(py_slice), ")");
        return linked::slice<DynamicSet, Yield::KEY>(this->view, py_slice);
    }

    inline const linked::SliceProxy<const View, DynamicSet, Yield::KEY> slice(
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt,
        std::optional<long long> step = std::nullopt
    ) const {
        LOG_CONTEXT(
            this, "LinkedSet::slice(", repr(start), ", ", repr(stop), ", ",
            repr(step), ")"
        );
        return linked::slice<DynamicSet, Yield::KEY>(
            this->view, start, stop, step
        );
    }

    inline const linked::SliceProxy<const View, DynamicSet, Yield::KEY> slice(
        PyObject* py_slice
    ) const {
        LOG_CONTEXT(this, "LinkedSet::slice(", repr(py_slice), ")");
        return linked::slice<DynamicSet, Yield::KEY>(this->view, py_slice);
    }

    //////////////////////////////////
    ////    OPERATOR OVERLOADS    ////
    //////////////////////////////////

    /* NOTE: The supported operators are as follows:
     *      (|, |=)     union, union update
     *      (&, &=)     intersection, intersection update
     *      (-, -=)     difference, difference update
     *      (^, ^=)     symmetric difference, symmetric difference update
     *      (<)         proper subset comparison
     *      (<=)        subset comparison
     *      (==)        equality comparison
     *      (!=)        inequality comparison
     *      (>=)        superset comparison
     *      (>)         proper superset comparison
     *      (<<)        string stream representation (equivalent to Python repr())
     *
     * These all work similarly to their Python counterparts except that they can
     * accept any iterable container in either C++ or Python as the other operand.
     * This symmetry is provided by the universal utility functions in
     * structs/util/iter.h and structs/util/ops.h.
     */

    inline auto operator[](long long index) {
        LOG_CONTEXT(this,  "LinkedSet[", index, "]");
        return position(index);
    }

    inline auto operator[](long long index) const {
        LOG_CONTEXT(this,  "LinkedSet[", index, "]");
        return position(index);
    }

    template <typename Container>
    inline DynamicSet operator|(const Container& other) const {
        LOG_CONTEXT(this,  "LinkedSet | ", repr(other));
        return union_(other);
    }

    template <typename Container>
    inline LinkedSet& operator|=(const Container& other) {
        LOG_CONTEXT(this,  "LinkedSet |= ", repr(other));
        update(other);
        return *this;
    }

    template <typename Container>
    inline DynamicSet operator-(const Container& other) const {
        LOG_CONTEXT(this,  "LinkedSet - ", repr(other));
        return difference(other);
    }

    template <typename Container>
    inline LinkedSet& operator-=(const Container& other) {
        LOG_CONTEXT(this,  "LinkedSet -= ", repr(other));
        difference_update(other);
        return *this;
    }

    template <typename Container>
    inline DynamicSet operator&(const Container& other) const {
        LOG_CONTEXT(this,  "LinkedSet & ", repr(other));
        return intersection(other);
    }

    template <typename Container>
    inline LinkedSet& operator&=(const Container& other) {
        LOG_CONTEXT(this,  "LinkedSet &= ", repr(other));
        intersection_update(other);
        return *this;
    }

    template <typename Container>
    inline DynamicSet operator^(const Container& other) const {
        LOG_CONTEXT(this,  "LinkedSet ^ ", repr(other));
        return symmetric_difference(other);
    }

    template <typename Container>
    inline LinkedSet& operator^=(const Container& other) {
        LOG_CONTEXT(this,  "LinkedSet ^= ", repr(other));
        symmetric_difference_update(other);
        return *this;
    }

    template <typename Container>
    inline bool operator<(const Container& other) const {
        LOG_CONTEXT(this,  "LinkedSet < ", repr(other));
        return linked::issubset<true>(this->view, other);
    }

    template <typename Container>
    inline bool operator<=(const Container& other) const {
        LOG_CONTEXT(this,  "LinkedSet <= ", repr(other));
        return linked::issubset<false>(this->view, other);
    }

    template <typename Container>
    inline bool operator==(const Container& other) const {
        LOG_CONTEXT(this,  "LinkedSet == ", repr(other));
        using C = std::remove_cv_t<std::remove_reference_t<Container>>;
        if constexpr (std::is_same_v<C, LinkedSet>) {
            if (this == &other) {
                return true;
            }
        }
        return linked::set_equal<true>(this->view, other);
    }

    template <typename Container>
    inline bool operator!=(const Container& other) const {
        LOG_CONTEXT(this,  "LinkedSet != ", repr(other));
        using C = std::remove_cv_t<std::remove_reference_t<Container>>;
        if constexpr (std::is_same_v<C, LinkedSet>) {
            if (this == &other) {
                return false;
            }
        }
        return linked::set_equal<false>(this->view, other);
    }

    template <typename Container>
    inline bool operator>=(const Container& other) const {
        LOG_CONTEXT(this,  "LinkedSet >= ", repr(other));
        return linked::issuperset<false>(this->view, other);
    }

    template <typename Container>
    inline bool operator>(const Container& other) const {
        LOG_CONTEXT(this,  "LinkedSet > ", repr(other));
        return linked::issuperset<true>(this->view, other);
    }

};


template <typename T, unsigned int Flags, typename... Ts>
inline auto operator<<(std::ostream& stream, const LinkedSet<T, Flags, Ts...>& set)
    -> std::ostream&
{
    LOG_CONTEXT(&set,  "ostream << LinkedSet");
    stream << linked::build_repr(
        set.view,
        "LinkedSet",
        "{",
        "}",
        64
    );
    return stream;
}


//////////////////////////////
////    PYTHON WRAPPER    ////
//////////////////////////////


/* CRTP mixin class containing the public set interface for a linked data structure. */
template <typename Derived>
class PySetInterface {

    template <typename Func, typename Result = PyObject*>
    static Result visit(Derived* self, Func func, Result err_code = nullptr) {
        try {
            return std::visit(func, self->variant);
        } catch (...) {
            throw_python();
            return err_code;
        }
    }

    template <typename Func>
    static auto unwrap_python(PyObject* arg, Func func) {
        if (Derived::typecheck(arg)) {
            Derived* other = reinterpret_cast<Derived*>(arg);
            return std::visit(func, other->variant);
        }
        return func(arg);
    }

public:

    static PyObject* add(Derived* self, PyObject* key) {
        PYLOG_CONTEXT(self, Derived::NAME, "::add(", repr(key), ")");
        return visit(self, [&key](auto& set) {
            set.add(key);
            Py_RETURN_NONE;
        });
    }

    static PyObject* add_left(Derived* self, PyObject* key) {
        PYLOG_CONTEXT(self, Derived::NAME, "::add_left(", repr(key), ")");
        return visit(self, [&key](auto& set) {
            set.add_left(key);
            Py_RETURN_NONE;
        });
    }

    static PyObject* lru_add(Derived* self, PyObject* key) {
        PYLOG_CONTEXT(self, Derived::NAME, "::lru_add(", repr(key), ")");
        return visit(self, [&key](auto& set) {
            set.lru_add(key);
            Py_RETURN_NONE;
        });
    }

    static PyObject* lru_contains(Derived* self, PyObject* key) {
        PYLOG_CONTEXT(self, Derived::NAME, "::lru_contains(", repr(key), ")");
        return visit(self, [&key](auto& set) {
            return Py_NewRef(set.lru_contains(key) ? Py_True : Py_False);
        });
    }

    static PyObject* remove(Derived* self, PyObject* key) {
        PYLOG_CONTEXT(self, Derived::NAME, "::remove(", repr(key), ")");
        return visit(self, [&key](auto& set) {
            set.remove(key);
            Py_RETURN_NONE;
        });
    }

    static PyObject* discard(Derived* self, PyObject* key) {
        PYLOG_CONTEXT(self, Derived::NAME, "::discard(", repr(key), ")");
        return visit(self, [&key](auto& set) {
            set.discard(key);
            Py_RETURN_NONE;
        });
    }

    static PyObject* union_(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        PYLOG_CONTEXT(self, Derived::NAME, "::union(... (+", nargs, "))");
        return visit(self, [&args, &nargs](auto& set) {
            auto result = set.union_();
            auto execute = [&result](auto& other) {
                result.update(other);
            };
            for (Py_ssize_t i = 0; i < nargs; ++i) {
                unwrap_python(args[i], execute);
            }
            return Derived::construct(std::move(result));
        });
    }

    static PyObject* union_left(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        PYLOG_CONTEXT(self, Derived::NAME, "::union_left(... (+", nargs, "))");
        return visit(self, [&args, &nargs](auto& set) {
            auto result = set.union_left();
            auto execute = [&result](auto& other) {
                result.update_left(other);
            };
            for (Py_ssize_t i = 0; i < nargs; ++i) {
                unwrap_python(args[i], execute);
            }
            return Derived::construct(std::move(result));
        });
    }

    static PyObject* update(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        PYLOG_CONTEXT(self, Derived::NAME, "::update(... (+", nargs, "))");
        return visit(self, [&args, &nargs](auto& set) {
            auto execute = [&set](auto& other) {
                set.update(other);
            };
            for (Py_ssize_t i = 0; i < nargs; ++i) {
                unwrap_python(args[i], execute);
            }
            Py_RETURN_NONE;
        });
    }

    static PyObject* update_left(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        PYLOG_CONTEXT(self, Derived::NAME, "::update_left(... (+", nargs, "))");
        return visit(self, [&args, &nargs](auto& set) {
            auto execute = [&set](auto& other) {
                set.update_left(other);
            };
            for (Py_ssize_t i = 0; i < nargs; ++i) {
                unwrap_python(args[i], execute);
            }
            Py_RETURN_NONE;
        });
    }

    static PyObject* lru_update(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        PYLOG_CONTEXT(self, Derived::NAME, "::lru_update(... (+", nargs, "))");
        return visit(self, [&args, &nargs](auto& set) {
            auto execute = [&set](auto& other) {
                set.lru_update(other);
            };
            for (Py_ssize_t i = 0; i < nargs; ++i) {
                unwrap_python(args[i], execute);
            }
            Py_RETURN_NONE;
        });
    }

    static PyObject* difference(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        PYLOG_CONTEXT(self, Derived::NAME, "::difference(... (+", nargs, "))");
        return visit(self, [&args, &nargs](auto& set) {
            auto result = set.difference();
            auto execute = [&result](auto& other) {
                result.difference_update(other);
            };
            for (Py_ssize_t i = 0; i < nargs; ++i) {
                unwrap_python(args[i], execute);
            }
            return Derived::construct(std::move(result));
        });
    }

    static PyObject* difference_update(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs
    ) {
        PYLOG_CONTEXT(self, Derived::NAME, "::difference_update(... (+", nargs, "))");
        return visit(self, [&args, &nargs](auto& set) {
            auto execute = [&set](auto& other) {
                set.difference_update(other);
            };
            for (Py_ssize_t i = 0; i < nargs; ++i) {
                unwrap_python(args[i], execute);
            }
            Py_RETURN_NONE;
        });
    }

    static PyObject* intersection(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs
    ) {
        PYLOG_CONTEXT(self, Derived::NAME, "::intersection(... (+", nargs, "))");
        return visit(self, [&args, &nargs](auto& set) {
            auto result = set.intersection();
            auto execute = [&result](auto& other) {
                result.intersection_update(other);
            };
            for (Py_ssize_t i = 0; i < nargs; ++i) {
                unwrap_python(args[i], execute);
            }
            return Derived::construct(std::move(result));
        });
    }

    static PyObject* intersection_update(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs
    ) {
        PYLOG_CONTEXT(self, Derived::NAME, "::intersection_update(... (+", nargs, "))");
        return visit(self, [&args, &nargs](auto& set) {
            auto execute = [&set](auto& other) {
                set.intersection_update(other);
            };
            for (Py_ssize_t i = 0; i < nargs; ++i) {
                unwrap_python(args[i], execute);
            }
            Py_RETURN_NONE;
        });
    }

    static PyObject* symmetric_difference(Derived* self, PyObject* items) {
        PYLOG_CONTEXT(
            self,  Derived::NAME, "::symmetric_difference(", repr(items), ")"
        );
        return visit(self, [&items](auto& set) {
            return unwrap_python(items, [&set](auto& other) {
                return Derived::construct(set.symmetric_difference_left(other));
            });
        });
    }

    static PyObject* symmetric_difference_left(Derived* self, PyObject* items) {
        PYLOG_CONTEXT(
            self,  Derived::NAME, "::symmetric_difference_left(", repr(items), ")"
        );
        return visit(self, [&items](auto& set) {
            return unwrap_python(items, [&set](auto& other) {
                return Derived::construct(set.symmetric_difference_left(other));
            });
        });
    }

    static PyObject* symmetric_difference_update(Derived* self, PyObject* items) {
        PYLOG_CONTEXT(
            self, Derived::NAME, "::symmetric_difference_update(", repr(items), ")"
        );
        return visit(self, [&items](auto& set) {
            unwrap_python(items, [&set](auto& other) {
                set.symmetric_difference_update(other);
            });
            Py_RETURN_NONE;
        });
    }

    static PyObject* symmetric_difference_update_left(Derived* self, PyObject* items) {
        PYLOG_CONTEXT(
            self, Derived::NAME, "::symmetric_difference_update_left(", repr(items), ")"
        );
        return visit(self, [&items](auto& set) {
            unwrap_python(items, [&set](auto& other) {
                set.symmetric_difference_update_left(other);
            });
            Py_RETURN_NONE;
        });
    }

    static PyObject* isdisjoint(Derived* self, PyObject* other) {
        PYLOG_CONTEXT(self, Derived::NAME, "::isdisjoint(", repr(other), ")");
        return visit(self, [&other](auto& set) {
            return unwrap_python(other, [&set](auto& other) {
                return Py_NewRef(set.isdisjoint(other) ? Py_True : Py_False);
            });
        });
    }

    static PyObject* issubset(Derived* self, PyObject* other) {
        PYLOG_CONTEXT(self, Derived::NAME, "::issubset(", repr(other), ")");
        return visit(self, [&other](auto& set) {
            return unwrap_python(other, [&set](auto& other) {
                return Py_NewRef(set.issubset(other) ? Py_True : Py_False);
            });
        });
    }

    static PyObject* issuperset(Derived* self, PyObject* other) {
        PYLOG_CONTEXT(self, Derived::NAME, "::issuperset(", repr(other), ")");
        return visit(self, [&other](auto& set) {
            return unwrap_python(other, [&set](auto& other) {
                return Py_NewRef(set.issuperset(other) ? Py_True : Py_False);
            });
        });
    }

    static PyObject* distance(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"distance"};

        return visit(self, [&self, &args, &nargs](auto& set) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key1 = pyargs.parse("key1");
            PyObject* key2 = pyargs.parse("key2");
            pyargs.finalize();

            PYLOG_CONTEXT(
                self, Derived::NAME, "::distance(", repr(key1), ", ", repr(key2), ")"
            );
            return PyLong_FromLongLong(set.distance(key1, key2));
        });
    }

    static PyObject* swap(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"swap"};

        return visit(self, [&self, &args, &nargs](auto& set) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key1 = pyargs.parse("key1");
            PyObject* key2 = pyargs.parse("key2");
            pyargs.finalize();

            PYLOG_CONTEXT(
                self, Derived::NAME, "::swap(", repr(key1), ", ", repr(key2), ")"
            );
            set.swap(key1, key2);
            Py_RETURN_NONE;
        });
    }

    static PyObject* move(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"move"};
        using bertrand::util::parse_int;

        return visit(self, [&self, &args, &nargs](auto& set) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            long long steps = pyargs.parse("steps", parse_int);
            pyargs.finalize();

            PYLOG_CONTEXT(
                self, Derived::NAME, "::move(", repr(key), ", ", steps, ")"
            );
            set.move(key, steps);
            Py_RETURN_NONE;
        });
    }

    static PyObject* move_to_index(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs
    ) {
        static constexpr std::string_view meth_name{"move_to_index"};
        using bertrand::util::parse_int;

        return visit(self, [&self, &args, &nargs](auto& set) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            long long index = pyargs.parse("index", parse_int);
            pyargs.finalize();

            PYLOG_CONTEXT(
                self, Derived::NAME, "::move_to_index(", repr(key), ", ", index, ")"
            );
            set.move_to_index(key, index);
            Py_RETURN_NONE;
        });
    }

    static PyObject* __or__(Derived* self, PyObject* other) {
        PYLOG_CONTEXT(self, Derived::NAME, " | ", repr(other));
        return visit(self, [&other](auto& set) {
            return unwrap_python(other, [&set](auto& other) {
                return Derived::construct(set | other);
            });
        });
    }

    static PyObject* __ior__(Derived* self, PyObject* other) {
        PYLOG_CONTEXT(self, Derived::NAME, " |= ", repr(other));
        return visit(self, [&self, &other](auto& set) {
            unwrap_python(other, [&set](auto& other) {
                set |= other;
            });
            return Py_NewRef(reinterpret_cast<PyObject*>(self));
        });
    }

    static PyObject* __sub__(Derived* self, PyObject* other) {
        PYLOG_CONTEXT(self, Derived::NAME, " - ", repr(other));
        return visit(self, [&other](auto& set) {
            return unwrap_python(other, [&set](auto& other) {
                return Derived::construct(set - other);
            });
        });
    }

    static PyObject* __isub__(Derived* self, PyObject* other) {
        PYLOG_CONTEXT(self, Derived::NAME, " -= ", repr(other));
        return visit(self, [&self, &other](auto& set) {
            unwrap_python(other, [&set](auto& other) {
                set -= other;
            });
            return Py_NewRef(reinterpret_cast<PyObject*>(self));
        });
    }

    static PyObject* __and__(Derived* self, PyObject* other) {
        PYLOG_CONTEXT(self, Derived::NAME, " & ", repr(other));
        return visit(self, [&other](auto& set) {
            return unwrap_python(other, [&set](auto& other) {
                return Derived::construct(set & other);
            });
        });
    }

    static PyObject* __iand__(Derived* self, PyObject* other) {
        PYLOG_CONTEXT(self, Derived::NAME, " &= ", repr(other));
        return visit(self, [&self, &other](auto& set) {
            unwrap_python(other, [&set](auto& other) {
                set &= other;
            });
            return Py_NewRef(reinterpret_cast<PyObject*>(self));
        });
    }

    static PyObject* __xor__(Derived* self, PyObject* other) {
        PYLOG_CONTEXT(self, Derived::NAME, " ^ ", repr(other));
        return visit(self, [&other](auto& set) {
            return unwrap_python(other, [&set](auto& other) {
                return Derived::construct(set ^ other);
            });
        });
    }

    static PyObject* __ixor__(Derived* self, PyObject* other) {
        PYLOG_CONTEXT(self, Derived::NAME, " ^= ", repr(other));
        return visit(self, [&self, &other](auto& set) {
            unwrap_python(other, [&set](auto& other) {
                set ^= other;
            });
            return Py_NewRef(reinterpret_cast<PyObject*>(self));
        });
    }

protected:

    struct docs {

        static constexpr std::string_view add {R"doc(
Insert a key at the end of the set if it is not already present.

Parameters
----------
key : Any
    The key to insert.

Notes
-----
Adds are O(1) for both ends of the set.
)doc"
        };

        static constexpr std::string_view add_left {R"doc(
Insert a key at the beginning of the set if it is not already present.

Parameters
----------
key : Any
    The key to insert.

Notes
-----
This method is analogous to the ``appendleft()`` method of a
:class:`collections.deque` object, except that only appends the key if it is
not already contained within the set.

Adds are O(1) for both ends of the set.
)doc"
        };

        static constexpr std::string_view lru_add {R"doc(
Insert a key at the front of the set if it is not present, or move it there
if it is.  Evicts the last key if the set is of fixed size and already full.

Parameters
----------
key : Any
    The key to move/insert.

Notes
-----
This method is roughly equivalent to:

.. code-block:: python

    set.add(key, left=True)
    set.move(key, 0)

except that it avoids repeated lookups and evicts the last key if the set is
already full.  The linked nature of the data structure makes this extremely
efficient, allowing the set to act as a fast LRU cache, particularly if it is
doubly-linked.

LRU adds are O(1) for doubly-linked sets and O(n) for singly-linked ones.
)doc"
        };

        static constexpr std::string_view remove {R"doc(
Remove an item from the set.

Parameters
----------
key : Any
    The key to remove.

Raises
------
KeyError
    If the key is not present in the set.

Notes
-----
Removals are O(1) for doubly-linked sets and O(n) for singly-linked ones.  This
is due to the need to traverse the entire set in order to find the previous
node.
)doc"
        };

        static constexpr std::string_view discard {R"doc(
Remove a key from the set if it is present.

Parameters
----------
key : Any
    The key to remove.

Notes
-----
Discards are O(1) for doubly-linked sets and O(n) for singly-linked ones.  This
is due to the need to traverse the entire set in order to find the previous
node.
)doc"
        };

        static constexpr std::string_view lru_contains {R"doc(
Search the set for a key, moving it to the front if it is present.

Parameters
----------
key : Any
    The key to search for.

Returns
-------
bool
    True if the key is present in the set.  False otherwise.

Notes
-----
This method is equivalent to ``key in set`` except that it also moves the key
to the front of the set if it is found.  The linked nature of the data
structure makes this extremely efficient, allowing the set to act as a fast
LRU cache, particularly if it is doubly-linked.

LRU searches are O(1) for doubly-linked sets and O(n) for singly-linked ones.
)doc"
        };

        static constexpr std::string_view union_ {R"doc(
Return a new set with the merged contents of this set and all other containers.

Parameters
----------
*others : Iterable[Any]
    An arbitrary number of containers passed to this method as positional
    arguments.  Must contain at least one element.

Returns
-------
LinkedSet
    A new set containing the union of all the given containers.

Notes
-----
Unions are O(sum(m_n)), where m_n is the length of each of the containers
passed to this method.
)doc"
        };

        static constexpr std::string_view union_left {R"doc(
Return a new set with the merged contents of this set and all other containers.

Parameters
----------
*others : Iterable[Any]
    An arbitrary number of containers passed to this method as positional
    arguments.  Must contain at least one element.

Returns
-------
LinkedSet
    A new set containing the union of all the given containers.

Notes
-----
This method appends new items to the beginning of the set instead of the end.

Unions are O(sum(m_n)), where m_n is the length of each of the containers
passed to this method.
)doc"
        };

        static constexpr std::string_view update {R"doc(
Update a set in-place, merging the contents of all other containers.

Parameters
----------
*others : Iterable[Any]
    An arbitrary number of containers passed to this method as positional
    arguments.

Notes
-----
Updates are O(sum(m_n)), where m_n is the length of each of the containers
passed to this method.
)doc"
        };

        static constexpr std::string_view update_left {R"doc(
Update a set in-place, merging the contents of all other containers.

Parameters
----------
*others : Iterable[Any]
    An arbitrary number of containers passed to this method as positional
    arguments.

Notes
-----
Updates are O(sum(m_n)), where m_n is the length of each of the containers
passed to this method.
)doc"
        };

        static constexpr std::string_view lru_update {R"doc(
Update a set in-place, merging the contents of all other containers according
to the LRU caching strategy.

Parameters
----------
*others : Iterable[Any]
    An arbitrary number of containers passed to this method as positional
    arguments.

Notes
-----
This method is roughly equivalent to:

.. code-block:: python

    set.update(*others)
    for container in others:
        for key in container:
            set.add(key)
            set.move(key, 0)

Except that it avoids repeated lookups, collapses the loops, and evicts the
last key if the set is already full.  The linked nature of the data structure
makes this extremely efficient, allowing the set to act as a fast LRU cache,
particularly if it is doubly-linked.

Updates are O(sum(m_n)), where m_n is the length of each of the containers
passed to this method.
)doc"
        };

        static constexpr std::string_view difference {R"doc(
Return a new set with the contents of all other containers removed.

Parameters
----------
*others : Iterable[Any]
    An arbitrary number of containers passed to this method as positional
    arguments.  Must contain at least one element.

Returns
-------
LinkedSet
    A new set containing the difference between the original set and all of the
    given containers.
)doc"
        };

        static constexpr std::string_view difference_update {R"doc(
Update a set in-place, removing the contents of all other containers.

Parameters
----------
*others : Iterable[Any]
    An arbitrary number of containers passed to this method as positional
    arguments.  Must contain at least one element.
)doc"
        };

        static constexpr std::string_view intersection {R"doc(
Return a new set containing the shared contents of all other containers.

Parameters
----------
*others : Iterable[Any]
    An arbitrary number of containers passed to this method as positional
    arguments.  Must contain at least one element.

Returns
-------
LinkedSet
    A new set containing only those elements that are shared between the
    original set and all of the given containers.
)doc"
        };

        static constexpr std::string_view intersection_update {R"doc(
Update a set in-place, removing any elements that are not stored within the
other containers.

Parameters
----------
*others : Iterable[Any]
    An arbitrary number of containers passed to this method as positional
    arguments.  Must contain at least one element.
)doc"
        };

        static constexpr std::string_view symmetric_difference {R"doc(
Return a new set containing the elements within this set or the given
container, but not both.

Parameters
----------
other : Iterable[Any]
    Another container to compare against.

Returns
-------
LinkedSet
    A new set containing only those elements that are not shared between the
    original set and the given container.

Notes
-----
Symmetric differences are O(2n), where n is the length of ``other``.
)doc"
        };

        static constexpr std::string_view symmetric_difference_left {R"doc(
Return a new set containing the elements within this set or the given
container, but not both.

Parameters
----------
other : Iterable[Any]
    Another container to compare against.

Returns
-------
LinkedSet
    A new set containing only those elements that are not shared between the
    original set and the given container.

Notes
-----
This method appends new items to the beginning of the set instead of the end.

Symmetric differences are O(2n), where n is the length of ``other``.
)doc"
        };

        static constexpr std::string_view symmetric_difference_update {R"doc(
Update a set in-place, removing any elements that are stored within the given
container and adding any that are missing.

Parameters
----------
other : Iterable[Any]
    Another container to compare against.

Notes
-----
Symmetric updates are O(2n) for doubly-linked sets and O(3n) for singly-linked
ones, where n is the length of ``other``.
)doc"
        };

        static constexpr std::string_view symmetric_difference_update_left {R"doc(
Update a set in-place, removing any elements that are stored within the given
container and adding any that are missing.

Parameters
----------
other : Iterable[Any]
    Another container to compare against.

Notes
-----
This method appends new items to the beginning of the set instead of the end.

Symmetric updates are O(2n) for doubly-linked sets and O(3n) for singly-linked
ones, where n is the length of ``other``.
)doc"
        };

        static constexpr std::string_view isdisjoint {R"doc(
Check if the set contains no elements in common with another container.

Parameters
----------
other : Iterable[Any]
    Another container to compare against.

Returns
-------
bool
    True if the set has no elements in common with ``other``.  False otherwise.
)doc"
        };

        static constexpr std::string_view issubset {R"doc(
Check if all elements of the set are contained within another container.

Parameters
----------
other : Iterable[Any]
    Another container to compare against.

Returns
-------
bool
    True if all elements of the set are contained within ``other``.  False
    otherwise.

Notes
-----
This is equivalent to ``set <= other``.
)doc"
        };

        static constexpr std::string_view issuperset {R"doc(
Check if the set contains all the elements of another container.

Parameters
----------
other : Iterable[Any]
    Another container to compare against.

Returns
-------
bool
    True if all elements of ``other`` are contained within the set. False
    otherwise.

Notes
-----
This is equivalent to ``set >= other``.
)doc"
        };

        static constexpr std::string_view distance {R"doc(
Get the linear distance between two keys in the set.

Parameters
----------
key1 : Any
    The first key.
key2 : Any
    The second key.

Returns
-------
int
    The difference between the indices of the two keys.  Positive values
    indicate that ``key1`` is to the left of ``key2``, while negative values
    indicate the opposite.  If the keys are the same, this will be 0.

Raises
------
KeyError
    If either key is not present in the set.

Notes
-----
This method is equivalent to ``set.index(key2) - set.index(key1)``, except
that it gathers both indices in a single iteration.

Distance calculations are O(n).
)doc"
        };

        static constexpr std::string_view swap {R"doc(
Swap the positions of two keys within the set.

Parameters
----------
key1 : Any
    The first key.
key2 : Any
    The second key.

Raises
------
KeyError
    If either key is not present in the set.

Notes
-----
This method is O(1) for doubly-linked sets and O(n) otherwise.  This is due to
the need to traverse the entire set in order to find the previous node.
)doc"
        };

        static constexpr std::string_view move {R"doc(
Move a key within the set by the specified number of spaces.

Parameters
----------
key : Any
    The key to move.
steps : int
    The number of spaces to move the key.  If positive, the key will be moved
    forward.  If negative, the key will be moved backward.

Raises
------
KeyError
    If the key is not present in the set.

Notes
-----
This method is O(steps) for doubly-linked sets and singly-linked sets with
``steps > 0``.  For singly-linked sets with ``steps < 0``, it is O(n) due to
the need to traverse the entire set in order to find the previous node.
)doc"
        };

        static constexpr std::string_view move_to_index {R"doc(
Move a key within the set to the specified index.

Parameters
----------
key : Any
    The key to move.
index : int
    The index to move the key to, following the same semantics as the normal
    index operator.

Raises
------
KeyError
    If the key is not present in the set.

Notes
-----
This method is O(n) due to the need to traverse the entire set in order to find
the given index.  For doubly-linked sets, it is optimized to O(n/2).
)doc"
        };

    };

};


/* A Python type that exposes a discriminated union of C++ LinkedSets to the Python
interpreter. */
class PyLinkedSet :
    public PyLinkedBase<PyLinkedSet>,
    public PyListInterface<PyLinkedSet>,
    public PySetInterface<PyLinkedSet>
{
    using Base = PyLinkedBase<PyLinkedSet>;
    using IList = PyListInterface<PyLinkedSet>;
    using ISet = PySetInterface<PyLinkedSet>;

    /* A std::variant representing all the LinkedSet implementations that are
    constructable from Python. */
    template <unsigned int Flags>
    using SetConfig = linked::LinkedSet<PyObject*, Flags, BasicLock>;
    using Variant = std::variant<
        SetConfig<Config::DEFAULT>,
        SetConfig<Config::STRICTLY_TYPED>,
        SetConfig<Config::FIXED_SIZE>,
        SetConfig<Config::FIXED_SIZE | Config::STRICTLY_TYPED>,
        SetConfig<Config::SINGLY_LINKED>,
        SetConfig<Config::SINGLY_LINKED | Config::STRICTLY_TYPED>,
        SetConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE>,
        SetConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::STRICTLY_TYPED>
    >;
    template <size_t I>
    using Alt = typename std::variant_alternative_t<I, Variant>;

    friend Base;
    friend IList;
    friend ISet;
    Variant variant;

    /* Construct a PyLinkedSet around an existing C++ LinkedSet. */
    template <typename Set>
    inline void from_cpp(Set&& set) {
        new (&variant) Variant(std::forward<Set>(set));
    }

    /* Parse the configuration code and initialize the variant with the forwarded
    arguments. */
    template <typename... Args>
    static void build_variant(unsigned int code, PyLinkedSet* self, Args&&... args) {
        switch (code) {
            case (Config::DEFAULT):
                self->from_cpp(Alt<0>(std::forward<Args>(args)...));
                break;
            case (Config::STRICTLY_TYPED):
                self->from_cpp(Alt<1>(std::forward<Args>(args)...));
                break;
            case (Config::FIXED_SIZE):
                self->from_cpp(Alt<2>(std::forward<Args>(args)...));
                break;
            case (Config::FIXED_SIZE | Config::STRICTLY_TYPED):
                self->from_cpp(Alt<3>(std::forward<Args>(args)...));
                break;
            case (Config::SINGLY_LINKED):
                self->from_cpp(Alt<4>(std::forward<Args>(args)...));
                break;
            case (Config::SINGLY_LINKED | Config::STRICTLY_TYPED):
                self->from_cpp(Alt<5>(std::forward<Args>(args)...));
                break;
            case (Config::SINGLY_LINKED | Config::FIXED_SIZE):
                self->from_cpp(Alt<6>(std::forward<Args>(args)...));
                break;
            case (Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::STRICTLY_TYPED):
                self->from_cpp(Alt<7>(std::forward<Args>(args)...));
                break;
            default:
                throw ValueError("invalid argument configuration");
        }
    }

    /* Construct a PyLinkedSet from scratch using the given constructor arguments. */
    static void initialize(
        PyLinkedSet* self,
        PyObject* iterable,
        std::optional<size_t> max_size,
        PyObject* spec,
        bool reverse,
        bool singly_linked,
        bool strictly_typed
    ) {
        unsigned int code = (
            Config::SINGLY_LINKED * singly_linked |
            Config::FIXED_SIZE * max_size.has_value() |
            Config::STRICTLY_TYPED * strictly_typed
        );
        if (iterable == nullptr) {
            build_variant(code, self, max_size, spec);
        } else {
            build_variant(code, self, iterable, max_size, spec, reverse);
        }
    }

public:
    static constexpr std::string_view NAME{"PyLinkedSet"};  // used in logging

    static int __init__(PyLinkedSet* self, PyObject* args, PyObject* kwargs) {
        static constexpr std::string_view meth_name{"__init__"};
        using bertrand::util::none_to_null;
        using bertrand::util::parse_int;
        using bertrand::util::is_truthy;

        try {
            PyArgs<CallProtocol::KWARGS> pyargs(meth_name, args, kwargs);
            PyObject* keys = pyargs.parse(
                "keys", none_to_null, (PyObject*)nullptr
            );
            std::optional<size_t> max_size = pyargs.parse(
                "max_size",
                [](PyObject* obj) -> std::optional<size_t> {
                    if (obj == Py_None) {
                        return std::nullopt;
                    }
                    long long result = parse_int(obj);
                    if (result < 0) {
                        throw ValueError("max_size cannot be negative");
                    }
                    return std::make_optional(static_cast<size_t>(result));
                },
                std::optional<size_t>()
            );
            PyObject* spec = pyargs.parse("spec", none_to_null, (PyObject*) nullptr);
            bool reverse = pyargs.parse("reverse", is_truthy, false);
            bool singly_linked = pyargs.parse("singly_linked", is_truthy, false);
            pyargs.finalize();

            PYLOG_CONTEXT(
                self, "PyLinkedSet(", repr(keys), ", ", repr(max_size), ", ",
                repr(spec), ", ", reverse, ", ", singly_linked, ")"
            );

            initialize(
                self, keys, max_size, spec, reverse, singly_linked, false
            );

            return 0;

        } catch (...) {
            throw_python();
            return -1;
        }
    }

    static PyObject* __str__(PyLinkedSet* self) {
        PYLOG_CONTEXT(self, "str(PyLinkedSet)");
        return Base::visit(self, [](auto& set) {
            std::ostringstream stream;
            stream << "{";
            auto it = set.begin();
            auto end = set.end();
            if (it != end) {
                stream << repr(*it);
                ++it;
            }
            while (it != set.end()) {
                stream << ", " << repr(*it);
                ++it;
            }
            stream << "}";
            auto str = stream.str();
            return PyUnicode_FromStringAndSize(str.c_str(), str.size());
        });
    }

private:

    struct docs {

        static constexpr std::string_view LinkedSet {R"doc(
A modular, ordered set based on a linked list available in both Python and C++.

This class is a drop-in replacement for a built-in :class:`set`, supporting all
the same operations, plus those of the built-in :class:`list` and some extras
leveraging the ordered nature of the set.  It is also available as a C++ type
under the same name, with identical semantics.

Parameters
----------
keys : Iterable[Any], optional
    The keys to initialize the set with.  If not specified, the set will be
    empty.
max_size : int, optional
    The maximum number of keys that the set can hold.  If not specified, the
    set will be unbounded.
spec : Any, optional
    A specific type to enforce for elements of the set, allowing the creation
    of type-safe containers.  This can be in any format recognized by
    :func:`isinstance() <python:isinstance>`.  The default is ``None``, which
    disables type checking for the set.  See the :meth:`specialize()` method
    for more details.
reverse : bool, default False
    If True, reverse the order of ``keys`` during set construction.  This is
    more efficient than calling :meth:`reverse()` after construction.
singly_linked : bool, default False
    If True, use a singly-linked set instead of a doubly-linked one.  This
    trades some performance in certain operations for increased memory
    efficiency.  Regardless of this setting, the set will still support all
    the same operations.

Notes
-----
These data structures are highly optimized, and offer performance that is
generally on par with the built-in :class:`set` type.  They have slightly more
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
        std::vector<int> keys = {1, 2, 3, 4, 5};
        bertrand::LinkedSet<int> set(items);

        set.add(6);
        set.update(std::vector{7, 8, 9});
        int x = set.pop();
        set.rotate(4);
        set[0] = x;
        for (int i : set) {
            // ...
        }

        std::cout << set;  // LinkedSet({9, 6, 7, 8, 1, 2, 3, 4})
        return 0;
    }

This makes it significantly easier to port code that relies on this data
structure between the two languages.  In fact, doing so provides significant
benefits, allowing users to take advantage of static C++ types that completely
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

    inline static PyGetSetDef properties[] = {
        BASE_PROPERTY(SINGLY_LINKED),
        BASE_PROPERTY(DOUBLY_LINKED),
        BASE_PROPERTY(FIXED_SIZE),
        BASE_PROPERTY(DYNAMIC),
        BASE_PROPERTY(STRICTLY_TYPED),
        BASE_PROPERTY(LOOSELY_TYPED),
        BASE_PROPERTY(lock),
        BASE_PROPERTY(capacity),
        BASE_PROPERTY(max_size),
        BASE_PROPERTY(frozen),
        BASE_PROPERTY(nbytes),
        BASE_PROPERTY(specialization),
        {NULL}
    };

    inline static PyMethodDef methods[] = {
        BASE_METHOD(reserve, METH_FASTCALL),
        BASE_METHOD(defragment, METH_NOARGS),
        BASE_METHOD(specialize, METH_O),
        BASE_METHOD(__reversed__, METH_NOARGS),
        BASE_METHOD(__class_getitem__, METH_CLASS | METH_O),
        LIST_METHOD(insert, METH_FASTCALL),
        LIST_METHOD(index, METH_FASTCALL),
        LIST_METHOD(count, METH_FASTCALL),
        LIST_METHOD(pop, METH_FASTCALL),
        LIST_METHOD(clear, METH_NOARGS),
        LIST_METHOD(copy, METH_NOARGS),
        LIST_METHOD(sort, METH_FASTCALL | METH_KEYWORDS),
        LIST_METHOD(reverse, METH_NOARGS),
        LIST_METHOD(rotate, METH_FASTCALL),
        SET_METHOD(add, METH_O),
        SET_METHOD(add_left, METH_O),
        SET_METHOD(lru_add, METH_O),
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
        {NULL}
    };

    #undef BASE_PROPERTY
    #undef BASE_METHOD
    #undef LIST_METHOD
    #undef SET_METHOD

    inline static PyMappingMethods mapping = [] {
        PyMappingMethods slots;
        slots.mp_length = (lenfunc) Base::__len__;
        slots.mp_subscript = (binaryfunc) IList::__getitem__;
        slots.mp_ass_subscript = (objobjargproc) IList::__setitem__;
        return slots;
    }();

    inline static PySequenceMethods sequence = [] {
        PySequenceMethods slots;
        slots.sq_length = (lenfunc) Base::__len__;
        slots.sq_item = (ssizeargfunc) IList::__getitem_scalar__;
        slots.sq_ass_item = (ssizeobjargproc) IList::__setitem_scalar__;
        slots.sq_contains = (objobjproc) IList::__contains__;
        return slots;
    }();

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

    static PyTypeObject build_type() {
        return {
            .ob_base = PyObject_HEAD_INIT(NULL)
            .tp_name = "bertrand.LinkedSet",
            .tp_basicsize = sizeof(PyLinkedSet),
            .tp_itemsize = 0,
            .tp_dealloc = (destructor) Base::__dealloc__,
            .tp_repr = (reprfunc) Base::__repr__,
            .tp_as_number = &number,
            .tp_as_sequence = &sequence,
            .tp_as_mapping = &mapping,
            .tp_hash = (hashfunc) PyObject_HashNotImplemented,
            .tp_str = (reprfunc) __str__,
            .tp_flags = (
                Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC |
                Py_TPFLAGS_IMMUTABLETYPE | Py_TPFLAGS_SEQUENCE
                // add Py_TPFLAGS_MANAGED_WEAKREF for Python 3.12+
            ),
            .tp_doc = PyDoc_STR(docs::LinkedSet.data()),
            .tp_traverse = (traverseproc) Base::__traverse__,
            .tp_clear = (inquiry) Base::__clear__,
            .tp_richcompare = (richcmpfunc) IList::__richcompare__,
            .tp_iter = (getiterfunc) Base::__iter__,
            .tp_methods = methods,
            .tp_getset = properties,
            .tp_init = (initproc) __init__,
            .tp_alloc = (allocfunc) PyType_GenericAlloc,
            .tp_new = (newfunc) PyType_GenericNew,
            .tp_free = (freefunc) PyObject_GC_Del,
        };
    };

public:

    inline static PyTypeObject Type = build_type();

    template <typename Set>
    static PyObject* construct(Set&& set) {
        PyLinkedSet* result = reinterpret_cast<PyLinkedSet*>(
            Type.tp_new(&Type, nullptr, nullptr)
        );
        if (result == nullptr) {
            return nullptr;
        }

        try {
            result->from_cpp(std::forward<Set>(set));
            return reinterpret_cast<PyObject*>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    static bool typecheck(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &Type);
        if (result == -1) {
            throw catch_python();
        }
        return static_cast<bool>(result);
    }

};


}  // namespace linked


using linked::LinkedSet;
using linked::PyLinkedSet;


}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_SET_H
