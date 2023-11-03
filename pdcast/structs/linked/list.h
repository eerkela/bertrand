// include guard: BERTRAND_STRUCTS_LINKED_LIST_H
#ifndef BERTRAND_STRUCTS_LINKED_LIST_H
#define BERTRAND_STRUCTS_LINKED_LIST_H

#include <sstream>  // std::ostringstream
#include "../util/except.h"  // type_error()
#include "core/view.h"  // ListView
#include "base.h"  // LinkedBase

#include "algorithms/add.h"
#include "algorithms/append.h"
#include "algorithms/contains.h"
#include "algorithms/count.h"
#include "algorithms/extend.h"
#include "algorithms/index.h"
#include "algorithms/insert.h"
#include "algorithms/lexical_compare.h"
#include "algorithms/pop.h"
#include "algorithms/position.h"
#include "algorithms/remove.h"
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

    /* Construct an empty list. */
    LinkedList(
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr
    ) : Base(max_size, spec)
    {}

    /* Construct a list from an input iterable. */
    LinkedList(
        PyObject* iterable,
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr,
        bool reverse = false
    ) : Base(iterable, max_size, spec, reverse)
    {}

    /* Construct a list from a base view. */
    LinkedList(View&& view) : Base(std::move(view)) {}

    /* Copy constructor. */
    LinkedList(const LinkedList& other) : Base(other.view) {}

    /* Move constructor. */
    LinkedList(LinkedList&& other) : Base(std::move(other.view)) {}

    /* Copy assignment operator. */
    LinkedList& operator=(const LinkedList& other) {
        Base::operator=(other);
        return *this;
    }

    /* Move assignment operator. */
    LinkedList& operator=(LinkedList&& other) {
        Base::operator=(std::move(other));
        return *this;
    }

    //////////////////////////////
    ////    LIST INTERFACE    ////
    //////////////////////////////

    /* LinkedLists implement the full Python list interface with equivalent semantics
     * to the built-in Python list type, as well as a few addons from
     * `collections.deque`.  There are only a few differences:
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

    /* Append an item to the end of a list. */
    inline void append(Value& item, bool left = false) {
        linked::append(this->view, item, left);
    }

    /* Insert an item into a list at the specified index. */
    template <typename Index>
    inline void insert(Index index, Value& item) {
        linked::insert(this->view, index, item);
    }

    /* Extend a list by appending elements from the iterable. */
    template <typename Container>
    inline void extend(Container& items, bool left = false) {
        linked::extend(this->view, items, left);
    }

    /* Get the index of an item within a list. */
    template <typename Index>
    inline size_t index(Value& item, Index start = 0, Index stop = -1) const {
        return linked::index(this->view, item, start, stop);
    }

    /* Count the number of occurrences of an item within a list. */
    template <typename Index>
    inline size_t count(Value& item, Index start = 0, Index stop = -1) const {
        return linked::count(this->view, item, start, stop);
    }

    /* Check if the list contains a certain item. */
    inline bool contains(Value& item) const {
        return linked::contains(this->view, item);
    }

    /* Remove the first occurrence of an item from a list. */
    inline void remove(Value& item) {
        linked::remove(this->view, item);
    }

    /* Remove an item from a list and return its value. */
    template <typename Index>
    inline Value pop(Index index) {
        return linked::pop(this->view, index);
    }

    /* Remove all elements from a list. */
    inline void clear() {
        this->view.clear();
    }

    /* Return a shallow copy of the list. */
    inline LinkedList copy() const {
        return LinkedList(this->view.copy());
    }

    /* Sort a list in-place. */
    template <typename Func>
    inline void sort(Func key = nullptr, bool reverse = false) {
        linked::sort<SortPolicy>(this->view, key, reverse);
    }

    /* Reverse a list in-place. */
    inline void reverse() {
        linked::reverse(this->view);
    }

    /* Rotate a list to the right by the specified number of steps. */
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
     *      void set(Value value): set the value at the current index.
     *      void del(): delete the value at the current index.
     *      void insert(Value value): insert a value at the current index.
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
     *      (+) for concatenating the elements of one container to another
     *      (*) for repeating the elements of a container a specified number of times
     *      (<) for lexicographic less-than comparison between containers
     *      (<=) for lexicographic less-than-or-equal-to comparison between containers
     *      (==) for lexicographic equality comparison between containers
     *      (!=) for lexicographic inequality comparison between containers
     *      (>=) for lexicographic greater-than-or-equal-to comparison between containers
     *      (>) for lexicographic greater-than comparison between containers
     *
     * These all work similarly to their Python equivalents except that they can accept
     * any iterable container in either C++ or Python to compare against.  This
     * symmetry is provided by the universal utility functions in structs/util/iter.h
     * and structs/util/python.h.
     */

};


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


// /////////////////////////////
// ////    CONCATENATION    ////
// /////////////////////////////


// /* Allow Python-style concatenation between Linked data structures and arbitrary
// Python/C++ containers. */
// template <typename T, typename Derived>
// inline auto operator+(const Derived& lhs, const T& rhs)
//     -> std::enable_if_t<Concatenateable<Derived>::enable, Derived>
// {
//     std::optional<Derived> result = lhs.copy();
//     if (!result.has_value()) {
//         throw std::runtime_error("could not copy list");
//     }
//     result.value().extend(rhs);  // must be specialized for T
//     return Derived(std::move(result.value()));
// }


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


// /* Allow Python-style concatenation between Python sequences and Linked data
// structures. */
// template <typename T, typename Derived, bool Enable = Concatenateable<Derived>::enable>
// inline auto operator+(const PyObject* lhs, const Derived& rhs)
//     -> std::enable_if_t<Enable, PyObject*>
// {
//     // Check that lhs is a Python sequence
//     if (!PySequence_Check(lhs)) {
//         std::ostringstream msg;
//         msg << "can only concatenate sequence (not '";
//         msg << lhs->ob_type->tp_name << "') to sequence";
//         throw util::type_error(msg.str());
//     }

//     // unpack list into Python sequence
//     PyObject* seq = PySequence_List(rhs.iter.python());  // new ref
//     if (seq == nullptr) {
//         return nullptr;  // propagate error
//     }

//     // concatenate using Python API
//     PyObject* concat = PySequence_Concat(lhs, seq);
//     Py_DECREF(seq);
//     return concat;
// }


// /* Allow in-place concatenation for Linked data structures using the += operator. */
// template <typename T, typename Derived>
// inline auto operator+=(Derived& lhs, const T& rhs)
//     -> std::enable_if_t<Concatenateable<Derived>::enable, Derived&>
// {
//     lhs.extend(rhs);  // must be specialized for T
//     return lhs;
// }


// //////////////////////////
// ////    REPETITION    ////
// //////////////////////////


// // TODO: we could probably optimize repetition by allocating a contiguous block of
// // nodes equal to list.size() * rhs.  We could also remove the extra copy in *= by
// // using an iterator to the end of the list and reusing it for each iteration.


// /* Allow Python-style repetition for Linked data structures using the * operator. */
// template <typename = void, typename Derived>
// auto operator*(const Derived& lhs, const ssize_t rhs)
//     -> std::enable_if_t<Repeatable<Derived>::enable, Derived>
// {
//     // handle empty repitition
//     if (rhs <= 0 || lhs.size() == 0) {
//         return Derived(lhs.max_size(), lhs.specialization());
//     }

//     // copy lhs
//     std::optional<Derived> result = lhs.copy();
//     if (!result.has_value()) {
//         throw std::runtime_error("could not copy list");
//     }

//     // extend copy rhs - 1 times
//     for (ssize_t i = 1; i < rhs; ++i) {
//         result.value().extend(lhs);
//     }

//     // move result into return value
//     return Derived(std::move(result.value()));
// }


// /* Allow Python-style repetition for Linked data structures using the * operator. */
// template <typename = void, typename Derived>
// inline auto operator*(const ssize_t lhs, const Derived& rhs)
//     -> std::enable_if_t<Repeatable<Derived>::enable, Derived>
// {
//     return rhs * lhs;  // symmetric
// }


// /* Allow Python-style repetition for Linked data structures using the * operator. */
// template <typename = void, typename Derived>
// auto operator*(const Derived& lhs, const PyObject* rhs)
//     -> std::enable_if_t<Repeatable<Derived>::enable, Derived>
// {
//     // Check that rhs is a Python integer
//     if (!PyLong_Check(rhs)) {
//         std::ostringstream msg;
//         msg << "can't multiply sequence by non-int of type '";
//         msg << rhs->ob_type->tp_name << "'";
//         throw util::type_error(msg.str());
//     }

//     // convert to C++ integer
//     ssize_t val = PyLong_AsSsize_t(rhs);
//     if (val == -1 && PyErr_Occurred()) {
//         throw util::catch_python<util::type_error>();
//     }

//     // delegate to C++ overload
//     return lhs * val;
// }


// /* Allow Python-style repetition for Linked data structures using the * operator. */
// template <typename = void, typename Derived>
// inline auto operator*(const PyObject* lhs, const Derived& rhs)
//     -> std::enable_if_t<Repeatable<Derived>::enable, Derived>
// {
//     return rhs * lhs;  // symmetric
// }


// /* Allow in-place repetition for Linked data structures using the *= operator. */
// template <typename = void, typename Derived>
// auto operator*=(Derived& lhs, const ssize_t rhs)
//     -> std::enable_if_t<Repeatable<Derived>::enable, Derived&>
// {
//     // handle empty repitition
//     if (rhs <= 0 || lhs.size() == 0) {
//         lhs.clear();
//         return lhs;
//     }

//     // copy lhs
//     std::optional<Derived> copy = lhs.copy();
//     if (!copy.has_value()) {
//         throw std::runtime_error("could not copy list");
//     }

//     // extend lhs rhs - 1 times
//     for (ssize_t i = 1; i < rhs; ++i) {
//         lhs.extend(copy.value());
//     }
//     return lhs;
// }


// /* Allow in-place repetition for Linked data structures using the *= operator. */
// template <typename = void, typename Derived>
// inline auto operator*=(Derived& lhs, const PyObject* rhs)
//     -> std::enable_if_t<Repeatable<Derived>::enable, Derived&>
// {
//     // Check that rhs is a Python integer
//     if (!PyLong_Check(rhs)) {
//         std::ostringstream msg;
//         msg << "can't multiply sequence by non-int of type '";
//         msg << rhs->ob_type->tp_name << "'";
//         throw util::type_error(msg.str());
//     }

//     // convert to C++ integer
//     ssize_t val = PyLong_AsSsize_t(rhs);
//     if (val == -1 && PyErr_Occurred()) {
//         throw util::catch_python<util::type_error>();
//     }

//     // delegate to C++ overload
//     return lhs *= val;
// }


}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_LIST_H
