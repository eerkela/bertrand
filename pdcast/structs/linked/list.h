// include guard: BERTRAND_STRUCTS_LINKED_LIST_H
#ifndef BERTRAND_STRUCTS_LINKED_LIST_H
#define BERTRAND_STRUCTS_LINKED_LIST_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <Python.h>  // CPython API
#include "core/view.h"  // ListView
#include "base.h"  // LinkedBase


#include "../util/args.h"  // PyArgs


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
namespace linked {


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

    /* Insert an item into the list at the specified index. */
    inline void insert(long long index, Value& item) {
        linked::insert(this->view, index, item);
    }

    /* Extend the list by appending elements from an iterable. */
    template <typename Container>
    inline void extend(Container& items, bool left = false) {
        linked::extend(this->view, items, left);
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
    inline bool contains(Value& item) const {
        return linked::contains(this->view, item);
    }

    /* Remove the first occurrence of an item from the list. */
    inline void remove(Value& item) {
        linked::remove(this->view, item);
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
    inline linked::ElementProxy<View> operator[](long long index) {
        return linked::position(this->view, index);
    }

    /* Get a proxy for a slice within the list. */
    template <typename... Args>
    inline linked::SliceProxy<View, LinkedList> slice(Args&&... args) {
        return linked::slice<View, LinkedList>(
            this->view,
            std::forward<Args>(args)...
        );
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















/////////////////////////////////
////    PYTHON EQUIVALENT    ////
/////////////////////////////////


// TODO: initializers should be const-correct at all levels
// -> const PyObject* iterable, const PyObject* spec


/* A class that binds the appropriate methods for the given view as a std::variant
of templated `ListView` types. */
class PyLinkedList {
    using SelfRef = cython::SelfRef<PyLinkedList>;
    using WeakRef = typename SelfRef::WeakRef;
    using SingleList = LinkedList<
        linked::SingleNode<PyObject*>,
        linked::MergeSort,
        util::BasicLock
    >;
    using DoubleList = LinkedList<
        linked::DoubleNode<PyObject*>,
        linked::MergeSort,
        util::BasicLock
    >;

    /* A discriminated union representing all the LinkedList implementations that are
    constructable from Python. */
    using Variant = std::variant<
        SingleList,
        DoubleList
    >;

    PyObject_HEAD
    Variant variant;
    const cython::VariantLock<PyLinkedList> lock;

    /* Select the appropriate variant based on constructor arguments. */
    template <typename... Args>
    inline static Variant get_variant(bool singly_linked, Args&&... args) {
        if (singly_linked) {
            return SingleList(std::forward<Args>(args)...);
        } else {
            return DoubleList(std::forward<Args>(args)...);
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty PyLinkedList. */
    PyLinkedList(
        std::optional<size_t> max_size,
        PyObject* spec,
        bool singly_linked
    ) : variant(get_variant(singly_linked, max_size, spec)), lock(*this)
    {}

    /* Construct a PyLinkedList by unpacking an input iterable. */
    PyLinkedList(
        PyObject* iterable,
        std::optional<size_t> max_size,
        PyObject* spec,
        bool reverse,
        bool singly_linked
    ) : variant(get_variant(singly_linked, iterable, max_size, spec, reverse)),
        lock(*this)
    {}

    /* Construct a PyLinkedList around an existing C++ LinkedList. */
    template <typename List>
    explicit PyLinkedList(List&& list) : variant(std::move(list)), lock(*this) {}

    /* Copy/move constructors/assignment operators deleted for simplicity. */
    PyLinkedList(const PyLinkedList&) = delete;
    PyLinkedList(PyLinkedList&&) = delete;
    PyLinkedList& operator=(const PyLinkedList&) = delete;
    PyLinkedList& operator=(PyLinkedList&&) = delete;

public:

    ////////////////////////////
    ////    LIST METHODS    ////
    ////////////////////////////

    /* Implement `LinkedList.append()` in Python. */
    inline static PyObject* append(
        PyLinkedList* self,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) {
        using Args = util::PyArgs<util::CallProtocol::VECTORCALL>;
        using util::catch_python, util::type_error;

        try {
            // parse arguments
            Args pyargs(args, nargs, kwnames);
            PyObject* item = pyargs.parse("item");
            bool left = pyargs.parse(
                "left",
                [](PyObject* obj) -> bool {
                    int result = PyObject_IsTrue(obj);
                    if (result == -1) throw catch_python<type_error>();
                    return static_cast<bool>(result);
                },
                false
            );

            // invoke C++ method
            std::visit(
                [&item, &left](auto& list) {
                    list.append(item, static_cast<bool>(left));
                },
                self->variant
            );

            // exit normally
            Py_RETURN_NONE;

        // translate C++ errors into Python exceptions
        } catch (...) {
            // TODO
            return nullptr;
        }
    }

    /* Implement `LinkedList.insert()` in Python. */
    inline static PyObject* insert(
        PyLinkedList* self,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) {
        using Args = util::PyArgs<util::CallProtocol::VECTORCALL>;
        using util::catch_python, util::type_error;

        try {
            Args pyargs(args, nargs, kwnames);
            long long index = pyargs.parse(
                "index",
                [](PyObject* obj) -> long long {
                    PyObject* integer = PyNumber_Index(obj);
                    if (integer == nullptr) throw catch_python<type_error>();
                    long long result = PyLong_AsLongLong(integer);
                    Py_DECREF(integer);
                    if (result == -1 && PyErr_Occurred()) throw catch_python<type_error>();
                    return result;
                }
            );
            PyObject* item = pyargs.parse("item");

            // invoke C++ method
            std::visit(
                [&index, &item](auto& list) {
                    list.insert(index, item);
                },
                self->variant
            );

            // exit normally
            Py_RETURN_NONE;

        // translate C++ errors into Python exceptions
        } catch (...) {
            // TODO
            return nullptr;
        }
    }

    /* Implement `LinkedList.extend()` in Python. */
    inline void extend(PyObject* items, bool left = false) {
        std::visit(
            [&items, &left](auto& list) {
                list.extend(items, left);
            },
            this->variant
        );
    }

    /* Implement `LinkedList.index()` in Python. */
    inline size_t index(
        PyObject* item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return std::visit(
            [&item, &start, &stop](auto& list) {
                return list.index(item, start, stop);
            },
            this->variant
        );
    }

    /* Implement `LinkedList.count()` in Python. */
    inline size_t count(
        PyObject* item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return std::visit(
            [&item, &start, &stop](auto& list) {
                return list.count(item, start, stop);
            },
            this->variant
        );
    }

    /* Implement `LinkedList.remove()` in Python. */
    inline void remove(PyObject* item) {
        std::visit(
            [&item](auto& list) {
                list.remove(item);
            },
            this->variant
        );
    }

    /* Implement `LinkedList.pop()` in Python. */
    inline PyObject* pop(long long index = -1) {
        return std::visit(
            [&index](auto& list) {
                return list.pop(index);
            },
            this->variant
        );
    }

    /* Implement `LinkedList.clear()` in Python. */
    inline void clear() {
        std::visit(
            [](auto& list) {
                list.clear();
            },
            this->variant
        );
    }

    /* Implement `LinkedList.copy()` in Python. */
    inline PyObject* copy() const {
        // TODO: return a new PyLinkedList, properly reference counted.
        return nullptr;
    }

    /* Implement `LinkedList.sort()` in Python. */
    inline void sort(PyObject* key = nullptr, bool reverse = false) {
        std::visit(
            [&key, &reverse](auto& list) {
                list.sort(key, reverse);
            },
            this->variant
        );
    }

    /* Implement `LinkedList.reverse()` in Python. */
    inline void reverse() {
        std::visit(
            [](auto& list) {
                list.reverse();
            },
            this->variant
        );
    }

    /* Implement `LinkedList.rotate()` in Python. */
    inline void rotate(long long steps = 1) {
        std::visit(
            [&steps](auto& list) {
                list.rotate(steps);
            },
            this->variant
        );
    }

    /* Implement `LinkedList.__contains__()` in Python. */
    inline static int __contains__(PyLinkedList* self, PyObject* item) {
        return std::visit(
            [&item](auto& list) {
                return list.contains(item);
            },
            self->variant
        );
    }

    /* Implement `LinkedList.__getitem__()` in Python (slice). */
    inline static PyObject* __getitem__(PyLinkedList* self, PyObject* key) {
        try {
            // check for integer index
            if (PyIndex_Check(key)) {
                PyObject* integer = PyNumber_Index(key);
                if (integer == nullptr) return nullptr;  // propagate
                long long index = PyLong_AsLongLong(integer);
                Py_DECREF(integer);
                if (index == -1 && PyErr_Occurred()) return nullptr;  // propagate

                // call scalar C++ method
                PyObject* result = std::visit(
                    [&index](auto& list) {
                        return list[index].get();
                    },
                    self->variant
                );
                return Py_XNewRef(result);  // return borrowed reference
            }

            // // check for slice
            // if (PySlice_Check(key)) {
            //     // TODO: figure out how to wrap this as a PyLinkedList
            //     PyObject* result = std::visit(
            //         [&key](auto& list) {
            //             return list.slice(key).get();
            //         },
            //         self->variant
            //     );
            //     return Py_XNewRef(result);  // return new reference
            // }

            // unrecognized key type
            PyErr_Format(
                PyExc_TypeError,
                "list indices must be integers or slices, not %s",
                Py_TYPE(key)->tp_name
            );
            return nullptr;

        // translate C++ errors into Python exceptions
        } catch (...) {
            // TODO
            return nullptr;
        }
    }

    /* Implement `LinkedList.__setitem__()/__delitem__()` in Python (slice). */
    inline static int __setitem__(
        PyLinkedList* self,
        PyObject* key,
        PyObject* items
    ) {
        try {
            // check for integer index
            if (PyIndex_Check(key)) {
                PyObject* integer = PyNumber_Index(key);
                if (integer == nullptr) return -1;  // propagate
                long long index = PyLong_AsLongLong(integer);
                Py_DECREF(integer);
                if (index == -1 && PyErr_Occurred()) return -1;  // propagate

                // call scalar C++ method
                std::visit(
                    [&index, &items](auto& list) {
                        if (items == nullptr) {
                            list[index].del();
                        } else {
                            list[index].set(items);
                        }
                    },
                    self->variant
                );
                return 0;
            }

            // check for slice
            if (PySlice_Check(key)) {
                std::visit(
                    [&key, &items](auto& list) {
                        if (items == nullptr) {
                            list.slice(key).del();
                        } else {
                            list.slice(key).set(items);
                        }
                    },
                    self->variant
                );
                return 0;
            }

            // unrecognized key type
            PyErr_Format(
                PyExc_TypeError,
                "list indices must be integers or slices, not %s",
                Py_TYPE(key)->tp_name
            );
            return -1;

        } catch (...) {
            // TODO
            return -1;
        }
    }

    /* Implement `LinkedList.__add__()/__radd__()` (sequence protocol) in Python. */
    inline static PyObject* __add__(PyLinkedList* self, PyObject* other) {
        // TODO: return a new PyLinkedList, properly reference counted.
        return nullptr;
    }

    /* Implement `LinkedList.__iadd__()` (sequence protocol) in Python. */
    inline static PyObject* __iadd__(PyLinkedList* self, PyObject* other) {
        std::visit(
            [&other](auto& list) {
                list += other;
            },
            self->variant
        );
        Py_INCREF(self);
        return reinterpret_cast<PyObject*>(self);
    }

    /* Implement `LinkedList.__mul__()__rmul__()` in Python. */
    inline static PyObject* __mul__(PyLinkedList* self, Py_ssize_t count) {
        // TODO: return a new PyLinkedList, properly reference counted.
        return nullptr;
    }

    /* Implement `LinkedList.__imul__()` in Python. */
    inline static PyObject* __imul__(PyLinkedList* self, Py_ssize_t count) {
        std::visit(
            [&count](auto& list) {
                list *= count;
            },
            self->variant
        );
        Py_INCREF(self);
        return reinterpret_cast<PyObject*>(self);
    }

    /* Implement `LinkedList.__lt__()/__le__()/__eq__()/__ne__()/__ge__()/__gt__()` in
    Python. */
    inline static PyObject* __richcompare__(
        PyLinkedList* self,
        PyObject* other,
        int cmp
    ) {
        try {
            bool result = std::visit(
                [&other, &cmp](auto& list) {
                    switch (cmp) {
                        case Py_LT:
                            return list < other;
                        case Py_LE:
                            return list <= other;
                        case Py_EQ:
                            return list == other;
                        case Py_NE:
                            return list != other;
                        case Py_GE:
                            return list >= other;
                        case Py_GT:
                            return list > other;
                        default:
                            throw util::catch_python<util::type_error>();
                    }
                },
                self->variant
            );
            return PyBool_FromLong(result);
        } catch (...) {
            // TODO
            return nullptr;
        }
    }

    /* Implement `LinkedList.__str__()` in Python. */
    inline static PyObject* __str__(PyLinkedList* self) {
        // TODO: return a formatted string representation of the list
        return nullptr;
    }

    /* Implement `LinkedList.__repr__()` in Python. */
    inline static PyObject* __repr__(PyLinkedList* self) {
         // TODO: return a formatted string representation of the list
         return nullptr;
    }

    ////////////////////////////
    ////    VIEW METHODS    ////
    ////////////////////////////

    // TODO: we could also move most of this over into a mixin that uses CRTP to
    // access the appropriate variant.  We could place this in base.h, alongside
    // LinkedBase.

    /* Getter for `LinkedList.capacity` in Python. */
    inline static PyObject* capacity(PyLinkedList* self, PyObject* /* ignored */) {
        size_t result = std::visit(
            [](auto& list) {
                return list.capacity();
            },
            self->variant
        );
        return PyLong_FromSize_t(result);
    }

    /* Getter for `LinkedList.max_size` in Python. */
    inline static PyObject* max_size(PyLinkedList* self, PyObject* /* ignored */) {
        std::optional<size_t> result = std::visit(
            [](auto& list) {
                return list.max_size();
            },
            self->variant
        );
        if (result.has_value()) {
            return PyLong_FromSize_t(result.value());
        } else {
            Py_RETURN_NONE;
        }
    }

    /* Getter for `LinkedList.dynamic` in Python. */
    inline static PyObject* dynamic(PyLinkedList* self, PyObject* /* ignored */) {
        bool result = std::visit(
            [](auto& list) {
                return list.dynamic();
            },
            self->variant
        );
        return PyBool_FromLong(result);
    }

    /* Getter for `LinkedList.frozen` in Python. */
    inline static PyObject* frozen(PyLinkedList* self, PyObject* /* ignored */) {
        bool result = std::visit(
            [](auto& list) {
                return list.frozen();
            },
            self->variant
        );
        return PyBool_FromLong(result);
    }

    /* Getter for `LinkedList.nbytes` in Python. */
    inline static PyObject* nbytes(PyLinkedList* self, PyObject* /* ignored */) {
        size_t result = std::visit(
            [](auto& list) {
                return list.nbytes();
            },
            self->variant
        );
        return PyLong_FromSize_t(result);
    }

    /* Getter for `LinkedList.specialization` in Python. */
    inline static PyObject* specialization(PyLinkedList* self, PyObject* /* ignored */) {
        PyObject* result = std::visit(
            [](auto& list) {
                return list.specialization();
            },
            self->variant
        );
        Py_XINCREF(result);
        return result;
    }

    // TODO: For reserve() to be available at the Python level, we need to create a
    // Python wrapper like we did with threading locks/iterators.

    // /* Implement `LinkedList.reserve()` in Python. */
    // inline PyObject* reserve(size_t size) {
    //     std::visit(
    //         [&size](auto& list) {
    //             return list.reserve(size);
    //         },
    //         this->variant
    //     );
    // }

    /* Implement `LinkedList.defragment()` in Python. */
    inline static PyObject* defragment(PyLinkedList* self, PyObject* /* ignored */) {
        std::visit(
            [](auto& list) {
                list.defragment();
            },
            self->variant
        );
        Py_RETURN_NONE;  // void
    }

    /* Implement `LinkedList.specialize()` in Python. */
    inline static PyObject* specialize(PyLinkedList* self, PyObject* spec) {
        std::visit(
            [&spec](auto& list) {
                list.specialize(spec);
            },
            self->variant
        );
        Py_RETURN_NONE;  // void
    }

    // /* Implement `LinkedList.__class_getitem__()` in Python. */
    // inline PyObject* __class_getitem__() {
    //     // TODO: Create a new PyTypeObject for the specialized type
    // }

    /* Implement `LinkedList.__len__()` in Python. */
    inline static Py_ssize_t __len__(PyLinkedList* self) noexcept {
        return std::visit(
            [](auto& list) {
                return list.size();
            },
            self->variant
        );
    }

    /* Implement `LinkedList.__bool__()` in Python. */
    inline bool __bool__() const noexcept {
        return std::visit(
            [](auto& list) {
                return !list.empty();
            },
            this->variant
        );
    }

    /* Implement `LinkedList.__iter__()` in Python. */
    inline static PyObject* __iter__(PyLinkedList* self) {
        return std::visit(
            [](auto& list) {
                return util::iter(list).python();
            },
            self->variant
        );
    }

    /* Implement `LinkedList.__reversed__()` in Python. */
    inline static PyObject* __reversed__(PyLinkedList* self) {
        return std::visit(
            [](auto& list) {
                return util::iter(list).rpython();
            },
            self->variant
        );
    }

private:

    // TODO: some of these (like garbage collection) could be centralized in base class
    // along with view methods above.

    /* Allocate and initialize a new LinkedList instance from Python. */
    inline static PyObject* __new__(
        PyTypeObject* type,
        PyObject* args,
        PyObject* kwargs
    ) {
        // parse arguments
        // util::PyArgs pyargs(args, kwargs);

        // allocate
        PyLinkedList* self = reinterpret_cast<PyLinkedList*>(type->tp_alloc(type, 0));
        if (self == nullptr) {
            return nullptr;
        }

        // initialize
        // TODO: use placement new with parsed arguments

        return reinterpret_cast<PyObject*>(self);
    }

    /* Deallocate the LinkedList when its Python reference count falls to zero. */
    inline static void dealloc(PyLinkedList* self) {
        // TODO: figure out how to do this properly
        PyObject_GC_UnTrack(self);
        type.tp_free(reinterpret_cast<PyObject*>(self));
    }

    /* Traversal function for Python's cyclic garbage collector. */
    inline static int gc_traverse(PyLinkedList* self, visitproc visit, void* arg) {
        // TODO: figure out how to do this properly
        return 0;
    }

    /* Clear function for Python's cyclic garbage collector. */
    inline static int gc_clear(PyLinkedList* self) {
        // TODO: figure out how to do this properly
        return 0;
    }

    /* Implement `LinkedList.__getitem__()` in Python (scalar). */
    inline static PyObject* __getitem_scalar__(PyLinkedList* self, Py_ssize_t index) {
        try {
            PyObject* result = std::visit(
                [&index](auto& list) {
                    return list[index].get();
                },
                self->variant
            );
            return Py_NewRef(result);

        // translate C++ errors into Python exceptions
        } catch (...) {
            // TODO
            return nullptr;
        }
    }

    /* Implement `LinkedList.__setitem__()/__delitem__()` in Python (scalar). */
    inline static int __setitem_scalar__(
        PyLinkedList* self,
        Py_ssize_t index,
        PyObject* item
    ) {
        try {
            std::visit(
                [&index, &item](auto& list) {
                    if (item == nullptr) {
                        list[index].del();
                    } else {
                        list[index].set(item);
                    }
                },
                self->variant
            );
            return 0;

        // translate C++ errors into Python exceptions
        } catch (...) {
            // TODO
            return -1;
        }
    }

    /* Vtable containing Python method definitions for the LinkedList. */
    inline static PyMethodDef methods[] = {
        {
            "append",
            (PyCFunction) append,
            METH_FASTCALL | METH_KEYWORDS,
            "docstring for append()"
        },
        {
            "defragment",
            (PyCFunction) defragment,
            METH_NOARGS,
            "docstring for defragment()"
        },
        {
            "specialize",
            (PyCFunction) specialize,
            METH_O,
            "docstring for specialize()"
        },
        {NULL}  // sentinel
    };

    /* Vtable containing Python @property definitions for the LinkedList. */
    inline static PyGetSetDef properties[] = {
        {
            "capacity",
            (getter) capacity,
            NULL,
            "docstring for capacity",
        },
        {
            "max_size",
            (getter) max_size,
            NULL,
            "docstring for max_size"
        },
        {
            "dynamic",
            (getter) dynamic,
            NULL,
            "docstring for dynamic"
        },
        {
            "frozen",
            (getter) frozen,
            NULL,
            "docstring for frozen"
        },
        {
            "nbytes",
            (getter) nbytes,
            NULL,
            "docstring for nbytes"
        },
        {
            "specialization",
            (getter) specialization,
            NULL,
            "docstring for specialization"
        },
        {NULL}  // sentinel
    };

    /* Vtable containing special methods related to Python's mapping protocol. */
    inline static PyMappingMethods mapping = [] {
        PyMappingMethods slots;
        slots.mp_length = (lenfunc) __len__;
        slots.mp_subscript = (binaryfunc) __getitem__;
        slots.mp_ass_subscript = (objobjargproc) __setitem__;
        return slots;
    }();

    /* Vtable containing special methods related to Python's sequence protocol. */
    inline static PySequenceMethods sequence = [] {
        PySequenceMethods slots;
        slots.sq_length = (lenfunc) __len__;
        slots.sq_concat = (binaryfunc) __add__;
        slots.sq_repeat = (ssizeargfunc) __mul__;
        slots.sq_item = (ssizeargfunc) __getitem_scalar__;
        slots.sq_ass_item = (ssizeobjargproc) __setitem_scalar__;
        slots.sq_contains = (objobjproc) __contains__;
        slots.sq_inplace_concat = (binaryfunc) __iadd__;
        slots.sq_inplace_repeat = (ssizeargfunc) __imul__;
        return slots;
    }();

    /* Initialize a PyTypeObject to represent the list in Python. */
    static PyTypeObject init_type() {
        PyTypeObject slots;
        slots.tp_name = "LinkedList";
        slots.tp_basicsize = sizeof(PyLinkedList);
        slots.tp_itemsize = 0;  // TODO: maybe this should be nonzero?
        slots.tp_dealloc = (destructor) dealloc;
        slots.tp_repr = (reprfunc) __repr__;
        slots.tp_as_sequence = &sequence;
        slots.tp_as_mapping = &mapping;
        slots.tp_hash = (hashfunc) PyObject_HashNotImplemented;
        slots.tp_str = (reprfunc) __str__;
        slots.tp_flags = (
            Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_IMMUTABLETYPE |
            Py_TPFLAGS_SEQUENCE
            // add Py_TPFLAGS_MANAGED_WEAKREF for Python 3.12+
        );
        slots.tp_doc = "docstring for LinkedList";
        slots.tp_traverse = (traverseproc) gc_traverse;
        slots.tp_clear = (inquiry) gc_clear;
        slots.tp_richcompare = (richcmpfunc) __richcompare__;
        slots.tp_iter = (getiterfunc) __iter__;
        slots.tp_methods = methods;
        slots.tp_getset = properties;
        slots.tp_new = (newfunc) __new__;
        return slots;
    };

    /* The final Python type. */
    inline static PyTypeObject type = init_type();

};










}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_LIST_H
