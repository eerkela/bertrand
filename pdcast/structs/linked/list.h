// include guard: BERTRAND_STRUCTS_LINKED_LIST_H
#ifndef BERTRAND_STRUCTS_LINKED_LIST_H
#define BERTRAND_STRUCTS_LINKED_LIST_H

#include <cstddef>  // size_t
#include <Python.h>  // CPython API
#include "core/view.h"  // ListView
#include "base.h"  // LinkedBase

#include "algorithms/add.h"
#include "algorithms/append.h"
#include "algorithms/concatenate.h"
#include "algorithms/contains.h"
#include "algorithms/count.h"
#include "algorithms/extend.h"
#include "algorithms/index.h"
#include "algorithms/insert.h"
#include "algorithms/lexical_compare.h"
#include "algorithms/pop.h"
#include "algorithms/position.h"
#include "algorithms/remove.h"
#include "algorithms/repeat.h"
#include "algorithms/reverse.h"
#include "algorithms/rotate.h"
#include "algorithms/slice.h"
#include "algorithms/sort.h"


namespace bertrand {
namespace structs {


/* A modular linked list class that mimics the Python list interface in C++. */
template <
    typename NodeType,
    typename SortPolicy = linked::MergeSort,
    typename LockPolicy = util::BasicLock
>
class LinkedList : public LinkedBase<linked::ListView<NodeType>, LockPolicy> {
    using Base = LinkedBase<linked::ListView<NodeType>, LockPolicy>;

public:
    using View = linked::ListView<NodeType>;
    using Node = typename View::Node;
    using Value = typename Node::Value;

    template <linked::Direction dir>
    using Iterator = typename View::template Iterator<dir>;
    template <linked::Direction dir>
    using ConstIterator = typename View::template ConstIterator<dir>;

    // TODO: type aliases for doubly_linked, etc.

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    // inherit constructors from LinkedBase
    using Base::Base;
    using Base::operator=;

    //////////////////////////////
    ////    LIST INTERFACE    ////
    //////////////////////////////

    /* LinkedLists implement the full Python list interface with equivalent semantics to
     * the built-in Python list type, as well as a few addons from `collections.deque`.
     * There are only a few differences:
     *
     *      1.  The append() and extend() methods accept a second boolean argument that
     *          signals whether the item(s) should be inserted at the beginning of the
     *          list or at the end.  This is similar to the appendleft() and
     *          extendleft() methods of `collections.deque`.
     *      2.  The count() method accepts optional `start` and `stop` arguments that
     *          specify a slice of the list to search within.  This is similar to the
     *          index() method of the built-in Python list.
     *      3.  LinkedLists are able to store non-Python C++ types, but only when
     *          declared from C++ code.  LinkedLists are available from Python, but can
     *          only store Python objects (i.e. PyObject*) when declared from a Python
     *          context.
     *
     * Otherwise, everything should behave exactly as expected, with similar overall
     * performance to a built-in Python list (random access limitations of linked lists
     * notwithstanding.)
     */

    /* Add an item to the end of the list. */
    inline void append(Value& item, bool left = false) {
        linked::append(this->view, item, left);
    }

    // TODO: insert() does not correctly handle negative indices
    // >>> l
    // LinkedList(['a', 'b', 'c', 'd', 'e', 'f', 'g'])
    // >>> l.insert(-1, "x")
    //     -> create: 'x'
    // >>> l
    // LinkedList(['a', 'b', 'c', 'd', 'e', 'f', 'g', 'x'])

    // should be LinkedList(['a', 'b', 'c', 'd', 'e', 'f', 'x', 'g'])

    // ALSO:
    // >>> l
    // LinkedList(['a', 'b', 'c', 'd', 'e', 'f', 'g', 'x'])
    // >>> l.insert(-1, "y")
    //     -> allocate: 16 nodes
    //     -> deallocate: 8 nodes
    //     -> create: 'y'
    // >>> l
    // LinkedList(['a', 'b', 'c', 'd', 'e', 'f', 'g', 'x'])

    // 'y' does not appear in the list (possibly related to slicing bug on resizes)
    // -> could be that the iterator is being invalidated by resize.  If so, this could
    // be a headache.  We might want to totally decouple convenience methods from
    // iterators in general.

    /* Insert an item into the list at the specified index. */
    template <typename Index>
    inline void insert(Index index, Value& item) {
        linked::insert(this->view, index, item);
    }

    /* Extend the list by appending elements from an iterable. */
    template <typename Container>
    inline void extend(Container& items, bool left = false) {
        linked::extend(this->view, items, left);
    }

    /* Get the index of an item within the list. */
    template <typename Index>
    inline size_t index(Value& item, Index start = 0, Index stop = -1) const {
        return linked::index(this->view, item, start, stop);
    }

    /* Count the number of occurrences of an item within the list. */
    template <typename Index>
    inline size_t count(Value& item, Index start = 0, Index stop = -1) const {
        return linked::count(this->view, item, start, stop);
    }

    /* Check if the list contains a certain item. */
    inline bool contains(Value& item) const {
        return linked::contains(this->view, item);
    }

    /* Remove the first occurrence of an item from the list. */
    inline void remove(Value& item) {
        linked::remove(this->view, item);
    }

    /* Remove an item from the list and return its value. */
    template <typename Index>
    inline Value pop(Index index = -1) {
        return linked::pop(this->view, index);
    }

    /* Remove all elements from the list. */
    inline void clear() {
        this->view.clear();
    }

    /* Return a shallow copy of the list. */
    inline LinkedList copy() const {
        return LinkedList(this->view.copy());
    }

    /* Sort the list in-place according to an optional key func. */
    template <typename Func>
    inline void sort(Func key = nullptr, bool reverse = false) {
        linked::sort<SortPolicy>(this->view, key, reverse);
    }

    /* Reverse the order of elements in the list in-place. */
    inline void reverse() {
        linked::reverse(this->view);
    }

    /* Shift all elements in the list to the right by the specified number of steps. */
    inline void rotate(long long steps = 1) {
        linked::rotate(this->view, steps);
    }

    ///////////////////////
    ////    PROXIES    ////
    ///////////////////////

    /* Proxies allow access to a particular element or slice of a list, allowing
     * convenient, Python-like syntax for list operations. 
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
     *      LinkedList get(): return a new list containing the contents of the slice.
     *      void set(PyObject* items): overwrite the contents of the slice with the
     *          contents of the iterable.
     *      void del(): remove the slice from the list.
     *      Iterator iter(): return a coupled iterator over the slice.
     *          NOTE: slice iterators may not yield results in the same order as the
     *          step size would indicate.  This is because slices are traversed in
     *          such a way as to minimize the number of nodes that must be visited and
     *          avoid backtracking.  See linked/algorithms/slice.h for more details.
     *      Iterator begin():  return an iterator to the first element of the slice.
     *          See note above.
     *      Iterator end(): return an iterator to terminate the slice.
     */

    /* Get a proxy for a value at a particular index of the list. */
    template <typename Index>
    inline linked::ElementProxy<View> operator[](Index index) {
        return linked::position(this->view, index);
    }

    /* Get a proxy for a slice within the list. */
    template <typename... Args>
    inline linked::SliceProxy<LinkedList> slice(Args&&... args) {
        return linked::slice(*this, std::forward<Args>(args)...);
    }

    //////////////////////////////////
    ////    OPERATOR OVERLOADS    ////
    //////////////////////////////////

    /* NOTE: operators are implemented as non-member functions for commutativity.
     * Namely, the supported operators are as follows:
     *      (+)     concatenation
     *      (*)     repetition
     *      (<)     lexicographic less-than comparison
     *      (<=)    lexicographic less-than-or-equal-to comparison
     *      (==)    lexicographic equality comparison
     *      (!=)    lexicographic inequality comparison
     *      (>=)    lexicographic greater-than-or-equal-to comparison
     *      (>)     lexicographic greater-than comparison
     *
     * These all work similarly to their Python equivalents except that they can accept
     * any iterable container in either C++ or Python to compare against.  This
     * symmetry is provided by the universal utility functions in structs/util/iter.h
     * and structs/util/python.h.
     */

};


/////////////////////////////
////    CONCATENATION    ////
/////////////////////////////


/* Concatenate a LinkedList with an arbitrary C++/Python container to produce a new
list. */
template <typename Container, typename... Ts>
LinkedList<Ts...> operator+(const LinkedList<Ts...>& lhs, const Container& rhs) {
    return linked::concatenate(lhs.view, rhs);
}


/* Concatenate a LinkedList with an arbitrary C++/Python container to produce a new
list (reversed). */
template <typename Container, typename... Ts>
LinkedList<Ts...> operator+(const Container& lhs, const LinkedList<Ts...>& rhs) {
    return linked::concatenate(lhs, rhs.view);
}


/* Concatenate a LinkedList with an arbitrary C++/Python container in-place. */
template <typename Container, typename... Ts>
LinkedList<Ts...>& operator+=(LinkedList<Ts...>& lhs, const Container& rhs) {
    linked::extend(lhs.view, rhs, false);
    return lhs;
}


// /* Allow Python-style concatenation between list-like C++ containers and Linked data
// structures. */
// template <typename T, typename Derived>
// inline auto operator+(const T& lhs, const Derived& rhs)
//     -> std::enable_if_t<
//         // first, check that T is a list-like container with a range-based insert
//         // method that returns an iterator.  This is true for all STL containers.
//         std::is_same_v<
//             decltype(
//                 std::declval<T>().insert(
//                     std::declval<T>().end(),
//                     std::declval<Derived>().begin(),
//                     std::declval<Derived>().end()
//                 )
//             ),
//             typename T::iterator
//         >,
//         // next, check that Derived inherits from Concatenateable
//         std::enable_if_t<Concatenateable<Derived>::enable, T>
//     >
// {
//     T result = lhs;
//     result.insert(result.end(), rhs.begin(), rhs.end());  // STL compliant
//     return result;
// }


//////////////////////////
////    REPETITION    ////
//////////////////////////


/* Repeat the elements of a LinkedList the specified number of times. */
template <typename Integer, typename... Ts>
inline LinkedList<Ts...> operator*(const LinkedList<Ts...>& list, const Integer rhs) {
    return linked::repeat(list.view, rhs);
}


/* Repeat the elements of a LinkedList the specified number of times (reversed). */
template <typename Integer, typename... Ts>
inline LinkedList<Ts...> operator*(const Integer lhs, const LinkedList<Ts...>& list) {
    return linked::repeat(list.view, lhs);
}


/* Repeat the elements of a LinkedList in-place the specified number of times. */
template <typename Integer, typename... Ts>
inline LinkedList<Ts...>& operator*=(LinkedList<Ts...>& list, const Integer rhs) {
    linked::repeat_inplace(list.view, rhs);
    return list;
}


////////////////////////////////////////
////    LEXICOGRAPHIC COMPARISON    ////
////////////////////////////////////////


/* Apply a lexicographic `<` comparison between the elements of a LinkedList and
another container.  */
template <typename Container, typename... Ts>
inline bool operator<(const LinkedList<Ts...>& lhs, const Container& rhs) {
    return linked::lexical_lt(lhs, rhs);
}


/* Apply a lexicographic `<` comparison between the elements of a LinkedList and
another container (reversed).  */
template <typename Container, typename... Ts>
inline bool operator<(const Container& lhs, const LinkedList<Ts...>& rhs) {
    return linked::lexical_lt(lhs, rhs);
}


/* Apply a lexicographic `<=` comparison between the elements of a LinkedList and
another container.  */
template <typename Container, typename... Ts>
inline bool operator<=(const LinkedList<Ts...>& lhs, const Container& rhs) {
    return linked::lexical_le(lhs, rhs);
}


/* Apply a lexicographic `<=` comparison between the elements of a LinkedList and
another container (reversed).  */
template <typename Container, typename... Ts>
inline bool operator<=(const Container& lhs, const LinkedList<Ts...>& rhs) {
    return linked::lexical_lt(lhs, rhs);
}


/* Apply a lexicographic `==` comparison between the elements of a LinkedList and
another container.  */
template <typename Container, typename... Ts>
inline bool operator==(const LinkedList<Ts...>& lhs, const Container& rhs) {
    return linked::lexical_eq(lhs, rhs);
}


/* Apply a lexicographic `==` comparison between the elements of a LinkedList and
another container (reversed).  */
template <typename Container, typename... Ts>
inline bool operator==(const Container& lhs, const LinkedList<Ts...>& rhs) {
    return linked::lexical_eq(lhs, rhs);
}


/* Apply a lexicographic `!=` comparison between the elements of a LinkedList and
another container.  */
template <typename Container, typename... Ts>
inline bool operator!=(const LinkedList<Ts...>& lhs, const Container& rhs) {
    return !linked::lexical_eq(lhs, rhs);
}


/* Apply a lexicographic `!=` comparison between the elements of a LinkedList and
another container (reversed).  */
template <typename Container, typename... Ts>
inline bool operator!=(const Container& lhs, const LinkedList<Ts...>& rhs) {
    return !linked::lexical_eq(lhs, rhs);
}


/* Apply a lexicographic `>=` comparison between the elements of a LinkedList and
another container.  */
template <typename Container, typename... Ts>
inline bool operator>=(const LinkedList<Ts...>& lhs, const Container& rhs) {
    return linked::lexical_ge(lhs, rhs);
}


/* Apply a lexicographic `>=` comparison between the elements of a LinkedList and
another container (reversed).  */
template <typename Container, typename... Ts>
inline bool operator>=(const Container& lhs, const LinkedList<Ts...>& rhs) {
    return linked::lexical_ge(lhs, rhs);
}


/* Apply a lexicographic `>` comparison between the elements of a LinkedList and
another container.  */
template <typename Container, typename... Ts>
inline bool operator>(const LinkedList<Ts...>& lhs, const Container& rhs) {
    return linked::lexical_gt(lhs, rhs);
}


/* Apply a lexicographic `>` comparison between the elements of a LinkedList and
another container (reversed).  */
template <typename Container, typename... Ts>
inline bool operator>(const Container& lhs, const LinkedList<Ts...>& rhs) {
    return linked::lexical_gt(lhs, rhs);
}


}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_LIST_H
