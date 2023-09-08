// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_UTIL_H
#define BERTRAND_STRUCTS_CORE_UTIL_H

#include <type_traits>  // std::decay_t<>
#include <Python.h>  // CPython API
#include "node.h"  // has_prev<>


/////////////////////////////////
////    COUPLED ITERATORS    ////
/////////////////////////////////


/*
NOTE: CoupledIterators are used to share state between the begin() and end() iterators
in a loop and generally simplify the iterator interface.  They act like pass-through
decorators for the begin() iterator, and contain their own end() iterator to terminate
the loop.  This means we can write loops as follows:

for (auto iter = view.iter(); iter != iter.end(); ++iter) {
     // full access to iter
}

Rather than the more verbose:

for (auto iter = view.begin(), end = view.end(); iter != end; ++iter) {
     // same as above
}

Both have identical performance, but the former is more concise and easier to read.  It
also allows any arguments provided to iter() to be passed through to both the begin()
and end() iterators, which can be used to share state between the two.
*/


/* A coupled pair of begin() and end() iterators to simplify the iterator interface. */
template <typename Iterator>
class CoupledIterator {
public:
    // iterator tags for std::iterator_traits
    using iterator_category     = typename Iterator::iterator_category;
    using difference_type       = typename Iterator::difference_type;
    using value_type            = typename Iterator::value_type;
    using pointer               = typename Iterator::pointer;
    using reference             = typename Iterator::reference;

    // couple the begin() and end() iterators into a single object
    CoupledIterator(const Iterator& begin, const Iterator& end) :
        first(begin), second(end)
    {}

    // allow use of the CoupledIterator in a range-based for loop
    Iterator& begin() { return first; }
    Iterator& end() { return second; }

    // pass iterator protocol through to begin()
    inline value_type operator*() const { return *first; }
    inline CoupledIterator& operator++() { ++first; return *this; }
    inline bool operator!=(const Iterator& other) const { return first != other; }

    // conditionally compile all other methods based on Iterator interface.
    // NOTE: this uses SFINAE to detect the presence of these methods on the template
    // Iterator.  If the Iterator does not implement the named method, then it will not
    // be compiled, and users will get compile-time errors if they try to access it.
    // This avoids the need to manually extend the CoupledIterator interface to match
    // that of the Iterator.  See https://en.cppreference.com/w/cpp/language/sfinae
    // for more information.

    template <typename T = Iterator>
    inline auto insert(value_type value) -> decltype(std::declval<T>().insert(value)) {
        return first.insert(value);  // void
    }

    template <typename T = Iterator>
    inline auto remove() -> decltype(std::declval<T>().remove()) {
        return first.remove();
    }

    template <typename T = Iterator>
    inline auto replace(value_type value) -> decltype(std::declval<T>().replace(value)) {
        return first.replace(value);
    }

    template <typename T = Iterator>
    inline auto index() -> decltype(std::declval<T>().index()) const {
        return first.index();
    }

    template <typename T = Iterator>
    inline auto inverted() -> decltype(std::declval<T>().inverted()) const {
        return first.inverted();
    }

protected:
    Iterator first, second;
};


////////////////////////////////
////    PYTHON ITERABLES    ////
////////////////////////////////


/*
NOTE: PyIterable describes a Python iterable object, which is any object that can be
iterated over using PyObject_GetIter() (i.e. `iter(obj)` in normal Python).  This
automates the complicated (and dangerous) reference counting that would be required to
iterate over a Python sequence using the C API directly.  It also allows us to use
range-based for loops to iterate over Python sequences, which is much more concise and
easier to read than the equivalent C API code.

Note that PyIterable can throw std::invalid_argument errors if PyObject_GetIter() or
PyIter_Next() fail with an error code.  Users should be prepared to handle these
exceptions whenever a PyIterable is constructed or iterated over.
*/


/* A wrapper around an arbitrary Python iterable that enables for-each style loops. */
class PyIterable {
public:
    class Iterator;
    using IteratorPair = CoupledIterator<Iterator>;

    /* Construct a PyIterable from a Python sequence. */
    PyIterable(PyObject* seq) : py_iter(PyObject_GetIter(seq)) {
        if (py_iter == nullptr) {
            throw std::invalid_argument("could not get iter(sequence)");
        }
    }

    /* Release the Python sequence. */
    ~PyIterable() { Py_XDECREF(py_iter); }

    /* Iterate over the sequence. */
    inline IteratorPair iter() const { return IteratorPair(begin(), end()); }
    inline Iterator begin() const { return Iterator(py_iter); }
    inline Iterator end() const { return Iterator(); }

    class Iterator {
    public:
        // iterator tags for std::iterator_traits
        using iterator_category     = std::forward_iterator_tag;
        using difference_type       = std::ptrdiff_t;
        using value_type            = PyObject*;
        using pointer               = PyObject**;
        using reference             = PyObject*&;

        /* Get current item. */
        PyObject* operator*() const { return curr; }

        /* Advance to next item. */
        Iterator& operator++() {
            Py_DECREF(curr);
            curr = PyIter_Next(py_iter);
            if (curr == nullptr && PyErr_Occurred()) {
                throw std::invalid_argument("could not get next(iterator)");
            }
            return *this;
        }

        /* Terminate sequence. */
        bool operator!=(const Iterator& other) const { return curr != other.curr; }

        /* Handle reference counts if an iterator is destroyed partway through
        iteration. */
        ~Iterator() { Py_XDECREF(curr); }

    private:
        friend PyIterable;
        friend IteratorPair;
        PyObject* py_iter;
        PyObject* curr;

        /* Return an iterator to the start of the sequence. */
        Iterator(PyObject* py_iter) : py_iter(py_iter), curr(nullptr) {
            if (py_iter != nullptr) {
                curr = PyIter_Next(py_iter);
                if (curr == nullptr && PyErr_Occurred()) {
                    throw std::invalid_argument("could not get next(iterator)");
                }
            }
        }

        /* Return an iterator to the end of the sequence. */
        Iterator() : py_iter(nullptr), curr(nullptr) {}
    };

protected:
    PyObject* py_iter;
};


/////////////////////////////
////    BIDIRECTIONAL    ////
/////////////////////////////


/*
NOTE: Bidirectional is a type-erased iterator wrapper that can contain either a forward
or backward iterator over a linked list.  This allows us to write bare-metal loops if
the iteration direction is known at compile time, while also allowing for dynamic
traversal based on runtime conditions.  This is useful for implementing slices, which
can be iterated over in either direction depending on the step size and singly- vs.
doubly-linked nature of the list.

Bidirectional iterators have a small overhead compared to statically-typed iterators,
but this is minimized as much as possible through the use of tagged unions and
constexpr branches to eliminate conditionals.  If a list is doubly-linked, these
optimizations mean that we only add a single if-statement on a constant boolean
discriminator to the loop body to determine the iterator direction.  If a list is
singly-linked, then the Bidirectional iterator is functionally equivalent to a
statically-typed forward iterator, and there are no unnecessary branches at all.
*/


/* enum to make iterator direction declarations more readable. */
enum class Direction {
    forward,
    backward
};


/* Conditionally-compiled base class for Bidirectional iterators that respects the
reversability of the associated view. */
template <template <Direction, typename = void> class Iterator, bool HasPrev>
class BidirectionalBase;


/* Specialization for singly-linked lists. */
template <template <Direction, typename = void> class Iterator>
class BidirectionalBase<Iterator, false> {
protected:
    union { Iterator<Direction::forward> forward; } it;

    BidirectionalBase(const Iterator<Direction::forward>& iter) :
        it{.forward = iter}
    {}
};


/* Specialization for doubly-linked lists. */
template <template <Direction, typename = void> class Iterator>
class BidirectionalBase<Iterator, true> {
protected:
    union {
        Iterator<Direction::forward> forward;
        Iterator<Direction::backward> backward;
    } it;

    BidirectionalBase(const Iterator<Direction::forward>& iter) :
        it{.forward = iter}
    {}
    BidirectionalBase(const Iterator<Direction::backward>& iter) :
        it{.backward = iter}
    {}
};


/* A type-erased iterator that can contain either a forward or backward iterator. */
template <template <Direction, typename = void> class Iterator>
class Bidirectional : public BidirectionalBase<
    Iterator,
    has_prev<typename Iterator<Direction::forward>::Node>::value
> {
public:
    using ForwardIterator = Iterator<Direction::forward>;
    using Node = typename ForwardIterator::Node;
    using Base = BidirectionalBase<Iterator, has_prev<Node>::value>;

    // iterator tags for std::iterator_traits
    using iterator_category     = typename ForwardIterator::iterator_category;
    using difference_type       = typename ForwardIterator::difference_type;
    using value_type            = typename ForwardIterator::value_type;
    using pointer               = typename ForwardIterator::pointer;
    using reference             = typename ForwardIterator::reference;

    const bool backward;

    /* Initialize the union using an existing iterator. */
    template <Direction dir>
    Bidirectional(const Iterator<dir>& it) :
        Base(it), backward(dir == Direction::backward)
    {}

    /* Call the contained type's destructor. */
    ~Bidirectional() {
        if constexpr (has_prev<Node>::value) {
            if (backward) {
                this->it.backward.~Iterator();
            } else {
                this->it.forward.~Iterator();
            }
        } else {
            this->it.forward.~Iterator();
        }
    }

    /* Dereference the iterator to get the node at the current position. */
    inline value_type operator*() const {
        if constexpr (has_prev<Node>::value) {
            if (backward) {
                return *(this->it.backward);
            }
        }
        return *(this->it.forward);
    }

    /* Prefix increment to advance the iterator to the next node in the slice. */
    inline Bidirectional& operator++() {
        if constexpr (has_prev<Node>::value) {
            if (backward) {
                ++(this->it.backward);
                return *this;
            }
        }
        ++(this->it.forward);
        return *this;
    }

    /* Inequality comparison to terminate the slice. */
    inline bool operator!=(const Bidirectional& other) const {
        using OtherNode = typename std::decay_t<decltype(other)>::Node;

        // NOTE: for our purposes, both the forward and backward iterators are freely
        // comparable.  If that weren't the case, we'd have to ensure a proper match.
        if constexpr (has_prev<Node>::value) {
            if (backward) {
                if constexpr (has_prev<OtherNode>::value) {
                    if (other.backward) {
                        return this->it.backward != other.it.backward;
                    }
                }
                return this->it.backward != other.it.forward;
            }
        }

        if constexpr (has_prev<OtherNode>::value) {
            if (other.backward) {
                printf("f\n");
                return this->it.forward != other.it.backward;
            }
        }
        return this->it.forward != other.it.forward;
    }

    // conditionally compile all other methods based on Iterator interface.
    // NOTE: this uses SFINAE to detect the presence of these methods on the template
    // Iterator.  If the Iterator does not implement the named method, then it will not
    // be compiled, and users will get compile-time errors if they try to access it.
    // This avoids the need to manually extend the Bidirectional interface to match
    // that of the Iterator.  See https://en.cppreference.com/w/cpp/language/sfinae
    // for more information.

    /* Insert a node at the current position. */
    template <typename T = ForwardIterator>
    inline auto insert(value_type value) -> decltype(std::declval<T>().insert(value)) {
        if constexpr (has_prev<Node>::value) {
            if (backward) {
                return this->it.backward.insert(value);
            }
        }
        return this->it.forward.insert(value);
    }

    /* Remove the node at the current position. */
    template <typename T = ForwardIterator>
    inline auto remove() -> decltype(std::declval<T>().remove()) {
        if constexpr (has_prev<Node>::value) {
            if (backward) {
                return this->it.backward.remove();
            }
        }
        return this->it.forward.remove();
    }

    /* Replace the node at the current position. */
    template <typename T = ForwardIterator>
    inline auto replace(value_type value) -> decltype(std::declval<T>().replace(value)) {
        if constexpr (has_prev<Node>::value) {
            if (backward) {
                return this->it.backward.replace(value);
            }
        }
        return this->it.forward.replace(value);
    }

    /* Get the index of the current position. */
    template <typename T = ForwardIterator>
    inline auto index() -> decltype(std::declval<T>().index()) const {
        if constexpr (has_prev<Node>::value) {
            if (backward) {
                return this->it.backward.index();
            }
        }
        return this->it.forward.index();
    }

    /* Check whether the iterator direction is consistent with a slice's step size. */
    template <typename T = ForwardIterator>
    inline auto inverted() -> decltype(std::declval<T>().inverted()) const {
        if constexpr (has_prev<Node>::value) {
            if (backward) {
                return this->it.backward.inverted();
            }
        }
        return this->it.forward.inverted();
    }

};


#endif  // BERTRAND_STRUCTS_CORE_UTIL_H
