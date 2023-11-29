// include guard: BERTRAND_STRUCTS_LINKED_SET_H
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
#include "core/allocate.h"
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


/* Apply default config flags for C++ LinkedLists. */
static constexpr unsigned int set_defaults(unsigned int flags) {
    unsigned int result = flags;
    if (!(result & (Config::DOUBLY_LINKED | Config::SINGLY_LINKED | Config::XOR))) {
        result |= Config::DOUBLY_LINKED;  // default to doubly-linked
    }
    if (!(result & (Config::DYNAMIC | Config::FIXED_SIZE))) {
        result |= Config::DYNAMIC;  // default to dynamic allocator
    }
    return result;
}


/* An ordered set based on a combined linked list and hash table. */
template <
    typename Key,
    unsigned int Flags = Config::DEFAULT,
    typename Lock = util::BasicLock
>
class LinkedSet : public LinkedBase<
    linked::SetView<NodeSelect<Key, set_defaults(Flags)>, set_defaults(Flags)>,
    Lock
> {
    using Base = LinkedBase<
        linked::SetView<NodeSelect<Key, set_defaults(Flags)>, set_defaults(Flags)>,
        Lock
    >;

public:
    using View = typename Base::View;
    using Node = typename Base::Node;
    using Value = typename Base::Value;

    template <linked::Direction dir>
    using Iterator = typename Base::template Iterator<dir>;
    template <linked::Direction dir>
    using ConstIterator = typename Base::template ConstIterator<dir>;

    /* Get a variation of this type with a different set of configuration flags. */
    template <unsigned int NewFlags>
    using Reconfigure = LinkedSet<Key, NewFlags, Lock>;

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

    /* Add an item to the end of the set if it is not already present. */
    inline void add(const Value& item) {
        linked::add(this->view, item);
    }

    /* Add an item to the beginning of the set if it is not already present. */
    inline void add_left(const Value& item) {
        linked::add_left(this->view, item);
    }

    /* Add an item to the set if it is not already present and move it to the front of
    the set, evicting the last element to make room if necessary. */
    inline void lru_add(const Value& item) {
        linked::lru_add(this->view, item);
    }

    /* Insert an item at a specific index of the set. */
    inline void insert(long long index, const Value& item) {
        linked::insert(this->view, index, item);
    }

    /* Extend a set by adding elements from one or more iterables that are not already
    present. */
    template <typename... Containers>
    inline void update(Containers&&... items) {
        (linked::update(this->view, std::forward<Containers>(items)), ...);
    }

    /* Extend a set by left-adding elements from one or more iterables that are not
    already present. */
    template <typename... Containers>
    inline void update_left(Containers&&... items) {
        (linked::update_left(this->view, std::forward<Containers>(items)), ...);
    }

    /* Extend a set by adding or moving items to the head of the set and possibly
    evicting the tail to make room. */
    template <typename... Containers>
    inline void lru_update(Containers&&... items) {
        (linked::lru_update(this->view, std::forward<Containers>(items)), ...);
    }

    /* Remove elements from a set that are contained in one or more iterables. */
    template <typename... Containers>
    inline void difference_update(Containers&&... items) {
        (linked::difference_update(this->view, std::forward<Containers>(items)), ...);
    }

    /* Removal elements from a set that are not contained in one or more iterables. */
    template <typename... Containers>
    inline void intersection_update(Containers&&... items) {
        (linked::intersection_update(this->view, std::forward<Containers>(items)), ...);
    }

    /* Update a set, keeping only elements found in either the set or the given
    container, but not both. */
    template <typename Container>
    inline void symmetric_difference_update(Container&& items) {
        linked::symmetric_difference_update(
            this->view, std::forward<Container>(items)
        );
    }

    /* Update a set, keeping only elements found in either the set or the given
    container, but not both.  Appends to the head of the set rather than the tail. */
    template <typename Container>
    inline void symmetric_difference_update_left(Container&& items) {
        linked::symmetric_difference_update_left(
            this->view, std::forward<Container>(items)
        );
    }

    /* Get the index of an item within the set. */
    inline size_t index(
        const Value& item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return linked::index(this->view, item, start, stop);
    }

    /* Count the number of occurrences of an item within the set. */
    inline size_t count(
        const Value& item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return linked::count(this->view, item, start, stop);
    }

    /* Check if the set contains a certain item. */
    inline bool contains(const Value& item) const {
        return linked::contains(this->view, item);
    }

    /* Check if the set contains a certain item and move it to the front of the set
    if so. */
    inline bool lru_contains(const Value& item) {
        return linked::lru_contains(this->view, item);
    }

    /* Remove an item from the set. */
    inline void remove(const Value& item) {
        linked::remove(this->view, item);
    }

    /* Remove an item from the set if it is present. */
    inline void discard(const Value& item) {
        linked::discard(this->view, item);
    }

    /* Remove an item from the set and return its value. */
    inline Value pop(long long index = -1) {
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
        linked::sort<linked::MergeSort>(this->view, key, reverse);
    }

    /* Reverse the order of elements in the set in-place. */
    inline void reverse() {
        linked::reverse(this->view);
    }

    /* Shift all elements in the set to the right by the specified number of steps. */
    inline void rotate(long long steps = 1) {
        linked::rotate(this->view, steps);
    }

    /* Return a new set with elements from this set and all other containers. */
    template <typename... Containers>
    inline LinkedSet union_(Containers&&... items) const {
        return LinkedSet(
            (linked::union_(this->view, std::forward<Containers>(items)), ...)
        );
    }

    /* Return a new set with elements from this set and all other containers.  Appends
    to the head of the set rather than the tail. */
    template <typename... Containers>
    inline LinkedSet union_left(Containers&&... items) const {
        return LinkedSet(
            (linked::union_left(this->view, std::forward<Containers>(items)), ...)
        );
    }

    /* Return a new set with elements common to this set and all other containers. */
    template <typename... Containers>
    inline LinkedSet intersection(Containers&&... items) const {
        return LinkedSet(
            (linked::intersection(this->view, std::forward<Containers>(items)), ...)
        );
    }

    /* Return a new set with elements from this set that are not common to any other
    containers. */
    template <typename... Containers>
    inline LinkedSet difference(Containers&&... items) const {
        return LinkedSet(
            (linked::difference(this->view, std::forward<Containers>(items)), ...)
        );
    }

    /* Return a new set with elements in either this set or another container, but not
    both. */
    template <typename Container>
    inline LinkedSet symmetric_difference(Container&& items) const {
        return LinkedSet(
            linked::symmetric_difference(
                this->view, std::forward<Container>(items)
            )
        );
    }

    /* Return a new set with elements in either this set or another container, but not
    both.  Appends to the head of the set rather than the tail. */
    template <typename Container>
    inline LinkedSet symmetric_difference_left(Container&& items) const {
        return LinkedSet(
            linked::symmetric_difference_left(
                this->view, std::forward<Container>(items)
            )
        );
    }

    /* Check whether the set has no elements in common with another container. */
    template <typename Container>
    inline bool isdisjoint(Container&& items) const {
        return linked::isdisjoint(this->view, std::forward<Container>(items));
    }

    /* Check whether all items within the set are also present in another container. */
    template <typename Container>
    inline bool issubset(Container&& items) const {
        return linked::issubset(
            this->view, std::forward<Container>(items), false
        );
    }

    /* Check whether the set contains all items within another container. */
    template <typename Container>
    inline bool issuperset(Container&& items) const {
        return linked::issuperset(
            this->view, std::forward<Container>(items), false
        );
    }

    /* Get the linear distance between two elements within the set. */
    inline long long distance(const Value& from, const Value& to) const {
        return linked::distance(this->view, from, to);
    }

    /* Swap the positions of two elements within the set. */
    inline void swap(const Value& item1, const Value& item2) {
        linked::swap(this->view, item1, item2);
    }

    /* Move an item within the set by the specified number of steps. */
    inline void move(const Value& item, long long steps) {
        linked::move(this->view, item, steps);
    }

    /* Move an item within the set to the specified index. */
    inline void move_to_index(Value& item, long long index) {
        linked::move_to_index(this->view, item, index);
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

    /* Get a proxy for a value at a particular index of the set. */
    inline linked::ElementProxy<View> position(long long index) {
        return linked::position(this->view, index);
    }

    /* Get a proxy for a slice within the set. */
    template <typename... Args>
    inline linked::SliceProxy<View, LinkedSet> slice(Args&&... args) {
        return linked::slice<View, LinkedSet>(
            this->view,
            std::forward<Args>(args)...
        );
    }

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

    /* Overload the array index operator ([]) to allow pythonic list indexing. */
    inline auto operator[](long long index) {
        return position(index);
    }

};


/////////////////////////////////////
////    STRING REPRESENTATION    ////
/////////////////////////////////////


/* Override the << operator to print the abbreviated contents of a set to an output
stream (equivalent to Python repr()). */
template <typename T, unsigned int Flags, typename... Ts>
inline std::ostream& operator<<(
    std::ostream& stream,
    const LinkedSet<T, Flags, Ts...>& set
) {
    stream << linked::repr(
        set.view,
        "LinkedSet",
        "{",
        "}",
        64
    );
    return stream;
}


//////////////////////////////
////    SET ARITHMETIC    ////
//////////////////////////////


/* Get the union between a LinkedSet and an arbitrary container. */
template <typename Container, typename T, unsigned int Flags, typename... Ts>
inline LinkedSet<T, Flags, Ts...> operator|(
    const LinkedSet<T, Flags, Ts...>& set,
    const Container& other
) {
    return set.union_(other);
}


/* Get the difference between a LinkedSet and an arbitrary container. */
template <typename Container, typename T, unsigned int Flags, typename... Ts>
inline LinkedSet<T, Flags, Ts...> operator-(
    const LinkedSet<T, Flags, Ts...>& set,
    const Container& other
) {
    return set.difference(other);
}


/* Get the intersection between a LinkedSet and an arbitrary container. */
template <typename Container, typename T, unsigned int Flags, typename... Ts>
inline LinkedSet<T, Flags, Ts...> operator&(
    const LinkedSet<T, Flags, Ts...>& set,
    const Container& other
) {
    return set.intersection(other);
}


/* Get the symmetric difference between a LinkedSet and an arbitrary container. */
template <typename Container, typename T, unsigned int Flags, typename... Ts>
inline LinkedSet<T, Flags, Ts...> operator^(
    const LinkedSet<T, Flags, Ts...>& set,
    const Container& other
) {
    return set.symmetric_difference(other);
}


/* Update a LinkedSet in-place, replacing it with the union of it and an arbitrary
container. */
template <typename Container, typename T, unsigned int Flags, typename... Ts>
inline LinkedSet<T, Flags, Ts...>& operator|=(
    LinkedSet<T, Flags, Ts...>& set,
    const Container& other
) {
    set.update(other);
    return set;
}


/* Update a LinkedSet in-place, replacing it with the difference between it and an
arbitrary container. */
template <typename Container, typename T, unsigned int Flags, typename... Ts>
inline LinkedSet<T, Flags, Ts...>& operator-=(
    LinkedSet<T, Flags, Ts...>& set,
    const Container& other
) {
    set.difference_update(other);
    return set;
}


/* Update a LinkedSet in-place, replacing it with the intersection between it and an
arbitrary container. */
template <typename Container, typename T, unsigned int Flags, typename... Ts>
inline LinkedSet<T, Flags, Ts...>& operator&=(
    LinkedSet<T, Flags, Ts...>& set,
    const Container& other
) {
    set.intersection_update(other);
    return set;
}


/* Update a LinkedSet in-place, replacing it with the symmetric difference between it
and an arbitrary container. */
template <typename Container, typename T, unsigned int Flags, typename... Ts>
inline LinkedSet<T, Flags, Ts...>& operator^=(
    LinkedSet<T, Flags, Ts...>& set,
    const Container& other
) {
    set.symmetric_difference_update(other);
    return set;
}


//////////////////////////////
////    SET COMPARISON    ////
//////////////////////////////


/* Check whether a LinkedSet is a proper subset of an arbitrary container. */
template <typename Container, typename T, unsigned int Flags, typename... Ts>
inline bool operator<(const LinkedSet<T, Flags, Ts...>& set, const Container& other) {
    return linked::issubset(set.view, other, true);
}


/* Check whether a LinkedSet is a subset of an arbitrary container. */
template <typename Container, typename T, unsigned int Flags, typename... Ts>
inline bool operator<=(const LinkedSet<T, Flags, Ts...>& set, const Container& other) {
    return linked::issubset(set.view, other, false);
}


/* Check whether a LinkedSet is equal to an arbitrary container. */
template <typename Container, typename T, unsigned int Flags, typename... Ts>
inline bool operator==(const LinkedSet<T, Flags, Ts...>& set, const Container& other) {
    return linked::set_equal(set.view, other);
}


/* Check whether a LinkedSet is not equal to an arbitrary container. */
template <typename Container, typename T, unsigned int Flags, typename... Ts>
inline bool operator!=(const LinkedSet<T, Flags, Ts...>& set, const Container& other) {
    return linked::set_not_equal(set.view, other);
}


/* Check whether a LinkedSet is a superset of an arbitrary container. */
template <typename Container, typename T, unsigned int Flags, typename... Ts>
inline bool operator>=(const LinkedSet<T, Flags, Ts...>& set, const Container& other) {
    return linked::issuperset(set.view, other, false);
}


/* Check whether a LinkedSet is a proper superset of an arbitrary container. */
template <typename Container, typename T, unsigned int Flags, typename... Ts>
inline bool operator>(const LinkedSet<T, Flags, Ts...>& set, const Container& other) {
    return linked::issuperset(set.view, other, true);
}


//////////////////////////////
////    PYTHON WRAPPER    ////
//////////////////////////////


/* CRTP mixin class containing the Python set interface for a linked data structure. */
template <typename Derived>
class PySetInterface {
public:

    /* Implement `LinkedSet.add()` in Python. */
    static PyObject* add(Derived* self, PyObject* item) {
        try {
            // invoke equivalent C++ method
            std::visit(
                [&item](auto& set) {
                    set.add(item);
                },
                self->variant
            );

            // exit normally
            Py_RETURN_NONE;

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.add_left()` in Python. */
    static PyObject* add_left(Derived* self, PyObject* item) {
        try {
            // invoke equivalent C++ method
            std::visit(
                [&item](auto& set) {
                    set.add_left(item);
                },
                self->variant
            );

            // exit normally
            Py_RETURN_NONE;

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.lru_add()` in Python. */
    static PyObject* lru_add(Derived* self, PyObject* item) {
        try {
            // invoke equivalent C++ method
            std::visit(
                [&item](auto& set) {
                    set.lru_add(item);
                },
                self->variant
            );

            // exit normally
            Py_RETURN_NONE;

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.discard()` in Python. */
    static PyObject* discard(Derived* self, PyObject* item) {
        try {
            // invoke equivalent C++ method
            std::visit(
                [&item](auto& set) {
                    set.discard(item);
                },
                self->variant
            );

            // exit normally
            Py_RETURN_NONE;

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.lru_contains()` in Python. */
    static PyObject* lru_contains(Derived* self, PyObject* item) {
        try {
            // invoke equivalent C++ method
            return std::visit(
                [&item](auto& set) {
                    return PyBool_FromLong(set.lru_contains(item));
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.union()` in Python. */
    static PyObject* union_(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        // allocate new Python set
        Derived* result = reinterpret_cast<Derived*>(
            Derived::__new__(&Derived::Type, nullptr, nullptr)
        );
        if (result == nullptr) return nullptr;  // propagate

        try {
            // invoke equivalent C++ method
            return std::visit(
                [&args, &nargs, &result](auto& set) {
                    if (nargs == 0) {
                        result->from_cpp(set.copy());
                        return reinterpret_cast<PyObject*>(result);
                    }

                    // get union with first item, then update with all others
                    auto copy = set.union_(args[0]);
                    for (Py_ssize_t i = 1; i < nargs; ++i) {
                        copy.update(args[i]);
                    }
                    result->from_cpp(copy);
                    return reinterpret_cast<PyObject*>(result);
                },
                self->variant
            );

        // translate C++ exceptions into Python errors
        } catch (...) {
            util::throw_python();
            Py_DECREF(result);
            return nullptr;
        }
    }

    /* Implement `LinkedSet.union_left()` in Python. */
    static PyObject* union_left(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        // allocate new Python set
        Derived* result = reinterpret_cast<Derived*>(
            Derived::__new__(&Derived::Type, nullptr, nullptr)
        );
        if (result == nullptr) return nullptr;  // propagate

        try {
            // invoke equivalent C++ method
            return std::visit(
                [&args, &nargs, &result](auto& set) {
                    if (nargs == 0) {
                        result->from_cpp(set.copy());
                        return reinterpret_cast<PyObject*>(result);
                    }

                    // get union with first item, then update with all others
                    auto copy = set.union_left(args[0]);
                    for (Py_ssize_t i = 1; i < nargs; ++i) {
                        copy.update_left(args[i]);
                    }
                    result->from_cpp(copy);
                    return reinterpret_cast<PyObject*>(result);
                },
                self->variant
            );

        // translate C++ exceptions into Python errors
        } catch (...) {
            util::throw_python();
            Py_DECREF(result);
            return nullptr;
        }
    }

    /* Implement `LinkedSet.update()` in Python. */
    static PyObject* update(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        try {
            // invoke equivalent C++ method
            std::visit(
                [&args, &nargs](auto& set) {
                    for (Py_ssize_t i = 0; i < nargs; ++i) {
                        set.update(args[i]);
                    }
                },
                self->variant
            );

            // exit normally
            Py_RETURN_NONE;

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.update()` in Python. */
    static PyObject* update_left(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        try {
            // invoke equivalent C++ method
            std::visit(
                [&args, &nargs](auto& set) {
                    for (Py_ssize_t i = 0; i < nargs; ++i) {
                        set.update_left(args[i]);
                    }
                },
                self->variant
            );

            // exit normally
            Py_RETURN_NONE;

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.lru_update()` in Python. */
    static PyObject* lru_update(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        try {
            // invoke equivalent C++ method
            std::visit(
                [&args, &nargs](auto& set) {
                    for (Py_ssize_t i = 0; i < nargs; ++i) {
                        set.lru_update(args[i]);
                    }
                },
                self->variant
            );

            // exit normally
            Py_RETURN_NONE;

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.difference()` in Python. */
    static PyObject* difference(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        // allocate new Python set
        Derived* result = reinterpret_cast<Derived*>(
            Derived::__new__(&Derived::Type, nullptr, nullptr)
        );
        if (result == nullptr) return nullptr;  // propagate

        try {
            // invoke equivalent C++ method
            return std::visit(
                [&args, &nargs, &result](auto& set) {
                    if (nargs == 0) {
                        result->from_cpp(set.copy());
                        return reinterpret_cast<PyObject*>(result);
                    }

                    // get union with first item, then update with all others
                    auto copy = set.difference(args[0]);
                    for (Py_ssize_t i = 1; i < nargs; ++i) {
                        copy.difference_update(args[i]);
                    }
                    result->from_cpp(copy);
                    return reinterpret_cast<PyObject*>(result);
                },
                self->variant
            );

        // translate C++ exceptions into Python errors
        } catch (...) {
            util::throw_python();
            Py_DECREF(result);
            return nullptr;
        }
    }

    /* Implement `LinkedSet.difference_update()` in Python. */
    static PyObject* difference_update(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs
    ) {
        try {
            // invoke equivalent C++ method
            std::visit(
                [&args, &nargs](auto& set) {
                    for (Py_ssize_t i = 0; i < nargs; ++i) {
                        set.difference_update(args[i]);
                    }
                },
                self->variant
            );

            // exit normally
            Py_RETURN_NONE;

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.intersection()` in Python. */
    static PyObject* intersection(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs
    ) {
        // allocate new Python set
        Derived* result = reinterpret_cast<Derived*>(
            Derived::__new__(&Derived::Type, nullptr, nullptr)
        );
        if (result == nullptr) return nullptr;  // propagate

        try {
            // invoke equivalent C++ method
            return std::visit(
                [&args, &nargs, &result](auto& set) {
                    if (nargs == 0) {
                        result->from_cpp(set.copy());
                        return reinterpret_cast<PyObject*>(result);
                    }

                    // get union with first item, then update with all others
                    auto copy = set.intersection(args[0]);
                    for (Py_ssize_t i = 1; i < nargs; ++i) {
                        copy.intersection_update(args[i]);
                    }
                    result->from_cpp(copy);
                    return reinterpret_cast<PyObject*>(result);
                },
                self->variant
            );

        // translate C++ exceptions into Python errors
        } catch (...) {
            util::throw_python();
            Py_DECREF(result);
            return nullptr;
        }
    }

    /* Implement `LinkedSet.intersection_update()` in Python. */
    static PyObject* intersection_update(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs
    ) {
        try {
            // invoke equivalent C++ method
            std::visit(
                [&args, &nargs](auto& set) {
                    for (Py_ssize_t i = 0; i < nargs; ++i) {
                        set.intersection_update(args[i]);
                    }
                },
                self->variant
            );

            // exit normally
            Py_RETURN_NONE;

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.symmetric_difference()` in Python. */
    static PyObject* symmetric_difference(Derived* self, PyObject* items) {
        // allocate new Python set
        Derived* result = reinterpret_cast<Derived*>(
            Derived::__new__(&Derived::Type, nullptr, nullptr)
        );
        if (result == nullptr) return nullptr;  // propagate

        try {
            // invoke equivalent C++ method
            return std::visit(
                [&result, &items](auto& set) {
                    result->from_cpp(set.symmetric_difference(items));
                    return reinterpret_cast<PyObject*>(result);
                },
                self->variant
            );

        // translate C++ exceptions into Python errors
        } catch (...) {
            util::throw_python();
            Py_DECREF(result);
            return nullptr;
        }
    }

    /* Implement `LinkedSet.symmetric_difference()` in Python. */
    static PyObject* symmetric_difference_left(Derived* self, PyObject* items) {
        // allocate new Python set
        Derived* result = reinterpret_cast<Derived*>(
            Derived::__new__(&Derived::Type, nullptr, nullptr)
        );
        if (result == nullptr) return nullptr;  // propagate

        try {
            // invoke equivalent C++ method
            return std::visit(
                [&result, &items](auto& set) {
                    result->from_cpp(set.symmetric_difference_left(items));
                    return reinterpret_cast<PyObject*>(result);
                },
                self->variant
            );

        // translate C++ exceptions into Python errors
        } catch (...) {
            util::throw_python();
            Py_DECREF(result);
            return nullptr;
        }
    }

    /* Implement `LinkedSet.symmetric_difference_update()` in Python. */
    static PyObject* symmetric_difference_update(Derived* self, PyObject* items) {
        try {
            // invoke equivalent C++ method
            std::visit(
                [&items](auto& set) {
                    set.symmetric_difference_update(items);
                },
                self->variant
            );

            // exit normally
            Py_RETURN_NONE;

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.symmetric_difference_update_left()` in Python. */
    static PyObject* symmetric_difference_update_left(Derived* self, PyObject* items) {
        try {
            // invoke equivalent C++ method
            std::visit(
                [&items](auto& set) {
                    set.symmetric_difference_update_left(items);
                },
                self->variant
            );

            // exit normally
            Py_RETURN_NONE;

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.isdisjoint()` in Python. */
    static PyObject* isdisjoint(Derived* self, PyObject* other) {
        try {
            return std::visit(
                [&other](auto& set) {
                    return PyBool_FromLong(set.isdisjoint(other));
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.issubset()` in Python. */
    static PyObject* issubset(Derived* self, PyObject* other) {
        try {
            return std::visit(
                [&other](auto& set) {
                    return PyBool_FromLong(set.issubset(other));
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.issubset()` in Python. */
    static PyObject* issuperset(Derived* self, PyObject* other) {
        try {
            return std::visit(
                [&other](auto& set) {
                    return PyBool_FromLong(set.issuperset(other));
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.distance()` in Python. */
    static PyObject* distance(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        using Args = util::PyArgs<util::CallProtocol::FASTCALL>;
        static constexpr std::string_view meth_name{"distance"};
        try {
            // parse arguments
            Args pyargs(meth_name, args, nargs);
            PyObject* item1 = pyargs.parse("item1");
            PyObject* item2 = pyargs.parse("item2");
            pyargs.finalize();

            // invoke equivalent C++ method
            return std::visit(
                [&item1, &item2](auto& set) {
                    return PyLong_FromLongLong(set.distance(item1, item2));
                },
                self->variant
            );

        // translate C++ exceptions into Python errors
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement LinkedSet.swap() in Python. */
    static PyObject* swap(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        using Args = util::PyArgs<util::CallProtocol::FASTCALL>;
        static constexpr std::string_view meth_name{"swap"};
        try {
            // parse arguments
            Args pyargs(meth_name, args, nargs);
            PyObject* item1 = pyargs.parse("item1");
            PyObject* item2 = pyargs.parse("item2");
            pyargs.finalize();

            // invoke equivalent C++ method
            std::visit(
                [&item1, &item2](auto& set) {
                    set.swap(item1, item2);
                },
                self->variant
            );

            // exit normally
            Py_RETURN_NONE;

        // translate C++ exceptions into Python errors
        } catch (...) {
            util::throw_python();
            return nullptr;
        }

    }

    /* Implement `LinkedSet.move()` in Python. */
    static PyObject* move(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        using Args = util::PyArgs<util::CallProtocol::FASTCALL>;
        static constexpr std::string_view meth_name{"move"};
        try {
            // parse arguments
            Args pyargs(meth_name, args, nargs);
            PyObject* item = pyargs.parse("item");
            long long steps = pyargs.parse("steps", util::parse_int);
            pyargs.finalize();

            // invoke equivalent C++ method
            std::visit(
                [&item, &steps](auto& set) {
                    set.move(item, steps);
                },
                self->variant
            );

            // exit normally
            Py_RETURN_NONE;

        // translate C++ exceptions into Python errors
        } catch (...) {
            util::throw_python();
            return nullptr;
        }

    }

    /* Implement `LinkedSet.move_to_index()` in Python. */
    static PyObject* move_to_index(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        using Args = util::PyArgs<util::CallProtocol::FASTCALL>;
        static constexpr std::string_view meth_name{"move_to_index"};
        try {
            // parse arguments
            Args pyargs(meth_name, args, nargs);
            PyObject* item = pyargs.parse("item");
            long long index = pyargs.parse("index", util::parse_int);
            pyargs.finalize();

            // invoke equivalent C++ method
            std::visit(
                [&item, &index](auto& set) {
                    set.move_to_index(item, index);
                },
                self->variant
            );

            // exit normally
            Py_RETURN_NONE;

        // translate C++ exceptions into Python errors
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.__or__()` (union operator) in Python. */
    static PyObject* __or__(Derived* self, PyObject* other) {
        // allocate new Python set
        Derived* result = reinterpret_cast<Derived*>(
            Derived::__new__(&Derived::Type, nullptr, nullptr)
        );
        if (result == nullptr) return nullptr;  // propagate

        // delegate to equivalent C++ operator
        try {
            return std::visit(
                [&other, &result](auto& set) {
                    result->from_cpp(set | other);
                    return reinterpret_cast<PyObject*>(result);
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            Py_DECREF(result);
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.__ior__()` (in-place union operator) in Python. */
    static PyObject* __ior__(Derived* self, PyObject* other) {
        // delegate to equivalent C++ operator
        try {
            std::visit(
                [&other](auto& set) {
                    set |= other;
                },
                self->variant
            );
            Py_INCREF(self);
            return reinterpret_cast<PyObject*>(self);

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.__sub__()` (difference operator) in Python. */
    static PyObject* __sub__(Derived* self, PyObject* other) {
        // allocate new Python set
        Derived* result = reinterpret_cast<Derived*>(
            Derived::__new__(&Derived::Type, nullptr, nullptr)
        );
        if (result == nullptr) return nullptr;  // propagate

        // delegate to equivalent C++ operator
        try {
            return std::visit(
                [&other, &result](auto& set) {
                    result->from_cpp(set - other);
                    return reinterpret_cast<PyObject*>(result);
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            Py_DECREF(result);
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.__isub__()` (in-place difference operator) in Python. */
    static PyObject* __isub__(Derived* self, PyObject* other) {
        // delegate to equivalent C++ operator
        try {
            std::visit(
                [&other](auto& set) {
                    set -= other;
                },
                self->variant
            );
            Py_INCREF(self);
            return reinterpret_cast<PyObject*>(self);

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.__and__()` (iuntersection operator) in Python. */
    static PyObject* __and__(Derived* self, PyObject* other) {
        // allocate new Python set
        Derived* result = reinterpret_cast<Derived*>(
            Derived::__new__(&Derived::Type, nullptr, nullptr)
        );
        if (result == nullptr) return nullptr;  // propagate

        // delegate to equivalent C++ operator
        try {
            return std::visit(
                [&other, &result](auto& set) {
                    result->from_cpp(set & other);
                    return reinterpret_cast<PyObject*>(result);
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            Py_DECREF(result);
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.__iand__()` (in-place intersection operator) in Python. */
    static PyObject* __iand__(Derived* self, PyObject* other) {
        // delegate to equivalent C++ operator
        try {
            std::visit(
                [&other](auto& set) {
                    set &= other;
                },
                self->variant
            );
            Py_INCREF(self);
            return reinterpret_cast<PyObject*>(self);

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.__xor__()` (symmetric difference operator) in Python. */
    static PyObject* __xor__(Derived* self, PyObject* other) {
        // allocate new Python set
        Derived* result = reinterpret_cast<Derived*>(
            Derived::__new__(&Derived::Type, nullptr, nullptr)
        );
        if (result == nullptr) return nullptr;  // propagate

        // delegate to equivalent C++ operator
        try {
            return std::visit(
                [&other, &result](auto& set) {
                    result->from_cpp(set ^ other);
                    return reinterpret_cast<PyObject*>(result);
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            Py_DECREF(result);
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedSet.__ixor__()` (in-place symmetric difference operator) in
    Python. */
    static PyObject* __ixor__(Derived* self, PyObject* other) {
        // delegate to equivalent C++ operator
        try {
            std::visit(
                [&other](auto& set) {
                    set ^= other;
                },
                self->variant
            );
            Py_INCREF(self);
            return reinterpret_cast<PyObject*>(self);

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

protected:

    /* docstrings for public Python attributes. */
    struct docs {

        static constexpr std::string_view add {R"doc(
Insert an item at the end of the set if it is not already present.

Parameters
----------
item : Any
    The item to insert.

Notes
-----
Adds are O(1) for both ends of the set.
)doc"
        };

        static constexpr std::string_view add_left {R"doc(
Insert an item at the beginning of the set if it is not already present.

Parameters
----------
item : Any
    The item to insert.

Notes
-----
This method is analogous to the ``appendleft()`` method of a
:class:`collections.deque` object, except that only appends the item if it is
not already contained within the set.

Adds are O(1) for both ends of the set.
)doc"
        };

        static constexpr std::string_view lru_add {R"doc(
Insert an item at the front of the set if it is not present, or move it there
if it is.  Evicts the last item if the set is of fixed size and already full.

Parameters
----------
item : Any
    The item to move/insert.

Notes
-----
This method is roughly equivalent to:

.. code-block:: python

    set.add(item, left=True)
    set.move(item, 0)

except that it avoids repeated lookups and evicts the last item if the set is
already full.  The linked nature of the data structure makes this extremely
efficient, allowing the set to act as a fast LRU cache, particularly if it is
doubly-linked.

LRU adds are O(1) for doubly-linked sets and O(n) for singly-linked ones.
)doc"
        };

        static constexpr std::string_view discard {R"doc(
Remove an item from the set if it is present.

Parameters
----------
item : Any
    The item to remove.

Notes
-----
Discards are O(1) for doubly-linked sets and O(n) for singly-linked ones.  This
is due to the need to traverse the entire set in order to find the previous
node.
)doc"
        };

        static constexpr std::string_view lru_contains {R"doc(
Search the set for an item, moving it to the front if it is present.

Parameters
----------
item : Any
    The item to search for.

Returns
-------
bool
    True if the item is present in the set.  False otherwise.

Notes
-----
This method is equivalent to ``item in set`` except that it also moves the item
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
        for item in container:
            set.add(item)
            set.move(item, 0)

Except that it avoids repeated lookups, collapses the loops, and evicts the
last item if the set is already full.  The linked nature of the data structure
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
Get the linear distance between two items in the set.

Parameters
----------
item1 : Any
    The first item.
item2 : Any
    The second item.

Returns
-------
int
    The difference between the indices of the two items.  Positive values
    indicate that ``item1`` is to the left of ``item2``, while negative values
    indicate the opposite.  If the items are the same, this will be 0.

Raises
------
KeyError
    If either item is not present in the set.

Notes
-----
This method is equivalent to ``set.index(item2) - set.index(item1)``, except
that it gathers both indices in a single iteration.

Distance calculations are O(n).
)doc"
        };

        static constexpr std::string_view swap {R"doc(
Swap the positions of two items within the set.

Parameters
----------
item1 : Any
    The first item.
item2 : Any
    The second item.

Raises
------
KeyError
    If either item is not present in the set.

Notes
-----
This method is O(1) for doubly-linked sets and O(n) otherwise.  This is due to
the need to traverse the entire set in order to find the previous node.
)doc"
        };

        static constexpr std::string_view move {R"doc(
Move an item within the set by the specified number of spaces.

Parameters
----------
item : Any
    The item to move.
steps : int
    The number of spaces to move the item.  If positive, the item will be moved
    forward.  If negative, the item will be moved backward.

Raises
------
KeyError
    If the item is not present in the set.

Notes
-----
This method is O(steps) for doubly-linked sets and singly-linked sets with
``steps > 0``.  For singly-linked sets with ``steps < 0``, it is O(n) due to
the need to traverse the entire set in order to find the previous node.
)doc"
        };

        static constexpr std::string_view move_to_index {R"doc(
Move an item within the set to the specified index.

Parameters
----------
item : Any
    The item to move.
index : int
    The index to move the item to, following the same semantics as the normal
    index operator.

Raises
------
KeyError
    If the item is not present in the set.

Notes
-----
This method is O(n) due to the need to traverse the entire set in order to find
the given index.  For doubly-linked sets, it is optimized to O(n/2).
)doc"
        };

    };

};


/* A discriminated union of templated `LinkedSet` types that can be used from
Python. */
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
    using SetConfig = linked::LinkedSet<PyObject*, Flags, util::BasicLock>;
    using Variant = std::variant<
        SetConfig<Config::DOUBLY_LINKED | Config::DYNAMIC>,
        SetConfig<Config::DOUBLY_LINKED | Config::DYNAMIC | Config::PACKED>,
        SetConfig<Config::DOUBLY_LINKED | Config::DYNAMIC | Config::STRICTLY_TYPED>,
        SetConfig<Config::DOUBLY_LINKED | Config::DYNAMIC | Config::PACKED | Config::STRICTLY_TYPED>,
        SetConfig<Config::DOUBLY_LINKED | Config::FIXED_SIZE>,
        SetConfig<Config::DOUBLY_LINKED | Config::FIXED_SIZE | Config::PACKED>,
        SetConfig<Config::DOUBLY_LINKED | Config::FIXED_SIZE | Config::STRICTLY_TYPED>,
        SetConfig<Config::DOUBLY_LINKED | Config::FIXED_SIZE | Config::PACKED | Config::STRICTLY_TYPED>,
        SetConfig<Config::SINGLY_LINKED | Config::DYNAMIC>,
        SetConfig<Config::SINGLY_LINKED | Config::DYNAMIC | Config::PACKED>,
        SetConfig<Config::SINGLY_LINKED | Config::DYNAMIC | Config::STRICTLY_TYPED>,
        SetConfig<Config::SINGLY_LINKED | Config::DYNAMIC | Config::PACKED | Config::STRICTLY_TYPED>,
        SetConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE>,
        SetConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::PACKED>,
        SetConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::STRICTLY_TYPED>,
        SetConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::PACKED | Config::STRICTLY_TYPED>
    >;
    template <size_t I>
    using Alternative = std::variant_alternative_t<I, Variant>;

    friend Base;
    friend IList;
    friend ISet;
    Variant variant;

    /* Construct a PyLinkedSet around an existing C++ LinkedSet. */
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

    /* Construct a PyLinkedSet from scratch using the given constructor arguments. */
    static void construct(
        PyLinkedSet* self,
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
            case (Config::PACKED):
                CONSTRUCT(1)
            case (Config::STRICTLY_TYPED):
                CONSTRUCT(2)
            case (Config::PACKED | Config::STRICTLY_TYPED):
                CONSTRUCT(3)
            case (Config::FIXED_SIZE):
                CONSTRUCT(4)
            case (Config::FIXED_SIZE | Config::PACKED):
                CONSTRUCT(5)
            case (Config::FIXED_SIZE | Config::STRICTLY_TYPED):
                CONSTRUCT(6)
            case (Config::FIXED_SIZE | Config::PACKED | Config::STRICTLY_TYPED):
                CONSTRUCT(7)
            case (Config::SINGLY_LINKED):
                CONSTRUCT(8)
            case (Config::SINGLY_LINKED | Config::PACKED):
                CONSTRUCT(9)
            case (Config::SINGLY_LINKED | Config::STRICTLY_TYPED):
                CONSTRUCT(10)
            case (Config::SINGLY_LINKED | Config::PACKED | Config::STRICTLY_TYPED):
                CONSTRUCT(11)
            case (Config::SINGLY_LINKED | Config::FIXED_SIZE):
                CONSTRUCT(12)
            case (Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::PACKED):
                CONSTRUCT(13)
            case (Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::STRICTLY_TYPED):
                CONSTRUCT(14)
            case (Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::PACKED | Config::STRICTLY_TYPED):
                CONSTRUCT(15)
            default:
                throw util::ValueError("invalid argument configuration");
        }
    }

    #undef CONSTRUCT

public:

    /* Initialize a LinkedSet instance from Python. */
    static int __init__(PyLinkedSet* self, PyObject* args, PyObject* kwargs) {
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
            bool packed = pyargs.parse("packed", util::is_truthy, false);
            pyargs.finalize();

            // initialize
            construct(
                self, iterable, max_size, spec, reverse, singly_linked, packed, false
            );

            // exit normally
            return 0;

        // translate C++ exceptions into Python eerrors
        } catch (...) {
            util::throw_python();
            return -1;
        }
    }

    /* Implement `LinkedSet.__str__()` in Python. */
    static PyObject* __str__(PyLinkedSet* self) {
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

    /* Implement `LinkedSet.__repr__()` in Python. */
    static PyObject* __repr__(PyLinkedSet* self) {
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

        static constexpr std::string_view LinkedSet {R"doc(
An ordered set based on a doubly-linked list.

This class is a drop-in replacement for a built-in :class:`set`, supporting all
the same operations, plus those of the built-in :class:`list` and some extras
related to the ordered nature of the set.  It is also available in C++ under
the same name, with equivalent semantics.

Parameters
----------
items : Iterable[Any], optional
    The items to initialize the set with.  If not specified, the set will be
    empty.
max_size : int, optional
    The maximum number of items that the set can hold.  If not specified, the
    set will be unbounded.
spec : Any, optional
    A specific type to enforce for elements of the set, allowing the creation
    of type-safe containers.  This can be in any format recognized by
    :func:`isinstance() <python:isinstance>`.  The default is ``None``, which
    disables strict type checking for the set.  See the :meth:`specialize()`
    method for more details.
reverse : bool, default False
    If True, reverse the order of ``items`` during set construction.  This is
    more efficient than calling :meth:`reverse()` after construction.
singly_linked : bool, default False
    If True, use a singly-linked set instead of a doubly-linked set.  This
    trades some performance in certain operations for increased memory
    efficiency.  Regardless of this setting, the set will still support all
    the same operations as a doubly-linked set.

Notes
-----
These data structures are highly optimized for performance, and are generally
on par with the built-in :class:`set` type.  They have slightly more overhead
due to handling the links between each node, but users should not notice a
significant difference on average.

The data structure itself is implemented entirely in C++, and can be used
natively at the C++ level.  The Python wrapper is directly equivalent to the
C++ class, and is provided for convenience.  Technically speaking, the Python
class represents a ``std::variant`` of possible C++ implementations, each of
which is templated for maximum performance.  The Python class is therefore
slightly slower than the C++ class due to extra indirection, but the difference
is negligible, and can mostly be attributed to the Python interpreter itself.

Due to the symmetry between Python and C++, users should be able to easily port
code that relies on this data structure with only minimal changes.
)doc"
        };

        // TODO: modify docs for remove(), count()?

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

    /* Vtable containing Python @property definitions for the LinkedSet. */
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

    /* Vtable containing Python method definitions for the LinkedSet. */
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

    #undef PROPERTY
    #undef BASE_METHOD
    #undef LIST_METHOD
    #undef SET_METHOD

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
            .tp_name = "bertrand.structs.LinkedSet",
            .tp_basicsize = sizeof(PyLinkedSet),
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
            .tp_doc = PyDoc_STR(docs::LinkedSet.data()),
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
static struct PyModuleDef module_set = {
    PyModuleDef_HEAD_INIT,
    .m_name = "set",
    .m_doc = (
        "This module contains an optimized LinkedSet data structure for use "
        "in Python.  The exact same data structure is also available in C++ "
        "under the same header path (bertrand/structs/linked/set.h)."
    ),
    .m_size = -1,
};


/* Python import hook. */
PyMODINIT_FUNC PyInit_set(void) {
    // initialize type objects
    if (PyType_Ready(&PyLinkedSet::Type) < 0) return nullptr;

    // initialize module
    PyObject* mod = PyModule_Create(&module_set);
    if (mod == nullptr) return nullptr;

    // link type to module
    Py_INCREF(&PyLinkedSet::Type);
    if (PyModule_AddObject(mod, "LinkedSet", (PyObject*) &PyLinkedSet::Type) < 0) {
        Py_DECREF(&PyLinkedSet::Type);
        Py_DECREF(mod);
        return nullptr;
    }
    return mod;
}


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_SET_H
