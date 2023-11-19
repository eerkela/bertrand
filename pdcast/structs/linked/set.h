// include guard: BERTRAND_STRUCTS_LINKED_SET_H
#ifndef BERTRAND_STRUCTS_LINKED_SET_H
#define BERTRAND_STRUCTS_LINKED_SET_H

#include <cstddef>  // size_t
#include <optional>  // std::optional<>
#include <ostream>  // std::ostream
#include <sstream>  // std::ostringstream
#include <Python.h>  // CPython API
#include "../util/args.h"  // PyArgs
#include "../util/except.h"  // throw_python()
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
    inline void insert(long long index, const Value& item) {
        linked::insert(this->view, index, item);
    }

    /* Extend a set by adding elements from an iterable that are not already present. */
    template <typename Container>
    inline void update(const Container& items, bool left = false) {
        linked::update(this->view, items, left);
    }

    /* Remove elements from a set that are contained in the given iterable. */
    template <typename Container>
    inline void difference_update(const Container& items) {
        linked::difference_update(this->view, items);
    }

    /* Removal elements from a set that are not contained in the given iterable. */
    template <typename Container>
    inline void intersection_update(const Container& items) {
        linked::intersection_update(this->view, items);
    }

    /* Update a set, keeping only elements found in either the set or the given
    container, but not both. */
    template <typename Container>
    inline void symmetric_difference_update(const Container& items, bool left = false) {
        linked::symmetric_difference_update(this->view, items, left);
    }

    /* Get the index of an item within the list. */
    inline size_t index(
        const Value& item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return linked::index(this->view, item, start, stop);
    }

    /* Count the number of occurrences of an item within the list. */
    inline size_t count(
        const Value& item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return linked::count(this->view, item, start, stop);
    }

    /* Check if the list contains a certain item. */
    inline bool contains(const Value& item) const {
        return linked::contains(this->view, item);
    }

    /* Remove the first occurrence of an item from the list. */
    inline void remove(const Value& item) {
        linked::remove(this->view, item);
    }

    /* Remove an item from the set if it is present. */
    inline void discard(const Value& item) {
        linked::discard(this->view, item);
    }

    /* Remove an item from the list and return its value. */
    inline Value pop(long long index = -1) {
        return linked::pop(this->view, index);
    }

    /* Remove all elements from the list. */
    inline void clear() {
        this->view.clear();
    }

    /* Return a shallow copy of the list. */
    inline LinkedSet copy() const {
        return LinkedSet(this->view.copy());
    }

    /* Sort the list in-place according to an optional key func. */
    template <typename Func>
    inline void sort(Func key = nullptr, bool reverse = false) {
        linked::sort<linked::MergeSort>(this->view, key, reverse);
    }

    /* Reverse the order of elements in the list in-place. */
    inline void reverse() {
        linked::reverse(this->view);
    }

    /* Shift all elements in the list to the right by the specified number of steps. */
    inline void rotate(long long steps = 1) {
        linked::rotate(this->view, steps);
    }

    /* Return a new set with elements from this set and all other container(s). */
    template <typename Container>
    inline LinkedSet union_(Container&& other, bool left = false) const {
        return LinkedSet(
            linked::union_(
                this->view, 
                std::forward<Container>(other),
                left
            )
        );
    }

    /* Return a new set with elements from this set that are common to all other
    container(s).  */
    template <typename Container>
    inline LinkedSet intersection(Container&& other) const {
        return LinkedSet(
            linked::intersection(
                this->view,
                std::forward<Container>(other)
            )
        );
    }

    /* Return a new set with elements in this set that are not in the other
    container(s). */
    template <typename Container>
    inline LinkedSet difference(Container&& other) const {
        return LinkedSet(
            linked::difference(
                this->view,
                std::forward<Container>(other)
            )
        );
    }

    /* Return a new set with elements in either this set or another container, but not
    both. */
    template <typename Container>
    inline LinkedSet symmetric_difference(Container&& other, bool left = false) const {
        return LinkedSet(
            linked::symmetric_difference(
                this->view,
                std::forward<Container>(other),
                left
            )
        );
    }

    /* Check whether the set has no elements in common with another container. */
    template <typename Container>
    inline bool isdisjoint(const Container& other) const {
        return linked::isdisjoint(this->view, other);
    }

    /* Check whether all items within the set are also present in another container. */
    template <typename Container>
    inline bool issubset(const Container& other) const {
        return linked::issubset(this->view, other, false);
    }

    /* Check whether the set contains all items within another container. */
    template <typename Container>
    inline bool issuperset(const Container& other) const {
        return linked::issuperset(this->view, other, false);
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
    inline linked::ElementProxy<View> operator[](long long index) {
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

};


/////////////////////////////////////
////    STRING REPRESENTATION    ////
/////////////////////////////////////


/* Override the << operator to print the abbreviated contents of a set to an output
stream (equivalent to Python repr()). */
template <typename... Ts>
std::ostream& operator<<(std::ostream& stream, const LinkedSet<Ts...>& set) {
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
template <typename Container, typename... Ts>
LinkedSet<Ts...> operator|(const LinkedSet<Ts...>& set, const Container& other) {
    return set.union_(other);
}


/* Get the difference between a LinkedSet and an arbitrary container. */
template <typename Container, typename... Ts>
LinkedSet<Ts...> operator-(const LinkedSet<Ts...>& set, const Container& other) {
    return set.difference(other);
}


/* Get the intersection between a LinkedSet and an arbitrary container. */
template <typename Container, typename... Ts>
LinkedSet<Ts...> operator&(const LinkedSet<Ts...>& set, const Container& other) {
    return set.intersection(other);
}


/* Get the symmetric difference between a LinkedSet and an arbitrary container. */
template <typename Container, typename... Ts>
LinkedSet<Ts...> operator^(const LinkedSet<Ts...>& set, const Container& other) {
    return set.symmetric_difference(other);
}


/* Update a LinkedSet in-place, replacing it with the union of it and an arbitrary
container. */
template <typename Container, typename... Ts>
LinkedSet<Ts...>& operator|=(LinkedSet<Ts...>& set, const Container& other) {
    set.update(other);
    return set;
}


/* Update a LinkedSet in-place, replacing it with the difference between it and an
arbitrary container. */
template <typename Container, typename... Ts>
LinkedSet<Ts...>& operator-=(LinkedSet<Ts...>& set, const Container& other) {
    set.difference_update(other);
    return set;
}


/* Update a LinkedSet in-place, replacing it with the intersection between it and an
arbitrary container. */
template <typename Container, typename... Ts>
LinkedSet<Ts...>& operator&=(LinkedSet<Ts...>& set, const Container& other) {
    set.intersection_update(other);
    return set;
}


/* Update a LinkedSet in-place, replacing it with the symmetric difference between it
and an arbitrary container. */
template <typename Container, typename... Ts>
LinkedSet<Ts...>& operator^=(LinkedSet<Ts...>& set, const Container& other) {
    set.symmetric_difference_update(other);
    return set;
}


//////////////////////////////
////    SET COMPARISON    ////
//////////////////////////////


/* Check whether a LinkedSet is a proper subset of an arbitrary container. */
template <typename Container, typename... Ts>
bool operator<(const LinkedSet<Ts...>& set, const Container& other) {
    return linked::issubset(set.view, other, true);
}


/* Check whether a LinkedSet is a subset of an arbitrary container. */
template <typename Container, typename... Ts>
bool operator<=(const LinkedSet<Ts...>& set, const Container& other) {
    return linked::issubset(set.view, other, false);
}


/* Check whether a LinkedSet is equal to an arbitrary container. */
template <typename Container, typename... Ts>
bool operator==(const LinkedSet<Ts...>& set, const Container& other) {
    return linked::set_equal(set.view, other);
}


/* Check whether a LinkedSet is not equal to an arbitrary container. */
template <typename Container, typename... Ts>
bool operator!=(const LinkedSet<Ts...>& set, const Container& other) {
    return linked::set_not_equal(set.view, other);
}


/* Check whether a LinkedSet is a superset of an arbitrary container. */
template <typename Container, typename... Ts>
bool operator>=(const LinkedSet<Ts...>& set, const Container& other) {
    return linked::issuperset(set.view, other, false);
}


/* Check whether a LinkedSet is a proper superset of an arbitrary container. */
template <typename Container, typename... Ts>
bool operator>(const LinkedSet<Ts...>& set, const Container& other) {
    return linked::issuperset(set.view, other, true);
}


//////////////////////////////
////    PYTHON WRAPPER    ////
//////////////////////////////


/* CRTP mixin class that contains the Python set interface for a linked data
structure. */
template <typename Derived>
class PySetInterface {
public:

    /* Implement `LinkedSet.add()` in Python. */
    static PyObject* add(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) {
        using Args = util::PyArgs<util::CallProtocol::VECTORCALL>;
        static constexpr std::string_view meth_name{"add"};
        try {
            // parse arguments
            Args pyargs(meth_name, args, nargs, kwnames);
            PyObject* item = pyargs.parse("item");
            bool left = pyargs.parse("left", util::is_truthy, false);
            pyargs.finalize();

            // invoke equivalent C++ method
            std::visit(
                [&item, &left](auto& set) {
                    set.add(item, left);
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

    /* Implement `LinkedSet.union()` in Python. */
    static PyObject* union_(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) {
        using Args = util::PyArgs<util::CallProtocol::VECTORCALL>;
        static constexpr std::string_view meth_name{"union"};

        // allocate new Python set
        Derived* result = reinterpret_cast<Derived*>(
            Derived::__new__(&Derived::Type, nullptr, nullptr)
        );
        if (result == nullptr) return nullptr;  // propagate

        try {
            // parse arguments
            Args pyargs(meth_name, args, nargs, kwnames);
            bool left = pyargs.keyword("left", util::is_truthy, false);
            pyargs.finalize_keyword();  // allow for variadic positional args

            // invoke equivalent C++ method
            return std::visit(
                [&args, &nargs, &result, &left](auto& set) {
                    if (nargs == 0) {
                        result->from_cpp(set.copy());
                        return reinterpret_cast<PyObject*>(result);
                    }

                    // get union with first item, then update with all others
                    auto copy = set.union_(args[0], left);
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

    /* Implement `LinkedSet.update()` in Python. */
    static PyObject* update(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) {
        using Args = util::PyArgs<util::CallProtocol::VECTORCALL>;
        static constexpr std::string_view meth_name{"update"};
        try {
            // parse arguments
            Args pyargs(meth_name, args, nargs, kwnames);
            bool left = pyargs.keyword("left", util::is_truthy, false);
            pyargs.finalize_keyword();  // allow for variadic positional args

            // invoke equivalent C++ method
            std::visit(
                [&args, &nargs, &left](auto& set) {
                    for (Py_ssize_t i = 0; i < nargs; ++i) {
                        set.update(args[i], left);
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
    static PyObject* difference(
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
    static PyObject* symmetric_difference(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) {
        using Args = util::PyArgs<util::CallProtocol::VECTORCALL>;
        static constexpr std::string_view meth_name{"symmetric_difference"};

        // allocate new Python set
        Derived* result = reinterpret_cast<Derived*>(
            Derived::__new__(&Derived::Type, nullptr, nullptr)
        );
        if (result == nullptr) return nullptr;  // propagate

        try {
            // parse arguments
            Args pyargs(meth_name, args, nargs, kwnames);
            PyObject* other = pyargs.parse("other");
            bool left = pyargs.parse("left", util::is_truthy, false);
            pyargs.finalize();

            // invoke equivalent C++ method
            return std::visit(
                [&result, &other, &left](auto& set) {
                    result->from_cpp(set.symmetric_difference(other, left));
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
    static PyObject* symmetric_difference_update(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) {
        using Args = util::PyArgs<util::CallProtocol::VECTORCALL>;
        static constexpr std::string_view meth_name{"symmetric_difference_update"};
        try {
            // parse arguments
            Args pyargs(meth_name, args, nargs, kwnames);
            PyObject* items = pyargs.parse("items");
            bool left = pyargs.parse("left", util::is_truthy, false);
            pyargs.finalize();

            // invoke equivalent C++ method
            std::visit(
                [&items, &left](auto& set) {
                    set.symmetric_difference_update(items, left);
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
            std::visit(
                [&other](auto& set) {
                    set.isdisjoint(other);
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

    /* Implement `LinkedSet.issubset()` in Python. */
    static PyObject* issubset(Derived* self, PyObject* other) {
        try {
            std::visit(
                [&other](auto& set) {
                    set.issubset(other);
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

    /* Implement `LinkedSet.issubset()` in Python. */
    static PyObject* issuperset(Derived* self, PyObject* other) {
        try {
            std::visit(
                [&other](auto& set) {
                    set.issuperset(other);
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

    /* Implement `LinkedSet.distance()` in Python. */
    static PyObject* distance(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs
    ) {
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
    static PyObject* swap(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs
    ) {
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
    static PyObject* move(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) {
        using Args = util::PyArgs<util::CallProtocol::VECTORCALL>;
        static constexpr std::string_view meth_name{"move"};
        try {
            // parse arguments
            Args pyargs(meth_name, args, nargs, kwnames);
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
    static PyObject* move_to_index(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) {
        using Args = util::PyArgs<util::CallProtocol::VECTORCALL>;
        static constexpr std::string_view meth_name{"move_to_index"};
        try {
            // parse arguments
            Args pyargs(meth_name, args, nargs, kwnames);
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
left : bool, default False
    If True, insert the item at the beginning of the set instead of the end.

Notes
-----
If ``left=True``, then the item will be appended to the beginning of the set
rather than the end.

Adds are O(1) for both ends of the set.
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

        static constexpr std::string_view union_ {R"doc(
Return a new set with the merged contents of this set and all other containers.

Parameters
----------
*others : Iterable[Any]
    An arbitrary number of containers passed to this method as positional
    arguments.  Must contain at least one element.
left : bool, default False
    If True, insert each item at the beginning of the set instead of the end.

Returns
-------
LinkedSet
    A new set containing the union of all the given containers.
)doc"
        };

        static constexpr std::string_view update {R"doc(
Update a set in-place, merging the contents of all other containers.

Parameters
----------
*others : Iterable[Any]
    An arbitrary number of containers passed to this method as positional
    arguments.  Must contain at least one element.
left : bool, default False
    If True, insert each item at the beginning of the set instead of the end.
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
left : bool, default False
    If True, insert each item at the beginning of the set instead of the end.

Returns
-------
LinkedSet
    A new set containing only those elements that are not shared between the
    original set and the given container.
)doc"
        };

        static constexpr std::string_view symmetric_difference_update {R"doc(
Update a set in-place, removing any elements that are stored within the given
container and adding any that are missing.

Parameters
----------
other : Iterable[Any]
    Another container to compare against.
left : bool, default False
    If True, insert each item at the beginning of the set instead of the end.
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
the given index.  For doubly-linked lists, it is optimized to O(n/2).
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
    using SingleSet = LinkedSet<linked::SingleNode<PyObject*>, util::BasicLock>;
    using DoubleSet = LinkedSet<linked::DoubleNode<PyObject*>, util::BasicLock>;
    using Variant = std::variant<
        SingleSet,
        DoubleSet
    >;

    friend Base;
    friend IList;
    friend ISet;
    Variant variant;

    /* Construct a PyLinkedSet around an existing C++ LinkedSet. */
    template <typename Set>
    inline void from_cpp(Set&& set) {
        new (&variant) Variant(std::forward<Set>(set));
    }

    /* Construct a PyLinkedList from scratch using the given constructor arguments. */
    static void construct(
        PyLinkedSet* self,
        PyObject* iterable,
        std::optional<size_t> max_size,
        PyObject* spec,
        bool reverse,
        bool singly_linked
    ) {
        if (iterable == nullptr) {
            if (singly_linked) {
                new (&self->variant) Variant(SingleSet(max_size, spec));
            } else {
                new (&self->variant) Variant(DoubleSet(max_size, spec));
            }
        } else {
            if (singly_linked) {
                new (&self->variant) Variant(
                    SingleSet(iterable, max_size, spec, reverse)
                );
            } else {
                new (&self->variant) Variant(
                    DoubleSet(iterable, max_size, spec, reverse)
                );
            }
        }
    }

public:

    /* Initialize a LinkedSet instance from Python. */
    static int __init__(
        PyLinkedSet* self,
        PyObject* args,
        PyObject* kwargs
    ) {
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
    The items to initialize the list with.  If not specified, the list will be
    empty.
max_size : int, optional
    The maximum number of items that the list can hold.  If not specified, the
    list will be unbounded.
spec : Any, optional
    A specific type to enforce for elements of the list, allowing the creation
    of type-safe containers.  This can be in any format recognized by
    :func:`isinstance() <python:isinstance>`.  The default is ``None``, which
    disables strict type checking for the list.  See the :meth:`specialize()`
    method for more details.
reverse : bool, default False
    If True, reverse the order of ``items`` during list construction.  This is
    more efficient than calling :meth:`reverse()` after construction.
singly_linked : bool, default False
    If True, use a singly-linked list instead of a doubly-linked list.  This
    trades some performance in certain operations for increased memory
    efficiency.  Regardless of this setting, the list will still support all
    the same operations as a doubly-linked list.

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

    /* Vtable containing Python @property definitions for the LinkedList. */
    inline static PyGetSetDef properties[] = {
        BASE_PROPERTY(lock),
        BASE_PROPERTY(capacity),
        BASE_PROPERTY(max_size),
        BASE_PROPERTY(dynamic),
        BASE_PROPERTY(frozen),
        BASE_PROPERTY(nbytes),
        BASE_PROPERTY(specialization),
        {NULL}  // sentinel
    };

    /* Vtable containing Python method definitions for the LinkedList. */
    inline static PyMethodDef methods[] = {
        BASE_METHOD(reserve, METH_FASTCALL),
        BASE_METHOD(defragment, METH_NOARGS),
        BASE_METHOD(specialize, METH_O),
        BASE_METHOD(__reversed__, METH_NOARGS),
        BASE_METHOD(__class_getitem__, METH_CLASS | METH_O),
        LIST_METHOD(insert, METH_FASTCALL | METH_KEYWORDS),
        LIST_METHOD(index, METH_FASTCALL | METH_KEYWORDS),
        LIST_METHOD(count, METH_FASTCALL | METH_KEYWORDS),
        LIST_METHOD(remove, METH_O),
        LIST_METHOD(pop, METH_FASTCALL),
        LIST_METHOD(clear, METH_NOARGS),
        LIST_METHOD(copy, METH_NOARGS),
        LIST_METHOD(sort, METH_FASTCALL | METH_KEYWORDS),
        LIST_METHOD(reverse, METH_NOARGS),
        LIST_METHOD(rotate, METH_FASTCALL),
        SET_METHOD(add, METH_FASTCALL | METH_KEYWORDS),
        {
            "union",  // renamed
            (PyCFunction) union_,
            METH_FASTCALL | METH_KEYWORDS,
            PyDoc_STR(ISet::docs::union_.data())
        },
        SET_METHOD(update, METH_FASTCALL | METH_KEYWORDS),
        SET_METHOD(difference, METH_FASTCALL),
        SET_METHOD(difference_update, METH_FASTCALL),
        SET_METHOD(intersection, METH_FASTCALL),
        SET_METHOD(intersection_update, METH_FASTCALL),
        SET_METHOD(symmetric_difference, METH_FASTCALL | METH_KEYWORDS),
        SET_METHOD(symmetric_difference_update, METH_FASTCALL | METH_KEYWORDS),
        SET_METHOD(isdisjoint, METH_O),
        SET_METHOD(issubset, METH_O),
        SET_METHOD(issuperset, METH_O),
        SET_METHOD(distance, METH_FASTCALL),
        SET_METHOD(swap, METH_FASTCALL),
        SET_METHOD(move, METH_FASTCALL | METH_KEYWORDS),
        SET_METHOD(move_to_index, METH_FASTCALL | METH_KEYWORDS),
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
        slots.mp_subscript = (binaryfunc) __getitem__;
        slots.mp_ass_subscript = (objobjargproc) __setitem__;
        return slots;
    }();

    /* Vtable containing special methods related to Python's sequence protocol. */
    inline static PySequenceMethods sequence = [] {
        PySequenceMethods slots;
        slots.sq_length = (lenfunc) Base::__len__;
        slots.sq_item = (ssizeargfunc) __getitem_scalar__;
        slots.sq_ass_item = (ssizeobjargproc) __setitem_scalar__;
        slots.sq_contains = (objobjproc) __contains__;
        return slots;
    }();

    /* Initialize a PyTypeObject to represent the list in Python. */
    static PyTypeObject build_type() {
        return {
            .ob_base = PyObject_HEAD_INIT(NULL)
            .tp_name = "bertrand.structs.LinkedSet",
            .tp_basicsize = sizeof(PyLinkedSet),
            .tp_itemsize = 0,
            .tp_dealloc = (destructor) Base::__dealloc__,
            .tp_repr = (reprfunc) __repr__,
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
            .tp_richcompare = (richcmpfunc) __richcompare__,
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
