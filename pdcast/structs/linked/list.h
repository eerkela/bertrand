// include guard: BERTRAND_STRUCTS_LINKED_LIST_H
#ifndef BERTRAND_STRUCTS_LINKED_LIST_H
#define BERTRAND_STRUCTS_LINKED_LIST_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <ostream>  // std::ostream
#include <sstream>  // std::ostringstream
#include <Python.h>  // CPython API
#include "../util/args.h"  // PyArgs
#include "../util/except.h"  // catch_python
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
#include "algorithms/repr.h"


namespace bertrand {
namespace structs {
namespace linked {


/* A modular linked list class that mimics the Python list interface in C++. */
template <typename NodeType, typename LockPolicy = util::BasicLock>
class LinkedList : public LinkedBase<linked::ListView<NodeType>, LockPolicy> {
    using Base = LinkedBase<linked::ListView<NodeType>, LockPolicy>;

public:
    using View = linked::ListView<NodeType>;
    using Value = typename View::Node::Value;

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


/////////////////////////////////////
////    STRING REPRESENTATION    ////
/////////////////////////////////////


/* Override the << operator to print the abbreviated contents of a list to an output
stream (equivalent to Python repr()). */
template <typename... Ts>
std::ostream& operator<<(std::ostream& stream, const LinkedList<Ts...>& list) {
    stream << linked::repr(
        list.view,
        "LinkedList",
        "[",
        "]",
        64
    );
    return stream;
}


/////////////////////////////
////    CONCATENATION    ////
/////////////////////////////


/* Concatenate a LinkedList with an arbitrary C++/Python container to produce a new
list. */
template <typename Container, typename... Ts>
LinkedList<Ts...> operator+(const LinkedList<Ts...>& lhs, const Container& rhs) {
    return linked::concatenate(lhs.view, rhs);
}


/* Concatenate a LinkedList with an arbitrary C++/Python container in-place. */
template <typename Container, typename... Ts>
LinkedList<Ts...>& operator+=(LinkedList<Ts...>& lhs, const Container& rhs) {
    linked::extend(lhs.view, rhs, false);
    return lhs;
}


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


//////////////////////////////
////    PYTHON WRAPPER    ////
//////////////////////////////


/* CRTP mixin class that contains the Python list interface for a linked data
structure. */
template <typename Derived>
class PyListInterface {
public:

    /* Implement `LinkedList.append()` in Python. */
    static PyObject* append(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) {
        using Args = util::PyArgs<util::CallProtocol::VECTORCALL>;
        try {
            // parse arguments
            Args pyargs(args, nargs, kwnames);
            PyObject* item = pyargs.parse("item");
            bool left = pyargs.parse("left", util::is_truthy, false);
            pyargs.finalize();

            // invoke equivalent C++ method
            std::visit(
                [&item, &left](auto& list) {
                    list.append(item, left);
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

    /* Implement `LinkedList.insert()` in Python. */
    static PyObject* insert(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) {
        using Args = util::PyArgs<util::CallProtocol::VECTORCALL>;
        try {
            // parse arguments
            Args pyargs(args, nargs, kwnames);
            long long index = pyargs.parse("index", util::parse_int);
            PyObject* item = pyargs.parse("item");
            pyargs.finalize();

            // invoke equivalent C++ method
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
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedList.extend()` in Python. */
    static PyObject* extend(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) {
        using Args = util::PyArgs<util::CallProtocol::VECTORCALL>;
        try {
            // parse arguments
            Args pyargs(args, nargs, kwnames);
            PyObject* items = pyargs.parse("items");
            bool left = pyargs.parse("left", util::is_truthy, false);
            pyargs.finalize();

            // invoke equivalent C++ method
            std::visit(
                [&items, &left](auto& list) {
                    list.extend(items, left);
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

    /* Implement `LinkedList.index()` in Python. */
    static PyObject* index(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) {
        using Args = util::PyArgs<util::CallProtocol::VECTORCALL>;
        using Index = std::optional<long long>;
        try {
            // parse arguments
            Args pyargs(args, nargs, kwnames);
            PyObject* item = pyargs.parse("item");
            Index start = pyargs.parse("start", util::parse_opt_int, Index());
            Index stop = pyargs.parse("stop", util::parse_opt_int, Index());
            pyargs.finalize();

            // invoke equivalent C++ method
            size_t result = std::visit(
                [&item, &start, &stop](auto& list) {
                    return list.index(item, start, stop);
                },
                self->variant
            );

            // return as Python integer
            return PyLong_FromSize_t(result);

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedList.count()` in Python. */
    static PyObject* count(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) {
        using Args = util::PyArgs<util::CallProtocol::VECTORCALL>;
        using Index = std::optional<long long>;
        try {
            // parse arguments
            Args pyargs(args, nargs, kwnames);
            PyObject* item = pyargs.parse("item");
            Index start = pyargs.parse("start", util::parse_opt_int, Index());
            Index stop = pyargs.parse("stop", util::parse_opt_int, Index());
            pyargs.finalize();

            // invoke equivalent C++ method
            size_t result = std::visit(
                [&item, &start, &stop](auto& list) {
                    return list.count(item, start, stop);
                },
                self->variant
            );

            // return as Python integer
            return PyLong_FromSize_t(result);

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedList.remove()` in Python. */
    static PyObject* remove(Derived* self, PyObject* item) {
        try {
            // invoke equivalent C++ method
            std::visit(
                [&item](auto& list) {
                    list.remove(item);
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

    /* Implement `LinkedList.pop()` in Python. */
    static PyObject* pop(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        using Args = util::PyArgs<util::CallProtocol::FASTCALL>;
        try {
            // parse arguments
            Args pyargs(args, nargs);
            long long index = pyargs.parse(
                "index", util::parse_int, (long long) -1
            );
            pyargs.finalize();

            // invoke equivalent C++ method
            return std::visit(
                [&index](auto& list) {
                    return list.pop(index);  // returns new reference
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedList.clear()` in Python. */
    static PyObject* clear(Derived* self, PyObject* /* ignored */) {
        try {
            // invoke equivalent C++ method
            std::visit(
                [](auto& list) {
                    list.clear();
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

    /* Implement `LinkedList.copy()` in Python. */
    static PyObject* copy(Derived* self, PyObject* /* ignored */) {
        // allocate new Python list
        Derived* result = reinterpret_cast<Derived*>(
            Derived::__new__(&Derived::Type, nullptr, nullptr)
        );
        if (result == nullptr) return nullptr;  // propagate

        // delegate to equivalent C++ method
        try {
            return std::visit(
                [&result](auto& list) {
                    result->from_cpp(list.copy());
                    return reinterpret_cast<PyObject*>(result);
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            Py_DECREF(result);
            return nullptr;
        }
    }

    /* Implement `LinkedList.sort()` in Python. */
    static PyObject* sort(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) {
        using Args = util::PyArgs<util::CallProtocol::VECTORCALL>;
        try {
            // parse arguments
            Args pyargs(args, nargs, kwnames);
            if (pyargs.positional() != 0) {
                PyErr_Format(
                    PyExc_TypeError,
                    "sort() takes no positional arguments",
                    pyargs.positional()
                );
                return nullptr;
            }
            PyObject* key = pyargs.parse("key", util::none_to_null, (PyObject*) nullptr);
            bool reverse = pyargs.parse("reverse", util::is_truthy, false);
            pyargs.finalize();

            // invoke equivalent C++ method
            std::visit(
                [&key, &reverse](auto& list) {
                    list.sort(key, reverse);
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

    /* Implement `LinkedList.reverse()` in Python. */
    static PyObject* reverse(Derived* self, PyObject* /* ignored */) {
        try {
            // invoke equivalent C++ method
            std::visit(
                [](auto& list) {
                    list.reverse();
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

    /* Implement `LinkedList.rotate()` in Python. */
    static PyObject* rotate(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        using Args = util::PyArgs<util::CallProtocol::FASTCALL>;
        try {
            // parse arguments
            Args pyargs(args, nargs);
            long long steps = pyargs.parse("steps", util::parse_int, (long long) 1);
            pyargs.finalize();

            // invoke equivalent C++ method
            std::visit(
                [&steps](auto& list) {
                    list.rotate(steps);
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

    /* Implement `LinkedList.__contains__()` in Python. */
    static int __contains__(Derived* self, PyObject* item) {
        try {
            // invoke equivalent C++ method
            return std::visit(
                [&item](auto& list) {
                    return list.contains(item);
                },
                self->variant
            );

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return -1;
        }
    }

    /* Implement `LinkedList.__getitem__()` in Python. */
    static PyObject* __getitem__(Derived* self, PyObject* key) {
        try {
            // check for integer index
            if (PyIndex_Check(key)) {
                long long index = util::parse_int(key);
                PyObject* result = std::visit(
                    [&index](auto& list) {
                        return list[index].get();
                    },
                    self->variant
                );
                return Py_XNewRef(result);  // return borrowed reference
            }

            // check for slice
            if (PySlice_Check(key)) {
                Derived* result = reinterpret_cast<Derived*>(
                    Derived::__new__(&Derived::Type, nullptr, nullptr)
                );
                if (result == nullptr) throw util::catch_python();
                return std::visit(
                    [&result, &key](auto& list) {
                        result->from_cpp(list.slice(key).get());
                        return reinterpret_cast<PyObject*>(result);
                    },
                    self->variant
                );
            }

            // unrecognized key type
            PyErr_Format(
                PyExc_TypeError,
                "list indices must be integers or slices, not %s",
                Py_TYPE(key)->tp_name
            );
            return nullptr;

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedList.__setitem__()/__delitem__()` in Python (slice). */
    static int __setitem__(Derived* self, PyObject* key, PyObject* items) {
        try {
            // check for integer index
            if (PyIndex_Check(key)) {
                long long index = util::parse_int(key);
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
            util::throw_python();
            return -1;
        }
    }

    /* Implement `LinkedList.__add__()` in Python. */
    static PyObject* __add__(Derived* self, PyObject* other) {
        // allocate new Python list
        Derived* result = reinterpret_cast<Derived*>(
            Derived::__new__(&Derived::Type, nullptr, nullptr)
        );
        if (result == nullptr) return nullptr;  // propagate

        // delegate to equivalent C++ operator
        try {
            return std::visit(
                [&other, &result](auto& list) {
                    result->from_cpp(list + other);
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

    /* Implement `LinkedList.__iadd__()` in Python. */
    static PyObject* __iadd__(Derived* self, PyObject* other) {
        try {
            std::visit(
                [&other](auto& list) {
                    list += other;
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

    /* Implement `LinkedList.__mul__()__rmul__()` in Python. */
    static PyObject* __mul__(Derived* self, Py_ssize_t count) {
        Derived* result = reinterpret_cast<Derived*>(
            Derived::__new__(&Derived::Type, nullptr, nullptr)
        );
        if (result == nullptr) return nullptr;  // propagate

        // delegate to equivalent C++ operator
        try {
            return std::visit(
                [&count, &result](auto& list) {
                    result->from_cpp(list * count);
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

    /* Implement `LinkedList.__imul__()` in Python. */
    static PyObject* __imul__(Derived* self, Py_ssize_t count) {
        try {
            std::visit(
                [&count](auto& list) {
                    list *= count;
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

    /* Implement `LinkedList.__lt__()/__le__()/__eq__()/__ne__()/__ge__()/__gt__()` in
    Python. */
    static PyObject* __richcompare__(Derived* self, PyObject* other, int cmp) {
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
                            throw util::ValueError("invalid comparison operator");
                    }
                },
                self->variant
            );
            return PyBool_FromLong(result);
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

protected:

    /* Implement `PySequence_GetItem()` in CPython API. */
    static PyObject* __getitem_scalar__(Derived* self, Py_ssize_t index) {
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
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `PySequence_SetItem()` in CPython API. */
    static int __setitem_scalar__(
        Derived* self,
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
            util::throw_python();
            return -1;
        }
    }

    /* docstrings for public Python attributes. */
    struct docs {

        static constexpr std::string_view append {R"doc(
Insert an item at the end of the list.

Parameters
----------
item : Any
    The item to insert.
left : bool, default False
    If True, insert the item at the beginning of the list instead of the end.

Notes
-----
If ``left=True``, this method behaves like the
:meth:`appendleft() <python:collections.deque.appendleft>` method of
:class:`collections.deque`.

Appends are O(1) for both ends of the list.
)doc"
        };

        static constexpr std::string_view insert {R"doc(
Insert an item at the specified index of the list.

Parameters
----------
index : int
    The index at which to insert the item.  This can be negative, following the
    same conventions as Python's built-in list indexing.
item : Any
    The item to insert.

Notes
-----
Insertions are O(n) on average, halved to O(n/2) if the list is doubly-linked.
)doc"
        };

        static constexpr std::string_view extend {R"doc(
Extend the list by appending all the items from the specified iterable.

Parameters
----------
items : Iterable[Any]
    The items to append.
left : bool, default False
    If True, insert the items at the beginning of the list instead of the end.

Notes
-----
If ``left=True``, this method behaves like the
:meth:`extendleft() <python:collections.deque.extendleft>` method of
:class:`collections.deque`.  Just like that method, the series of left appends
results in reversing the order of elements in ``items``.

If an error occurs while extending the list, then the operation will be undone,
returning the list to its original state.

Extends are O(m), where ``m`` is the length of ``items``.
)doc"
        };

        static constexpr std::string_view index {R"doc(
Get the index of an item within the list.

Parameters
----------
item : Any
    The item to search for.
start : int, default None
    The index at which to start searching.  If not specified, the search will
    start at the beginning of the list.
stop : int, default None
    The index at which to stop searching.  If not specified, the search will
    continue until the end of the list.

Returns
-------
int
    The index of the first occurrence of ``item`` within the list.

Raises
------
ValueError
    If ``item`` is not found within the list.
IndexError
    If ``stop`` is less than ``start``.

Notes
-----
The ``start`` and ``stop`` indices can be negative, following the same
conventions as Python's built-in list indexing.  However, the stop index must
always be greater than or equal to the start index, otherwise an error will be
raised.

Indexing is O(n) on average.
)doc"
        };

        static constexpr std::string_view count {R"doc(
Count the number of occurrences of an item within the list.

Parameters
----------
item : Any
    The item to search for.
start : int, default None
    The index at which to start searching.  If not specified, the search will
    start at the beginning of the list.
stop : int, default None
    The index at which to stop searching.  If not specified, the search will
    continue until the end of the list.

Returns
-------
int
    The number of occurrences of ``item`` within the given range.

Raises
------
IndexError
    If ``stop`` is less than ``start``.

Notes
-----
The ``start`` and ``stop`` indices can be negative, following the same
conventions as Python's built-in list indexing.  However, the stop index must
always be greater than or equal to the start index, otherwise an error will be
raised.

Counting is O(n).
)doc"
        };

        static constexpr std::string_view remove {R"doc(
Remove the first occurrence of an item from the list.

Parameters
----------
item : Any
    The item to remove.

Raises
------
ValueError
    If ``item`` is not found within the list.

Notes
-----
Removals are O(n) on average.
)doc"
        };

        static constexpr std::string_view pop {R"doc(
Remove and return the item at the specified index.

Parameters
----------
index : int, default -1
    The index of the item to remove.  If not specified, the last item will be
    removed.

Returns
-------
Any
    The item that was removed.

Raises
------
IndexError
    If the list is empty or if ``index`` is out of bounds.

Notes
-----
Pops have different performance characteristics based on whether they occur at
the front or back of the list.  Popping from the front of a list is O(1) for
both singly- and doubly-linked lists.  Popping from the back, however, is only
O(1) for doubly-linked lists.  It is O(n) for singly-linked lists because the
whole list must be traversed to find the new tail.

Pops towards the middle of the list are O(n) in both cases.
)doc"
        };

        static constexpr std::string_view clear {R"doc(
Remove all items from the list in-place.

Notes
-----
Clearing is O(n).
)doc"
        };

        static constexpr std::string_view copy {R"doc(
Return a shallow copy of the list.

Returns
-------
LinkedList
    A new list containing the same items.

Notes
-----
Copying is O(n).
)doc"
        };

        static constexpr std::string_view sort {R"doc(
Sort the list in-place.

Parameters
----------
key : Callable, optional
    A function that takes an item from the list and returns a value to use
    during sorting.  If this is not given, then the items will be compared
    directly via the ``<`` operator.
reverse : bool, default False
    If True, sort the list in descending order.  Otherwise, sort in ascending
    order.

Notes
-----
Sorting is O(n log n), using an iterative merge sort algorithm that avoids
recursion.  The sort is stable, meaning that the relative order of items that
compare equal will not change, and it is performed in-place for minimal memory
overhead.

If a ``key`` function is provided, then the keys will be computed once and
reused for all iterations of the sorting algorithm.  Otherwise, each element
will be compared directly using the ``<`` operator.  If ``reverse=True``, then
the value of the comparison will be inverted (i.e. ``not a < b``).

One quirk of this implementation is how it handles errors.  By default, if a
comparison throws an exception, then the sort will be aborted and the list will
be left in a partially-sorted state.  This is consistent with the behavior of
Python's built-in :meth:`list.sort() <python:list.sort>` method.  However, when
a ``key`` function is provided, we actually end up sorting an auxiliary list of
``(key, value)`` pairs, which is then reflected in the original list.  This
means that if a comparison throws an exception, the original list will not be
changed.  This holds even if the ``key`` is a simple identity function
(``lambda x: x``), which opens up the possibility of anticipating errors and
handling them gracefully.
)doc"
        };

        static constexpr std::string_view reverse {R"doc(
Reverse the order of items in the list in-place.

Notes
-----
Reversing a list is O(n) for both singly- and doubly-linked lists.
)doc"
        };

        static constexpr std::string_view rotate {R"doc(
Rotate each item to the right by the specified number of steps.

Parameters
----------
steps : int, default 1
    The number of steps to rotate the list.  If this is positive, the list will
    be rotated to the right.  If this is negative, the list will be rotated to
    the left.

Notes
-----
This method is consistent with :meth:`collections.deque.rotate`.

Rotations are O(steps).
)doc"
        };

    };

};


/* A discriminated union of templated `LinkedList` types that can be used from
Python. */
class PyLinkedList :
    public PyLinkedBase<PyLinkedList>,
    public PyListInterface<PyLinkedList>
{
    using Base = PyLinkedBase<PyLinkedList>;
    using IList = PyListInterface<PyLinkedList>;

    /* A std::variant representing all the LinkedList implementations that are
    constructable from Python. */
    using SingleList = LinkedList<SingleNode<PyObject*>, util::BasicLock>;
    using DoubleList = LinkedList<DoubleNode<PyObject*>, util::BasicLock>;
    using Variant = std::variant<
        SingleList,
        DoubleList
    >;

    friend Base;
    friend IList;
    Variant variant;

    /* Construct a PyLinkedList around an existing C++ LinkedList. */
    template <typename List>
    inline void from_cpp(List&& list) {
        new (&variant) Variant(std::forward<List>(list));
    }

public:

    /* Initialize a LinkedList instance from Python. */
    static int __init__(
        PyLinkedList* self,
        PyObject* args,
        PyObject* kwargs
    ) {
        using Args = util::PyArgs<util::CallProtocol::KWARGS>;
        using util::ValueError;
        try {
            // parse arguments
            Args pyargs(args, kwargs);
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
            if (iterable == nullptr) {
                if (singly_linked) {
                    new (&self->variant) Variant(SingleList(max_size, spec));
                } else {
                    new (&self->variant) Variant(DoubleList(max_size, spec));
                }
            } else {
                if (singly_linked) {
                    new (&self->variant) Variant(
                        SingleList(iterable, max_size, spec, reverse)
                    );
                } else {
                    new (&self->variant) Variant(
                        DoubleList(iterable, max_size, spec, reverse)
                    );
                }
            }

            // exit normally
            return 0;

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return -1;
        }
    }

    /* Implement `LinkedList.__str__()` in Python. */
    static PyObject* __str__(PyLinkedList* self) {
        try {
            std::ostringstream stream;
            stream << "[";
            std::visit(
                [&stream](auto& list) {
                    auto it = list.begin();
                    if (it != list.end()) {
                        stream << util::repr(*it);
                        ++it;
                    }
                    for (; it != list.end(); ++it) {
                        stream << ", " << util::repr(*it);
                    }
                },
                self->variant
            );
            stream << "]";
            return PyUnicode_FromString(stream.str().c_str());

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedList.__repr__()` in Python. */
    static PyObject* __repr__(PyLinkedList* self) {
        try {
            std::ostringstream stream;
            std::visit(
                [&stream](auto& list) {
                    stream << list;
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

        static constexpr std::string_view LinkedList {R"doc(
A doubly-linked list.

This class is a drop-in replacement for a built-in :class:`list` or
:class:`collections.deque` object, supporting all the same operations.  It is
also available in C++ under the same name, with equivalent semantics.

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
on par with the built-in :class:`list` and :class:`collections.deque` types.
They retains the usual tradeoffs of linked lists vs arrays (e.g. random access,
vs constant-time insertions, etc.), but attempts to minimize compromises
wherever possible.  Users should not notice a significant difference on
average.

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
        LIST_METHOD(append, METH_FASTCALL | METH_KEYWORDS),
        LIST_METHOD(insert, METH_FASTCALL | METH_KEYWORDS),
        LIST_METHOD(extend, METH_FASTCALL | METH_KEYWORDS),
        LIST_METHOD(index, METH_FASTCALL | METH_KEYWORDS),
        LIST_METHOD(count, METH_FASTCALL | METH_KEYWORDS),
        LIST_METHOD(remove, METH_O),
        LIST_METHOD(pop, METH_FASTCALL),
        LIST_METHOD(clear, METH_NOARGS),
        LIST_METHOD(copy, METH_NOARGS),
        LIST_METHOD(sort, METH_FASTCALL | METH_KEYWORDS),
        LIST_METHOD(reverse, METH_NOARGS),
        LIST_METHOD(rotate, METH_FASTCALL),
        {NULL}  // sentinel
    };

    #undef PROPERTY
    #undef BASE_METHOD
    #undef LIST_METHOD

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
    static PyTypeObject build_type() {
        return {
            .ob_base = PyObject_HEAD_INIT(NULL)
            .tp_name = "bertrand.structs.LinkedList",
            .tp_basicsize = sizeof(PyLinkedList),
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
            .tp_doc = PyDoc_STR(docs::LinkedList.data()),
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
static struct PyModuleDef module_ = {
    PyModuleDef_HEAD_INIT,
    .m_name = "list",
    .m_doc = (
        "This module contains an optimized LinkedList data structure for use "
        "in Python.  The exact same data structure is also available in C++ "
        "under the same header path (bertrand/structs/linked/list.h)."
    ),
    .m_size = -1,
};


/* Python import hook. */
PyMODINIT_FUNC PyInit_list(void) {
    // initialize type objects
    if (PyType_Ready(&PyLinkedList::Type) < 0) return nullptr;

    // initialize module
    PyObject* mod = PyModule_Create(&module_);
    if (mod == nullptr) return nullptr;

    // link type to module
    Py_INCREF(&PyLinkedList::Type);
    if (PyModule_AddObject(mod, "LinkedList", (PyObject*) &PyLinkedList::Type) < 0) {
        Py_DECREF(&PyLinkedList::Type);
        Py_DECREF(mod);
        return nullptr;
    }

    return mod;
}


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_LIST_H
