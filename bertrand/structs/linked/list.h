#ifndef BERTRAND_STRUCTS_LINKED_LIST_H
#define BERTRAND_STRUCTS_LINKED_LIST_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <ostream>  // std::ostream
#include <sstream>  // std::ostringstream
#include <Python.h>  // CPython API
#include "../util/args.h"  // PyArgs
#include "../util/except.h"  // catch_python
#include "../util/ops.h"  // repr(), lexical comparisons
#include "core/allocate.h"  // Config
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
namespace linked {


template <typename T, unsigned int Flags, typename Lock>
class LinkedList;


namespace list_config {

    template <typename T, unsigned int Flags>
    using NodeSelect = std::conditional_t<
        !!(Flags & Config::SINGLY_LINKED),
        SingleNode<T>,
        DoubleNode<T>
    >;

}


//////////////////////////
////    LINKEDLIST    ////
//////////////////////////


/* A modular linked list class that mimics the Python list interface in C++. */
template <
    typename T,
    unsigned int Flags = Config::DEFAULT,
    typename Lock = BasicLock
>
class LinkedList : public LinkedBase<
    linked::ListView<list_config::NodeSelect<T, Flags>, Flags>,
    Lock
> {
    using Base = LinkedBase<
        linked::ListView<list_config::NodeSelect<T, Flags>, Flags>,
        Lock
    >;
    using DynamicList = LinkedList<T, Flags & ~Config::FIXED_SIZE, Lock>;

public:
    using View = typename Base::View;
    using Value = T;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    using Base::Base;
    using Base::operator=;

    //////////////////////////////
    ////    LIST INTERFACE    ////
    //////////////////////////////

    /* LinkedLists implement the full Python list interface with equivalent semantics to
     * the built-in Python list type, as well as a few addons from `collections.deque`.
     * There are only a few differences:
     *
     *      1.  The append() and extend() methods have corresponding append_left() and
     *          extend_left() counterparts.  These are similar to the appendleft() and
     *          extendleft() methods of `collections.deque`.
     *      2.  The count() method accepts optional `start` and `stop` arguments that
     *          specify a slice of the list to search within.  This is similar to the
     *          index() method of the built-in Python list.
     *      3.  LinkedLists are able to store non-Python C++ types, but only when
     *          declared from C++ or Cython.  LinkedLists are available from Python,
     *          but can only store Python objects when declared from a Python context.
     *
     * Otherwise, everything should behave exactly as expected, with similar overall
     * performance to a built-in Python list (random access limitations of linked lists
     * notwithstanding.)
     */

    /* Add an item to the end of the list. */
    inline void append(const Value& item) {
        linked::append(this->view, item);
    }

    /* Add an item to the beginning of the list. */
    inline void append_left(const Value& item) {
        linked::append_left(this->view, item);
    }

    /* Insert an item at a specified index of the list. */
    inline void insert(long long index, const Value& item) {
        linked::insert(this->view, index, item);
    }

    /* Extend the list by appending elements from an iterable. */
    template <typename Container>
    inline void extend(const Container& items) {
        linked::extend(this->view, items);
    }

    /* Extend the list by left-appending elements from an iterable. */
    template <typename Container>
    inline void extend_left(const Container& items) {
        linked::extend_left(this->view, items);
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
    template <typename Func = PyObject*>
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
     * ElementProxies are returned by the `position()` method and array index
     * operator[] when given a single numeric argument.  This argument can be negative
     * following the same semantics as built-in Python lists (i.e. -1 refers to the
     * last element, and overflow results in an error).  Each proxy offers the
     * following methods:
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

    inline auto position(long long index)
        -> linked::ElementProxy<View, Yield::KEY>
    {
        return linked::position<Yield::KEY>(this->view, index);
    }

    inline auto position(long long index) const
        -> const linked::ElementProxy<const View, Yield::KEY>
    {
        return linked::position<Yield::KEY>(this->view, index);
    }

    template <typename... Args>
    inline auto slice(Args&&... args)
        -> linked::SliceProxy<View, DynamicList, Yield::KEY>
    {
        return linked::slice<DynamicList, Yield::KEY>(
            this->view, std::forward<Args>(args)...
        );
    }

    template <typename... Args>
    inline auto slice(Args&&... args) const
        -> const linked::SliceProxy<const View, DynamicList, Yield::KEY>
    {
        return linked::slice<DynamicList, Yield::KEY>(
            this->view, std::forward<Args>(args)...
        );
    }

    //////////////////////////////////
    ////    OPERATOR OVERLOADS    ////
    //////////////////////////////////

    /* NOTE: The supported operators are as follows:
     *      (+, +=)     concatenation, in-place concatenation
     *      (*, *=)     repetition, in-place repetition
     *      (<)         lexicographic less-than comparison
     *      (<=)        lexicographic less-than-or-equal-to comparison
     *      (==)        lexicographic equality comparison
     *      (!=)        lexicographic inequality comparison
     *      (>=)        lexicographic greater-than-or-equal-to comparison
     *      (>)         lexicographic greater-than comparison
     *
     * These all work similarly to their Python counterparts except that they can
     * accept any iterable container in either C++ or Python as the other operand.
     * This symmetry is provided by the universal utility functions in
     * structs/util/iter.h and structs/util/ops.h.
     */

    inline auto operator[](long long index) {
        return position(index);
    }

    inline auto operator[](long long index) const {
        return position(index);
    }

    template <typename Container>
    inline DynamicList operator+(const Container& other) const {
        return DynamicList(linked::concatenate(this->view, other));
    }

    template <typename Container>
    inline LinkedList& operator+=(const Container& other) {
        extend(other);
        return *this;
    }

    inline DynamicList operator*(long long other) const {
        return DynamicList(linked::repeat(this->view, other));
    }

    inline LinkedList& operator*=(long long other) {
        linked::repeat_inplace(this->view, other);
        return *this;
    }

    template <typename Container>
    inline bool operator<(const Container& other) const {
        return lexical_lt(*this, other);
    }

    template <typename Container>
    inline bool operator<=(const Container& other) const {
        return lexical_le(*this, other);
    }

    template <typename Container>
    inline bool operator==(const Container& other) const {
        return lexical_eq(*this, other);
    }

    template <typename Container>
    inline bool operator!=(const Container& other) const {
        return !lexical_eq(*this, other);
    }

    template <typename Container>
    inline bool operator>=(const Container& other) const {
        return lexical_ge(*this, other);
    }

    template <typename Container>
    inline bool operator>(const Container& other) const {
        return lexical_gt(*this, other);
    }

};


template <typename T, unsigned int Flags, typename... Ts>
inline auto operator<<(std::ostream& stream, const LinkedList<T, Flags, Ts...>& list)
    -> std::ostream&
{
    stream << linked::build_repr(
        list.view,
        "LinkedList",
        "[",
        "]",
        64
    );
    return stream;
}


template <typename T, unsigned int Flags, typename... Ts> 
inline auto operator*(long long other, const LinkedList<T, Flags, Ts...>& list) {
    return list * other;
}


//////////////////////////////
////    PYTHON WRAPPER    ////
//////////////////////////////


/* CRTP mixin class containing the public list interface for a linked data structure. */
template <typename Derived>
class PyListInterface {

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

    static PyObject* append(Derived* self, PyObject* item) {
        return visit(self, [&item](auto& list) {
            list.append(item);
            Py_RETURN_NONE;
        });
    }

    static PyObject* append_left(Derived* self, PyObject* item) {
        return visit(self, [&item](auto& list) {
            list.append_left(item);
            Py_RETURN_NONE;
        });
    }

    static PyObject* insert(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"insert"};
        using bertrand::util::parse_int;
        return visit(self, [&args, &nargs](auto& list) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            long long index = pyargs.parse("index", parse_int);
            PyObject* item = pyargs.parse("item");
            pyargs.finalize();
            list.insert(index, item);
            Py_RETURN_NONE;
        });
    }

    static PyObject* extend(Derived* self, PyObject* items) {
        return visit(self, [&items](auto& list) {
            return unwrap_python(items, [&list](auto& other) {
                list.extend(other);
                Py_RETURN_NONE;
            });
        });
    }

    static PyObject* extend_left(Derived* self, PyObject* items) {
        return visit(self, [&items](auto& list) {
            return unwrap_python(items, [&list](auto& other) {
                list.extend_left(other);
                Py_RETURN_NONE;
            });
        });
    }

    static PyObject* index(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"index"};
        using bertrand::util::parse_opt_int;
        using Index = std::optional<long long>;
        return visit(self, [&args, &nargs](auto& list) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* item = pyargs.parse("item");
            Index start = pyargs.parse("start", parse_opt_int, Index());
            Index stop = pyargs.parse("stop", parse_opt_int, Index());
            pyargs.finalize();
            return PyLong_FromSize_t(list.index(item, start, stop));
        });
    }

    static PyObject* count(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"count"};
        using bertrand::util::parse_opt_int;
        using Index = std::optional<long long>;
        return visit(self, [&args, &nargs](auto& list) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* item = pyargs.parse("item");
            Index start = pyargs.parse("start", parse_opt_int, Index());
            Index stop = pyargs.parse("stop", parse_opt_int, Index());
            pyargs.finalize();
            return PyLong_FromSize_t(list.count(item, start, stop));
        });
    }

    static PyObject* remove(Derived* self, PyObject* item) {
        return visit(self, [&item](auto& list) {
            list.remove(item);
            Py_RETURN_NONE;
        });
    }

    static PyObject* pop(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"pop"};
        using bertrand::util::parse_int;
        return visit(self, [&args, &nargs](auto& list) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            long long index = pyargs.parse("index", parse_int, (long long)-1);
            pyargs.finalize();
            return list.pop(index);  // returns new reference
        });
    }

    static PyObject* clear(Derived* self, PyObject* = nullptr) {
        return visit(self, [](auto& list) {
            list.clear();
            Py_RETURN_NONE;
        });
    }

    static PyObject* copy(Derived* self, PyObject* = nullptr) {
        return visit(self, [](auto& list) {
            return Derived::construct(list.copy());
        });
    }

    static PyObject* sort(
        Derived* self,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) {
        static constexpr std::string_view meth_name{"sort"};
        using bertrand::util::none_to_null;
        using bertrand::util::is_truthy;
        return visit(self, [&args, &nargs, &kwnames](auto& list) {
            PyArgs<CallProtocol::VECTORCALL> pyargs(meth_name, args, nargs, kwnames);
            PyObject* key = pyargs.keyword("key", none_to_null, (PyObject*)nullptr);
            bool reverse = pyargs.keyword("reverse", is_truthy, false);
            pyargs.finalize();
            list.sort(key, reverse);
            Py_RETURN_NONE;
        });
    }

    static PyObject* reverse(Derived* self, PyObject* = nullptr) {
        return visit(self, [](auto& list) {
            list.reverse();
            Py_RETURN_NONE;
        });
    }

    static PyObject* rotate(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"rotate"};
        using bertrand::util::parse_int;
        return visit(self, [&args, &nargs](auto& list) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            long long steps = pyargs.parse("steps", parse_int, (long long)1);
            pyargs.finalize();
            list.rotate(steps);
            Py_RETURN_NONE;
        });
    }

    static int __contains__(Derived* self, PyObject* item) {
        return visit(self, [&item](auto& list) {
            return list.contains(item);
        }, -1);
    }

    static PyObject* __getitem__(Derived* self, PyObject* key) {
        return visit(self, [&key](auto& list) {
            if (PyIndex_Check(key)) {
                long long index = bertrand::util::parse_int(key);
                return Py_NewRef(list.position(index).get());
            }

            if (PySlice_Check(key)) {
                return Derived::construct(list.slice(key).get());
            }

            PyErr_Format(
                PyExc_TypeError,
                "indices must be integers or slices, not %s",
                Py_TYPE(key)->tp_name
            );
            return static_cast<PyObject*>(nullptr);
        });
    }

    static int __setitem__(Derived* self, PyObject* key, PyObject* items) {
        return visit(self, [&key, &items](auto& list) {
            if (PyIndex_Check(key)) {
                long long index = bertrand::util::parse_int(key);
                if (items == nullptr) {
                    list[index].del();
                } else {
                    list[index] = items;
                }
                return 0;
            }

            if (PySlice_Check(key)) {
                if (items == nullptr) {
                    list.slice(key).del();
                } else {
                    unwrap_python(items, [&list, &key](auto& items) {
                        list.slice(key) = items;
                    });
                }
                return 0;
            }

            PyErr_Format(
                PyExc_TypeError,
                "indices must be integers or slices, not %s",
                Py_TYPE(key)->tp_name
            );
            return -1;
        }, -1);
    }

    static PyObject* __add__(Derived* self, PyObject* other) {
        return visit(self, [&other](auto& list) {
            return unwrap_python(other, [&list](auto& other) {
                return Derived::construct(list + other);
            });
        });
    }

    static PyObject* __iadd__(Derived* self, PyObject* other) {
        return visit(self, [&self, &other](auto& list) {
            unwrap_python(other, [&list](auto& other) {
                list += other;
            });
            return Py_NewRef(reinterpret_cast<PyObject*>(self));
        });
    }

    static PyObject* __mul__(Derived* self, Py_ssize_t count) {
        return visit(self, [&count](auto& list) {
            return Derived::construct(list * count);
        });
    }

    static PyObject* __imul__(Derived* self, Py_ssize_t count) {
        return visit(self, [&self, &count](auto& list) {
            list *= count;
            return Py_NewRef(reinterpret_cast<PyObject*>(self));
        });
    }

    static PyObject* __richcompare__(Derived* self, PyObject* other, int cmp) {
        return visit(self, [&other, &cmp](auto& list) {
            return unwrap_python(other, [&list, &cmp](auto& other) {
                switch (cmp) {
                    case Py_LT:
                        return Py_NewRef(list < other ? Py_True : Py_False);
                    case Py_LE:
                        return Py_NewRef(list <= other ? Py_True : Py_False);
                    case Py_EQ:
                        return Py_NewRef(list == other ? Py_True : Py_False);
                    case Py_NE:
                        return Py_NewRef(list != other ? Py_True : Py_False);
                    case Py_GE:
                        return Py_NewRef(list >= other ? Py_True : Py_False);
                    case Py_GT:
                        return Py_NewRef(list > other ? Py_True : Py_False);
                    default:  // should never occur
                        PyErr_SetString(PyExc_TypeError, "invalid comparison");
                        return static_cast<PyObject*>(nullptr);
                }
            });
        });
    }

protected:

    /* Implement `PySequence_GetItem()` in CPython API. */
    static PyObject* __getitem_scalar__(Derived* self, Py_ssize_t index) {
        return visit(self, [&index](auto& list) {
            return Py_NewRef(list.position(index).get());
        });
    }

    /* Implement `PySequence_SetItem()` in CPython API. */
    static int __setitem_scalar__(Derived* self, Py_ssize_t index, PyObject* item) {
        return visit(self, [&index, &item](auto& list) {
            if (item == nullptr) {
                list.position(index).del();
            } else {
                list.position(index).set(item);
            }
            return 0;
        }, -1);
    }

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
Appends are O(1) for both ends of the list.
)doc"
        };

        static constexpr std::string_view append_left {R"doc(
Insert an item at the beginning of the list.

Parameters
----------
item : Any
    The item to insert.

Notes
-----
This method is analogous to the
:meth:`appendleft() <python:collections.deque.appendleft>` method of a
:class:`collections.deque` object.

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

Notes
-----
If an error occurs while extending the list, then the operation will be undone,
returning the list to its original state.

Extends are O(m), where ``m`` is the length of ``items``.
)doc"
        };

        static constexpr std::string_view extend_left {R"doc(
Extend the list by left-appending all the items from the specified iterable.

Parameters
----------
items : Iterable[Any]
    The items to append.

Notes
-----
This method is analogous to the
:meth:`extendleft() <python:collections.deque.extendleft>` method of a
:class:`collections.deque` object.  Just like that method, the series of left
appends results in reversing the order of elements in ``items``.

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


/* A Python type that exposes a discriminated union of C++ LinkedLists to the Python
interpreter. */
class PyLinkedList :
    public PyLinkedBase<PyLinkedList>,
    public PyListInterface<PyLinkedList>
{
    using Base = PyLinkedBase<PyLinkedList>;
    using IList = PyListInterface<PyLinkedList>;

    /* A std::variant representing all of the LinkedList implementations that are
    constructable from Python. */
    template <unsigned int Flags>
    using ListConfig = linked::LinkedList<PyObject*, Flags, BasicLock>;
    using Variant = std::variant<
        ListConfig<Config::DEFAULT>,
        ListConfig<Config::STRICTLY_TYPED>,
        ListConfig<Config::FIXED_SIZE>,
        ListConfig<Config::FIXED_SIZE | Config::STRICTLY_TYPED>,
        ListConfig<Config::SINGLY_LINKED>,
        ListConfig<Config::SINGLY_LINKED | Config::STRICTLY_TYPED>,
        ListConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE>,
        ListConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::STRICTLY_TYPED>
    >;
    template <size_t I>
    using Alt = typename std::variant_alternative_t<I, Variant>;

    friend Base;
    friend IList;
    Variant variant;

    /* Construct a PyLinkedList around an existing C++ LinkedList. */
    template <typename List>
    inline void from_cpp(List&& list) {
        new (&variant) Variant(std::forward<List>(list));
    }

    /* Parse the configuration code and initialize the variant with the forwarded
    arguments. */
    template <typename... Args>
    static void build_variant(unsigned int code, PyLinkedList* self, Args&&... args) {
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

    /* Translate Python constructor arguments into a specific template configuration
    and initialize the variant accordingly. */
    static void initialize(
        PyLinkedList* self,
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

    static int __init__(PyLinkedList* self, PyObject* args, PyObject* kwargs) {
        static constexpr std::string_view meth_name{"__init__"};
        using bertrand::util::none_to_null;
        using bertrand::util::is_truthy;
        using bertrand::util::parse_int;
        try {
            PyArgs<CallProtocol::KWARGS> pyargs(meth_name, args, kwargs);
            PyObject* iterable = pyargs.parse(
                "iterable", none_to_null, (PyObject*) nullptr
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

            initialize(
                self, iterable, max_size, spec, reverse, singly_linked, false
            );

            return 0;

        } catch (...) {
            throw_python();
            return -1;
        }
    }

    static PyObject* __str__(PyLinkedList* self) {
        return Base::visit(self, [](auto& list) {
            std::ostringstream stream;
            stream << "[";
            auto it = list.begin();
            auto end = list.end();
            if (it != end) {
                stream << repr(*it);
                ++it;
            }
            while (it != list.end()) {
                stream << ", " << repr(*it);
                ++it;
            }
            stream << "]";
            auto str = stream.str();
            return PyUnicode_FromStringAndSize(str.c_str(), str.size());
        });
    }

private:

    struct docs {

        static constexpr std::string_view LinkedList {R"doc(
A modular linked list available in both Python and C++.

This class is a drop-in replacement for a built-in :class:`list` or
:class:`collections.deque` object, supporting all the same operations.  It is
also available as a C++ type under the same name, with identical semantics.

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
    disables type checking for the list.  See the :meth:`specialize()` method
    for more details.
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
These data structures are highly optimized, and offer performance that is
generally on par with the built-in :class:`list` and :class:`collections.deque`
types.  They retain the usual tradeoffs of linked lists vs arrays (e.g. random
access, vs constant-time insertions, etc.), but attempt to minimize compromises
wherever possible.  Users should not notice a significant difference on average.

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
        std::vector<int> items{1, 2, 3, 4, 5};
        bertrand::LinkedList<int> list(items);

        list.append(6);
        list.extend(std::vector<int>{7, 8, 9});
        int x = list.pop();
        list.rotate(4);
        list[0] = x;
        for (int i : list) {
            // ...
        }

        std::cout << list;  // LinkedList([9, 6, 7, 8, 1, 2, 3, 4])
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

    #define BASE_PROPERTY(NAME) \
        { #NAME, (getter) Base::NAME, NULL, PyDoc_STR(Base::docs::NAME.data()) } \

    #define BASE_METHOD(NAME, ARG_PROTOCOL) \
        { #NAME, (PyCFunction) Base::NAME, ARG_PROTOCOL, PyDoc_STR(Base::docs::NAME.data()) } \

    #define LIST_METHOD(NAME, ARG_PROTOCOL) \
        { #NAME, (PyCFunction) IList::NAME, ARG_PROTOCOL, PyDoc_STR(IList::docs::NAME.data()) } \

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
        LIST_METHOD(append, METH_O),
        LIST_METHOD(append_left, METH_O),
        LIST_METHOD(insert, METH_FASTCALL),
        LIST_METHOD(extend, METH_O),
        LIST_METHOD(extend_left, METH_O),
        LIST_METHOD(index, METH_FASTCALL),
        LIST_METHOD(count, METH_FASTCALL),
        LIST_METHOD(remove, METH_O),
        LIST_METHOD(pop, METH_FASTCALL),
        LIST_METHOD(clear, METH_NOARGS),
        LIST_METHOD(copy, METH_NOARGS),
        LIST_METHOD(sort, METH_FASTCALL | METH_KEYWORDS),
        LIST_METHOD(reverse, METH_NOARGS),
        LIST_METHOD(rotate, METH_FASTCALL),
        {NULL}
    };

    #undef BASE_PROPERTY
    #undef BASE_METHOD
    #undef LIST_METHOD

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
        slots.sq_concat = (binaryfunc) IList::__add__;
        slots.sq_repeat = (ssizeargfunc) IList::__mul__;
        slots.sq_item = (ssizeargfunc) IList::__getitem_scalar__;
        slots.sq_ass_item = (ssizeobjargproc) IList::__setitem_scalar__;
        slots.sq_contains = (objobjproc) IList::__contains__;
        slots.sq_inplace_concat = (binaryfunc) IList::__iadd__;
        slots.sq_inplace_repeat = (ssizeargfunc) IList::__imul__;
        return slots;
    }();

    static PyTypeObject build_type() {
        return {
            .ob_base = PyObject_HEAD_INIT(NULL)
            .tp_name = "bertrand.LinkedList",
            .tp_basicsize = sizeof(PyLinkedList),
            .tp_itemsize = 0,
            .tp_dealloc = (destructor) Base::__dealloc__,
            .tp_repr = (reprfunc) Base::__repr__,
            .tp_as_sequence = &sequence,
            .tp_as_mapping = &mapping,
            .tp_hash = (hashfunc) PyObject_HashNotImplemented,
            .tp_str = (reprfunc) __str__,
            .tp_flags = (
                Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC |
                Py_TPFLAGS_IMMUTABLETYPE | Py_TPFLAGS_SEQUENCE
                // add Py_TPFLAGS_MANAGED_WEAKREF for Python 3.12+
            ),
            .tp_doc = PyDoc_STR(docs::LinkedList.data()),
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

    template <typename List>
    static PyObject* construct(List&& list) {
        PyLinkedList* result = reinterpret_cast<PyLinkedList*>(
            Type.tp_new(&Type, nullptr, nullptr)
        );
        if (result == nullptr) {
            return nullptr;
        }

        try {
            result->from_cpp(std::forward<List>(list));
            return reinterpret_cast<PyObject*>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Check whether another PyObject* is of this type. */
    static bool typecheck(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &Type);
        if (result == -1) {
            throw catch_python();
        }
        return static_cast<bool>(result);
    }

};


}  // namespace linked


using linked::LinkedList;
using linked::PyLinkedList;


}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_LIST_H
