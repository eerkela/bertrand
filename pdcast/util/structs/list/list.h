// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_LIST_LIST_H
#define BERTRAND_STRUCTS_LIST_LIST_H

// TODO: additional includes as necessary

#include <sstream>  // std::ostringstream
#include <stack>  // std::stack

#include "linked/util.h"
#include "linked/view.h"
#include "linked/sort.h"
#include "linked/algorithms/append.h"
#include "linked/algorithms/clear.h"
#include "linked/algorithms/contains.h"
#include "linked/algorithms/count.h"
#include "linked/algorithms/extend.h"
#include "linked/algorithms/index.h"
#include "linked/algorithms/insert.h"
#include "linked/algorithms/pop.h"
#include "linked/algorithms/position.h"
#include "linked/algorithms/remove.h"
#include "linked/algorithms/reverse.h"
#include "linked/algorithms/rotate.h"
#include "linked/algorithms/slice.h"

#include "base.h"  // LinkedBase


/* Namespaces reflect file system and Python import path. */
namespace bertrand {
namespace structs {


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


template <typename Derived>
class Concatenateable;


template <typename Derived>
class Repeatable;


template <typename Derived>
class Lexicographic;


//////////////////////
////    PUBLIC    ////
//////////////////////


/* Name of the equivalent Python class, to form dotted names for Python iterators. */
inline constexpr std::string_view linked_list_name { "LinkedList" };


/* Namespace alias for generic list methods/operators. */
namespace IList = algorithms::list;


/* A modular linked list class that mimics the Python list interface in C++. */
template <
    typename NodeType = DoubleNode<PyObject*>,
    typename SortPolicy = MergeSort,
    typename LockPolicy = BasicLock
>
class LinkedList :
    public LinkedBase<ListView<NodeType>, LockPolicy, linked_list_name>,
    public Concatenateable<LinkedList<NodeType, SortPolicy, LockPolicy>>,
    public Repeatable<LinkedList<NodeType, SortPolicy, LockPolicy>>,
    public Lexicographic<LinkedList<NodeType, SortPolicy, LockPolicy>>
{
public:
    using View = ListView<NodeType>;
    using Node = typename View::Node;
    using Value = typename Node::Value;
    using Base = LinkedBase<View, LockPolicy, linked_list_name>;
    using Self = LinkedList<NodeType, SortPolicy, LockPolicy>;
    static constexpr std::string_view name { linked_list_name };

    // TODO: type aliases for Iterator, doubly_linked, etc.

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
        bool reverse = false,
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr
    ) : Base(iterable, reverse, max_size, spec)
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

    /* Append an item to the end of a list. */
    inline void append(PyObject* item, bool left = false) {
        IList::append(this->view, item, left);
    }

    /* Insert an item into a list at the specified index. */
    template <typename T>
    inline void insert(T index, PyObject* item) {
        IList::insert(this->view, index, item);
    }

    /* Extend a list by appending elements from the iterable. */
    inline void extend(PyObject* items, bool left = false) {
        IList::extend(this->view, items, left);
    }

    /* Get the index of an item within a list. */
    template <typename T>
    inline size_t index(PyObject* item, T start = 0, T stop = -1) const {
        return IList::index(this->view, item, start, stop);
    }

    /* Count the number of occurrences of an item within a list. */
    template <typename T>
    inline size_t count(PyObject* item, T start = 0, T stop = -1) const {
        return IList::count(this->view, item, start, stop);
    }

    /* Check if the list contains a certain item. */
    inline bool contains(PyObject* item) const {
        return IList::contains(this->view, item);
    }

    /* Remove the first occurrence of an item from a list. */
    inline void remove(PyObject* item) {
        IList::remove(this->view, item);
    }

    /* Remove an item from a list and return its value. */
    template <typename T>
    inline PyObject* pop(T index) {
        return IList::pop(this->view, index);
    }

    /* Remove all elements from a list. */
    inline void clear() {
        IList::clear(this->view);
    }

    /* Return a shallow copy of the list. */
    inline Self copy() const {
        return Self(this->view.copy());
    }

    /* Sort a list in-place. */
    template <typename Func>
    void sort(Func key = nullptr, bool reverse = false) {
        SortFunc<SortPolicy, Func>::sort(this->view, key, reverse);
    }

    /* Reverse a list in-place. */
    void reverse() {
        IList::reverse(this->view);
    }

    /* Rotate a list to the right by the specified number of steps. */
    void rotate(long long steps = 1) {
        IList::rotate(this->view, steps);
    }

    ///////////////////////
    ////    PROXIES    ////
    ///////////////////////

    /* Get a proxy for a value at a particular index of the list. */
    template <typename T>
    IList::ElementProxy<View> operator[](T index) {
        return IList::position(this->view, index);
    }

    /* Get a proxy for a slice within the list. */
    template <typename... Args>
    IList::SliceProxy<Self> slice(Args&&... args) {
        // can throw type_error, std::invalid_argument, std::runtime_error
        return IList::slice(*this, std::forward<Args>(args)...);
    }

    //////////////////////////////////
    ////    OPERATOR OVERLOADS    ////
    //////////////////////////////////

    // TODO: move these into algorithms/concatenate.h, repeat.h, and lexical_compare.h

};


/////////////////////////////
////    CONCATENATION    ////
/////////////////////////////


/* A mixin that adds operator overloads that mimic the behavior of Python lists with
respect to concatenation, repetition, and lexicographic comparison. */
template <typename Derived>
struct Concatenateable {
    static constexpr bool enable = std::is_base_of_v<Concatenateable<Derived>, Derived>;

    /* Overload the + operator to allow concatenation of Derived types from both
    Python and C++. */
    template <typename T>
    friend Derived operator+(const Derived& lhs, const T& rhs);
    template <typename T>
    friend T operator+(const T& lhs, const Derived& rhs);

    /* Overload the += operator to allow in-place concatenation of Derived types from
    both Python and C++. */
    template <typename T>
    friend Derived& operator+=(Derived& lhs, const T& rhs);

};


/* Allow Python-style concatenation between Linked data structures and arbitrary
Python/C++ containers. */
template <typename T, typename Derived>
inline auto operator+(const Derived& lhs, const T& rhs)
    -> std::enable_if_t<Concatenateable<Derived>::enable, Derived>
{
    std::optional<Derived> result = lhs.copy();
    if (!result.has_value()) {
        throw std::runtime_error("could not copy list");
    }
    result.value().extend(rhs);  // must be specialized for T
    return Derived(std::move(result.value()));
}


/* Allow Python-style concatenation between list-like C++ containers and Linked data
structures. */
template <typename T, typename Derived>
inline auto operator+(const T& lhs, const Derived& rhs)
    -> std::enable_if_t<
        // first, check that T is a list-like container with a range-based insert
        // method that returns an iterator.  This is true for all STL containers.
        std::is_same_v<
            decltype(
                std::declval<T>().insert(
                    std::declval<T>().end(),
                    std::declval<Derived>().begin(),
                    std::declval<Derived>().end()
                )
            ),
            typename T::iterator
        >,
        // next, check that Derived inherits from Concatenateable
        std::enable_if_t<Concatenateable<Derived>::enable, T>
    >
{
    T result = lhs;
    result.insert(result.end(), rhs.begin(), rhs.end());  // STL compliant
    return result;
}


/* Allow Python-style concatenation between Python sequences and Linked data
structures. */
template <typename T, typename Derived, bool Enable = Concatenateable<Derived>::enable>
inline auto operator+(const PyObject* lhs, const Derived& rhs)
    -> std::enable_if_t<Enable, PyObject*>
{
    // Check that lhs is a Python sequence
    if (!PySequence_Check(lhs)) {
        std::ostringstream msg;
        msg << "can only concatenate sequence (not '";
        msg << lhs->ob_type->tp_name << "') to sequence";
        throw type_error(msg.str());
    }

    // unpack list into Python sequence
    PyObject* seq = PySequence_List(rhs.iter.python());  // new ref
    if (seq == nullptr) {
        return nullptr;  // propagate error
    }

    // concatenate using Python API
    PyObject* concat = PySequence_Concat(lhs, seq);
    Py_DECREF(seq);
    return concat;
}


/* Allow in-place concatenation for Linked data structures using the += operator. */
template <typename T, typename Derived>
inline auto operator+=(Derived& lhs, const T& rhs)
    -> std::enable_if_t<Concatenateable<Derived>::enable, Derived&>
{
    lhs.extend(rhs);  // must be specialized for T
    return lhs;
}


//////////////////////////
////    REPETITION    ////
//////////////////////////


// TODO: we could probably optimize repetition by allocating a contiguous block of
// nodes equal to list.size() * rhs.  We could also remove the extra copy in *= by
// using an iterator to the end of the list and reusing it for each iteration.


/* A mixin that adds operator overloads that mimic the behavior of Python lists with
respect to concatenation, repetition, and lexicographic comparison. */
template <typename Derived>
struct Repeatable {
    static constexpr bool enable = std::is_base_of_v<Repeatable<Derived>, Derived>;

    // NOTE: We use a dummy typename to avoid forward declarations of operator* and
    // operator*=.  It doesn't actually affect the implementation of either overload.

    /* Overload the * operator to allow repetition of Derived types from both Python
    and C++. */
    template <typename>
    friend Derived operator*(const Derived& lhs, const ssize_t rhs);
    template <typename>
    friend Derived operator*(const Derived& lhs, const PyObject* rhs);
    template <typename>
    friend Derived operator*(const ssize_t lhs, const Derived& rhs);
    template <typename>
    friend Derived operator*(const PyObject* lhs, const Derived& rhs);

    /* Overload the *= operator to allow in-place repetition of Derived types from
    both Python and C++. */
    template <typename>
    friend Derived& operator*=(Derived& lhs, const ssize_t rhs);
    template <typename>
    friend Derived& operator*=(Derived& lhs, const PyObject* rhs);

};


/* Allow Python-style repetition for Linked data structures using the * operator. */
template <typename = void, typename Derived>
auto operator*(const Derived& lhs, const ssize_t rhs)
    -> std::enable_if_t<Repeatable<Derived>::enable, Derived>
{
    // handle empty repitition
    if (rhs <= 0 || lhs.size() == 0) {
        return Derived(lhs.max_size(), lhs.specialization());
    }

    // copy lhs
    std::optional<Derived> result = lhs.copy();
    if (!result.has_value()) {
        throw std::runtime_error("could not copy list");
    }

    // extend copy rhs - 1 times
    for (ssize_t i = 1; i < rhs; ++i) {
        result.value().extend(lhs);
    }

    // move result into return value
    return Derived(std::move(result.value()));
}


/* Allow Python-style repetition for Linked data structures using the * operator. */
template <typename = void, typename Derived>
inline auto operator*(const ssize_t lhs, const Derived& rhs)
    -> std::enable_if_t<Repeatable<Derived>::enable, Derived>
{
    return rhs * lhs;  // symmetric
}


/* Allow Python-style repetition for Linked data structures using the * operator. */
template <typename = void, typename Derived>
auto operator*(const Derived& lhs, const PyObject* rhs)
    -> std::enable_if_t<Repeatable<Derived>::enable, Derived>
{
    // Check that rhs is a Python integer
    if (!PyLong_Check(rhs)) {
        std::ostringstream msg;
        msg << "can't multiply sequence by non-int of type '";
        msg << rhs->ob_type->tp_name << "'";
        throw type_error(msg.str());
    }

    // convert to C++ integer
    ssize_t val = PyLong_AsSsize_t(rhs);
    if (val == -1 && PyErr_Occurred()) {
        throw catch_python<type_error>();
    }

    // delegate to C++ overload
    return lhs * val;
}


/* Allow Python-style repetition for Linked data structures using the * operator. */
template <typename = void, typename Derived>
inline auto operator*(const PyObject* lhs, const Derived& rhs)
    -> std::enable_if_t<Repeatable<Derived>::enable, Derived>
{
    return rhs * lhs;  // symmetric
}


/* Allow in-place repetition for Linked data structures using the *= operator. */
template <typename = void, typename Derived>
auto operator*=(Derived& lhs, const ssize_t rhs)
    -> std::enable_if_t<Repeatable<Derived>::enable, Derived&>
{
    // handle empty repitition
    if (rhs <= 0 || lhs.size() == 0) {
        lhs.clear();
        return lhs;
    }

    // copy lhs
    std::optional<Derived> copy = lhs.copy();
    if (!copy.has_value()) {
        throw std::runtime_error("could not copy list");
    }

    // extend lhs rhs - 1 times
    for (ssize_t i = 1; i < rhs; ++i) {
        lhs.extend(copy.value());
    }
    return lhs;
}


/* Allow in-place repetition for Linked data structures using the *= operator. */
template <typename = void, typename Derived>
inline auto operator*=(Derived& lhs, const PyObject* rhs)
    -> std::enable_if_t<Repeatable<Derived>::enable, Derived&>
{
    // Check that rhs is a Python integer
    if (!PyLong_Check(rhs)) {
        std::ostringstream msg;
        msg << "can't multiply sequence by non-int of type '";
        msg << rhs->ob_type->tp_name << "'";
        throw type_error(msg.str());
    }

    // convert to C++ integer
    ssize_t val = PyLong_AsSsize_t(rhs);
    if (val == -1 && PyErr_Occurred()) {
        throw catch_python<type_error>();
    }

    // delegate to C++ overload
    return lhs *= val;
}


/////////////////////////////////////////
////    LEXICOGRAPHIC COMPARISONS    ////
/////////////////////////////////////////


/* A mixin that adds operator overloads that mimic the behavior of Python lists with
respect to concatenation, repetition, and lexicographic comparison. */
template <typename Derived>
struct Lexicographic {
    static constexpr bool enable = std::is_base_of_v<Lexicographic<Derived>, Derived>;

    /* Overload the < operator to allow lexicographic comparison between Derived types
    and arbitrary C++ containers/Python sequences. */
    template <typename T>
    friend bool operator<(const Derived& lhs, const T& rhs);
    template <typename T>
    friend bool operator<(const T& lhs, const Derived& rhs);

    /* Overload the <= operator to allow lexicographic comparison between Derived types
    and arbitrary C++ containers/Python sequences. */
    template <typename T>
    friend bool operator<=(const Derived& lhs, const T& rhs);
    template <typename T>
    friend bool operator<=(const T& lhs, const Derived& rhs);

    /* Overload the == operator to allow lexicographic comparison between Derived types
    and arbitrary C++ containers/Python sequences. */
    template <typename T>
    friend bool operator==(const Derived& lhs, const T& rhs);
    template <typename T>
    friend bool operator==(const T& lhs, const Derived& rhs);

    /* Overload the != operator to allow lexicographic comparison between Derived types
    and arbitrary C++ containers/Python sequences. */
    template <typename T>
    friend bool operator!=(const Derived& lhs, const T& rhs);
    template <typename T>
    friend bool operator!=(const T& lhs, const Derived& rhs);

    /* Overload the > operator to allow lexicographic comparison between Derived types
    and arbitrary C++ containers/Python sequences. */
    template <typename T>
    friend bool operator>(const Derived& lhs, const T& rhs);
    template <typename T>
    friend bool operator>(const T& lhs, const Derived& rhs);

    /* Overload the >= operator to allow lexicographic comparison between Derived types
    and arbitrary C++ containers/Python sequences. */
    template <typename T>
    friend bool operator>=(const Derived& lhs, const T& rhs);
    template <typename T>
    friend bool operator>=(const T& lhs, const Derived& rhs);

};


/* Allow lexicographic < comparison between Linked data structures and compatible C++
containers. */
template <typename T, typename Derived, bool Enable = Lexicographic<Derived>::enable>
auto operator<(const Derived& lhs, const T& rhs) -> std::enable_if_t<Enable, bool> {
    // get coupled iterators
    auto iter_lhs = std::begin(lhs);
    auto end_lhs = std::end(lhs);
    auto iter_rhs = std::begin(rhs);
    auto end_rhs = std::end(rhs);

    // loop until one of the sequences is exhausted
    while (iter_lhs != end_lhs && iter_rhs != end_rhs) {
        if (*iter_lhs < *iter_rhs) return true;
        if (*iter_rhs < *iter_lhs) return false;
        ++iter_lhs;
        ++iter_rhs;
    }

    // check if lhs is shorter than rhs
    return (iter_lhs == end_lhs && iter_rhs != end_rhs);
}


/* Allow lexicographic < comparison between Linked data structures and Python
sequences. */
template <typename Derived>
auto operator<(const Derived& lhs, const PyObject* rhs)
    -> std::enable_if_t<Lexicographic<Derived>::enable, bool>
{
    using Node = typename Derived::Node;

    // check that rhs is a Python sequence
    if (!PySequence_Check(rhs)) {
        std::ostringstream msg;
        msg << "can only compare list to sequence (not '";
        msg << rhs->ob_type->tp_name << "')";
        throw type_error(msg.str());
    }

    // get coupled iterators
    auto iter_lhs = std::begin(lhs);
    auto end_lhs = std::end(lhs);
    PyIterable pyiter_rhs(rhs);  // handles reference counts
    auto iter_rhs = pyiter_rhs.begin();
    auto end_rhs = pyiter_rhs.end();

    // loop until one of the sequences is exhausted
    while (iter_lhs != end_lhs && iter_rhs != end_rhs) {
        Node* node = *iter_lhs;  // TODO: not actually a Node
        if (node->lt(*iter_rhs)) {
            return true;
        }

        // compare rhs < lhs
        int comp = PyObject_RichCompareBool(*iter_rhs, node->value(), Py_LT);
        if (comp == -1) {
            throw catch_python<type_error>();
        } else if (comp == 1) {
            return false;
        }

        // advance iterators
        ++iter_lhs;
        ++iter_rhs;
    }

    // check if lhs is shorter than rhs
    return (iter_lhs == end_lhs && iter_rhs != end_rhs);
}


/* Allow lexicographic < comparison between compatible C++ containers and Linked data
structures. */
template <typename T, typename Derived>
inline auto operator<(const T& lhs, const Derived& rhs)
    -> std::enable_if_t<Lexicographic<Derived>::enable, bool>
{
    return rhs > lhs;  // implies lhs < rhs
}


/* Allow lexicographic <= comparison between Linked data structures and compatible C++
containers. */
template <typename T, typename Derived>
auto operator<=(const Derived& lhs, const T& rhs)
    -> std::enable_if_t<Lexicographic<Derived>::enable, bool>
{
    // get coupled iterators
    auto iter_lhs = std::begin(lhs);
    auto end_lhs = std::end(lhs);
    auto iter_rhs = std::begin(rhs);
    auto end_rhs = std::end(rhs);

    // loop until one of the sequences is exhausted
    while (iter_lhs != end_lhs && iter_rhs != end_rhs) {
        if ((*iter_lhs)->value() < *iter_rhs) return true;
        if (*iter_rhs < (*iter_lhs)->value()) return false;
        ++iter_lhs;
        ++iter_rhs;
    }

    // check if lhs is exhausted
    return (iter_lhs == end_lhs);
}


/* Allow lexicographic <= comparison between Linked data structures and Python
sequences. */
template <typename Derived>
auto operator<=(const Derived& lhs, const PyObject* rhs)
    -> std::enable_if_t<Lexicographic<Derived>::enable, bool>
{
    using Node = typename Derived::Node;

    // check that rhs is a Python sequence
    if (!PySequence_Check(rhs)) {
        std::ostringstream msg;
        msg << "can only compare list to sequence (not '";
        msg << rhs->ob_type->tp_name << "')";
        throw type_error(msg.str());
    }

    // get coupled iterators
    auto iter_lhs = std::begin(lhs);
    auto end_lhs = std::end(lhs);
    PyIterable pyiter_rhs(rhs);  // handles reference counts
    auto iter_rhs = pyiter_rhs.begin();
    auto end_rhs = pyiter_rhs.end();

    // loop until one of the sequences is exhausted
    while (iter_lhs != end_lhs && iter_rhs != end_rhs) {
        Node* node = *iter_lhs;
        if (node->lt(*iter_rhs)) {
            return true;
        }

        // compare rhs < lhs
        int comp = PyObject_RichCompareBool(*iter_rhs, node->value(), Py_LT);
        if (comp == -1) {
            throw std::runtime_error("could not compare list elements");
        } else if (comp == 1) {
            return false;
        }

        // advance iterators
        ++iter_lhs;
        ++iter_rhs;
    }

    // check if lhs is exhausted
    return (iter_lhs == end_lhs);
}


/* Allow lexicographic <= comparison between compatible C++ containers and Linked data
structures. */
template <typename T, typename Derived>
inline auto operator<=(const T& lhs, const Derived& rhs)
    -> std::enable_if_t<Lexicographic<Derived>::enable, bool>
{
    return rhs >= lhs;  // implies lhs <= rhs
}


/* Allow == comparison between Linked data structures and compatible C++ containers. */
template <typename T, typename Derived>
auto operator==(const Derived& lhs, const T& rhs)
    -> std::enable_if_t<Lexicographic<Derived>::enable, bool>
{
    using Node = typename Derived::Node;

    if (lhs.size() != rhs.size()) {
        return false;
    }

    // compare elements in order
    auto iter_rhs = std::begin(rhs);
    for (const Node& item : lhs) {
        if (item->value() != *iter_rhs) {
            return false;
        }
        ++iter_rhs;
    }

    return true;
}


/* Allow == comparison betwen Linked data structures and Python sequences. */
template <typename Derived>
auto operator==(const Derived& lhs, const PyObject* rhs)
    -> std::enable_if_t<Lexicographic<Derived>::enable, bool>
{
    using Node = typename Derived::Node;

    // check that rhs is a Python sequence
    if (!PySequence_Check(rhs)) {
        std::ostringstream msg;
        msg << "can only compare list to sequence (not '";
        msg << rhs->ob_type->tp_name << "')";
        throw type_error(msg.str());
    }

    // check that lhs and rhs have the same length
    Py_ssize_t len = PySequence_Length(rhs);
    if (len == -1) {
        std::ostringstream msg;
        msg << "could not get length of sequence (of type '";
        msg << rhs->ob_type->tp_name << "')";
        throw type_error(msg.str());
    } else if (lhs.size() != static_cast<size_t>(len)) {
        return false;
    }

    // compare elements in order
    PyIterable pyiter_rhs(rhs);  // handles reference counts
    auto iter_rhs = pyiter_rhs.begin();
    for (const Node& item : lhs) {
        if (item->ne(*iter_rhs)) {
            return false;
        }
        ++iter_rhs;
    }

    return true;
}


/* Allow == comparison between compatible C++ containers and Linked data structures. */
template <typename T, typename Derived>
inline auto operator==(const T& lhs, const Derived& rhs)
    -> std::enable_if_t<Lexicographic<Derived>::enable, bool>
{
    return rhs == lhs;
}


/* Allow != comparison between Linked data structures and compatible C++ containers. */
template <typename T, typename Derived>
inline auto operator!=(const Derived& lhs, const T& rhs)
    -> std::enable_if_t<Lexicographic<Derived>::enable, bool>
{
    return !(lhs == rhs);
}


/* Allow != comparison between compatible C++ containers Linked data structures. */
template <typename T, typename Derived>
inline auto operator!=(const T& lhs, const Derived& rhs)
    -> std::enable_if_t<Lexicographic<Derived>::enable, bool>
{
    return !(lhs == rhs);
}


/* Allow lexicographic >= comparison between Linked data structures and compatible C++
containers. */
template <typename T, typename Derived>
inline auto operator>=(const Derived& lhs, const T& rhs)
    -> std::enable_if_t<Lexicographic<Derived>::enable, bool>
{
    return !(lhs < rhs);
}


/* Allow lexicographic >= comparison between compatible C++ containers and Linked data
structures. */
template <typename T, typename Derived>
inline auto operator>=(const T& lhs, const Derived& rhs)
    -> std::enable_if_t<Lexicographic<Derived>::enable, bool>
{
    return !(lhs < rhs);
}


/* Allow lexicographic > comparison between Linked data structures and compatible C++
containers. */
template <typename T, typename Derived>
inline auto operator>(const Derived& lhs, const T& rhs)
    -> std::enable_if_t<Lexicographic<Derived>::enable, bool>
{
    return !(lhs <= rhs);
}


/* Allow lexicographic > comparison between compatible C++ containers and Linked data
structures. */
template <typename T, typename Derived>
inline auto operator>(const T& lhs, const Derived& rhs)
    -> std::enable_if_t<Lexicographic<Derived>::enable, bool>
{
    return !(lhs <= rhs);
}



}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LIST_LIST_H include guard
