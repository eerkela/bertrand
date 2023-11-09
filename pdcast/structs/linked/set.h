// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_SET_H
#define BERTRAND_STRUCTS_SET_H

#include <cstddef>  // size_t
#include <Python.h>  // CPython API
#include "core/view.h"  // SetView
#include "base.h"  // LinkedBase

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
// #include "algorithms/relative.h"
#include "algorithms/remove.h"
#include "algorithms/reverse.h"
#include "algorithms/rotate.h"
#include "algorithms/set_compare.h"
#include "algorithms/slice.h"
#include "algorithms/sort.h"
#include "algorithms/union.h"
#include "algorithms/update.h"


namespace bertrand {
namespace structs {
namespace linked {


/* A module linked set class that mimics the Python set interface in C++. */
template <
    typename NodeType,
    typename SortPolicy = linked::MergeSort,
    typename LockPolicy = util::BasicLock
>
class LinkedSet : public LinkedBase<linked::SetView<NodeType>, LockPolicy> {
    using Base = LinkedBase<linked::SetView<NodeType>, LockPolicy>;

public:
    using View = linked::SetView<NodeType>;
    using Node = typename View::Node;
    using Value = typename View::Value;

    template <linked::Direction dir>
    using Iterator = typename View::template Iterator<dir>;
    template <linked::Direction dir>
    using ConstIterator = typename View::template ConstIterator<dir>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    // inherit constructors from LinkedBase
    using Base::Base;
    using Base::operator=;

    /////////////////////////////
    ////    SET INTERFACE    ////
    /////////////////////////////

    /* LinkedSets implement the full Python set interface with equivalent semantics to
     * the built-in Python set type, as well as a few addons from `collections.deque`.
     * There are only a few differences:
     *
     *      1.  The add() and update() methods accept a second boolean argument that
     *          signals whether the item(s) should be inserted at the beginning of the
     *          list or at the end.  This is similar to the appendleft() and
     *          extendleft() methods of `collections.deque`.
     *      TODO
     */

    /* Add an item to the set if it is not already present. */
    inline void add(Value& item, bool left = false) {
        linked::add(this->view, item, left);
    }

    /* Insert an item at a specific index of the set. */
    inline void insert(size_t index, Value& item) {
        linked::insert(this->view, index, item);
    }

    /* Extend a set by adding elements from an iterable that are not already present. */
    template <typename Container>
    inline void update(Container& items, bool left = false) {
        linked::update(this->view, items, left);
    }

    /* Get the index of an item within the set. */
    template <typename Index>
    inline size_t index(Value& item, Index start = 0, Index stop = -1) const {
        return linked::index(this->view, item, start, stop);
    }

    /* Count the number of occurrences of an item within the set. */
    template <typename Index>
    inline size_t count(Value& item, Index start = 0, Index stop = -1) const {
        return linked::count(this->view, item, start, stop);
    }

    /* Remove an item from the set. */
    inline void remove(Value& item) {
        linked::remove(this->view, item);
    }

    /* Remove an item from the set if it is present. */
    inline void discard(Value& item) {
        linked::discard(this->view, item);
    }

    /* Remove an item from the set and return its value. */
    template <typename Index>
    inline Value pop(Index index = -1) {
        return linked::pop(this->view, index);
    }

    /* Remove all elements from the set. */
    inline void clear() {
        this->view.clear();
    }

    /* Return a shallow copy of the set. */
    inline LinkedSet copy() const {
        return LinkedSet(this->view.copy());
    }

    /* Sort the set in-place according to an optional key func. */
    template <typename Func>
    inline void sort(Func key = nullptr, bool reverse = false) {
        linked::sort<SortPolicy>(this->view, key, reverse);
    }

    /* Reverse the order of elements in the set in-place. */
    inline void reverse() {
        linked::reverse(this->view);
    }

    /* Shift all elements in the set to the right by the specified number of steps. */
    inline void rotate(int steps = 1) {
        linked::rotate(this->view, steps);
    }

    /* Get the linear distance between two elements within the set. */
    inline long long distance(Value& from, Value& to) const {
        return linked::distance(this->view, from, to);
    }

    /* Swap the positions of two elements within the set. */
    inline void swap(Value& item1, Value& item2) {
        linked::swap(this->view, item1, item2);
    }

    /* Move an item within the set by the specified number of steps. */
    template <typename Steps>
    inline void move(Value& item, Steps steps) {
        linked::move(this->view, item, steps);
    }

    /* Move an item within the set to the specified index. */
    template <typename Index>
    inline void move_to_index(Value& item, Index index) {
        linked::move_to_index(this->view, item, index);
    }

    /* Check whether the set has no elements in common with another container. */
    template <typename Container>
    inline bool isdisjoint(const Container& other) const {
        return linked::isdisjoint(this->view, other);
    }

    /* Check whether all items within the set are also present in another container. */
    template <typename Container>
    inline bool issubset(const Container& other) const {
        return linked::issubset(this->view, other);
    }

    /* Check whether the set contains all items within another container. */
    template <typename Container>
    inline bool issuperset(const Container& other) const {
        return linked::issuperset(this->view, other);
    }

    /* Return a new set with elements from this set and all other container(s). */
    template <typename... Containers>
    inline LinkedSet set_union(const Containers&&... others) const {
        return LinkedSet(linked::set_union(this->view, others...));
    }

    /* Return a new set with elements from this set that are common to all other
    container(s).  */
    template <typename... Containers>
    inline LinkedSet intersection(const Containers&&... others) const {
        return LinkedSet(linked::intersection(this->view, others...));
    }

    /* Return a new set with elements in this set that are not in the other
    container(s). */
    template <typename... Containers>
    inline LinkedSet difference(const Containers&&... others) const {
        return LinkedSet(linked::difference(this->view, others...));
    }

    /* Return a new set with elements in either this set or another container, but not
    both. */
    template <typename... Containers>
    inline LinkedSet symmetric_difference(const Containers&&... others) const {
        return LinkedSet(linked::symmetric_difference(this->view, others...));
    }

    ///////////////////////
    ////    PROXIES    ////
    ///////////////////////

    /* Proxies allow access to a particular element or slice of a set, allowing
     * convenient, Python-like syntax for set operations.
     *
     * ElementProxies are returned by the array index operator [] when given with a
     * single numeric argument.  This argument can be negative following the same
     * semantics as built-in Python lists (i.e. -1 refers to the last element, and
     * overflow results in an error).  Each proxy offers the following methods:
     *
     *      Value get(): return the value at the current index.
     *      void set(Value& value): set the value at the current index.
     *      void del(): delete the value at the current index.
     *      void insert(Value& value): insert a value at the current index.
     *      Value pop(): remove the value at the current index and return it.
     *      operator Value(): implicitly coerce the proxy to its value in function
     *          calls and other contexts.
     *      operator=(Value& value): set the value at the current index using
     *          assignment syntax.
     *
     * SliceProxies are returned by the `slice()` factory method, which can accept
     * either a Python slice object or separate start, stop, and step arguments, each
     * of which are optional, and can be negative following the same semantics as
     * above.  Each proxy exposes the following methods:
     *
     *      LinkedSet get(): return a new set containing the contents of the slice.
     *      void set(PyObject* items): overwrite the contents of the slice with the
     *          contents of the iterable.
     *      void del(): remove the slice from the set.
     *      Iterator iter(): return a coupled iterator over the slice.
     *          NOTE: slice iterators may not yield results in the same order as the
     *          step size would indicate.  This is because slices are traversed in
     *          such a way as to minimize the number of nodes that must be visited and
     *          avoid backtracking.  See linked/algorithms/slice.h for more details.
     *      Iterator begin():  return an iterator to the first element of the slice.
     *          See note above.
     *      Iterator end(): return an iterator to terminate the slice.
     *
     * RelativeProxies are returned by the `relative()` factory method, which accepts
     * any value within the set.  The value is searched and used as a reference point
     * for relative indexing.  The resulting proxies can then be used to manipulate the
     * set locally around the sentinel value, which can be faster than operating on the
     * whole set at once.  For instance, a relative insertion can be O(1), whereas a
     * whole-set insertion is O(n).  Each proxy exposes the following methods:
     *
     *      TODO
     */


    //////////////////////////////////
    ////    OPERATOR OVERLOADS    ////
    //////////////////////////////////

    /* NOTE: operators are implemented as non-member functions for commutativity.
     * Namely, the supported operators are as follows:
     *      (|)     union
     *      (&)     intersection
     *      (-)     difference
     *      (^)     symmetric difference
     *      (<)     proper subset comparison
     *      (<=)    subset comparison
     *      (==)    equality comparison
     *      (!=)    inequality comparison
     *      (>=)    superset comparison
     *      (>)     proper superset comparison
     *
     * These all work similarly to their Python equivalents except that they can accept
     * any iterable container in either C++ or Python to compare against.  This
     * symmetry is provided by the universal utility functions in structs/util/iter.h
     * and structs/util/python.h.
     */

};


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_SET_H include guard
