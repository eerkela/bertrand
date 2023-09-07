// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_VIEW_H
#define BERTRAND_STRUCTS_CORE_VIEW_H

#include <chrono>  // std::chrono
#include <cstddef>  // size_t
#include <mutex>  // std::mutex, std::lock_guard
#include <optional>  // std::optional
#include <stdexcept>  // std::invalid_argument
#include <tuple>  // std::tuple
#include <Python.h>  // CPython API
#include "node.h"  // Hashed<T>, Mapped<T>
#include "allocate.h"  // Allocator
#include "table.h"  // HashTable


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


template <typename ViewType>
class SliceProxy;


///////////////////////////////////
////    ITERATOR DECORATORS    ////
///////////////////////////////////


// NOTE: CoupledIterator<> can wrap Bidirectional<> iterators, but not the other way
// around.


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
    inline auto consistent() -> decltype(std::declval<T>().consistent()) const {
        return first.consistent();
    }

private:
    Iterator first, second;
};


/* Conditionally-compiled base class for Bidirectional iterators that respects the
reversability of the associated view. */
template <template <bool, typename = void> class Iterator, bool HasPrev>
class BidirectionalBase;


/* Specialization for singly-linked lists. */
template <template <bool, typename = void> class Iterator>
class BidirectionalBase<Iterator, false> {
protected:
    union { Iterator<false> forward; } it;

    template <bool reverse>
    BidirectionalBase(const Iterator<reverse>& it) : it{it} {}
};


/* Specialization for doubly-linked lists. */
template <template <bool, typename = void> class Iterator>
class BidirectionalBase<Iterator, true> {
protected:
    union { Iterator<false> forward; Iterator<true> backward; } it;

    template <bool reverse>
    BidirectionalBase(const Iterator<reverse>& it) : it{it} {}
};


/* A type-erased iterator wrapper that can contain either a forward or backward
iterator of any kind. */
template <template <bool, typename = void> class Iterator>
class Bidirectional : public BidirectionalBase<
    Iterator,
    has_prev<typename Iterator<false>::Node>::value
> {
public:
    using Base = BidirectionalBase<
        Iterator, has_prev<typename Iterator<false>::Node>::value
    >;
    using ForwardIterator = Iterator<false>;
    using Node = typename ForwardIterator::Node;

    // iterator tags for std::iterator_traits
    using iterator_category     = typename ForwardIterator::iterator_category;
    using difference_type       = typename ForwardIterator::difference_type;
    using value_type            = typename ForwardIterator::value_type;
    using pointer               = typename ForwardIterator::pointer;
    using reference             = typename ForwardIterator::reference;

    /* Initialize the union using an existing iterator. */
    template <bool reverse>
    Bidirectional(const Iterator<reverse>& it) : Base(it), backward(reverse) {}

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
    inline auto consistent() -> decltype(std::declval<T>().consistent()) const {
        if constexpr (has_prev<Node>::value) {
            if (backward) {
                return this->it.backward.consistent();
            }
        }
        return this->it.forward.consistent();
    }

private:
    const bool backward;
};


/////////////////////////
////    UTILITIES    ////
/////////////////////////


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

private:
    PyObject* py_iter;
};


////////////////////////
////    FUNCTORS    ////
////////////////////////


// NOTE: each functor is associated with a particular namespace (colloquial) within a
// ListView.  They offer a more convenient, abstract interface for creating iterators
// and handling list access, and can be reused to promote composition over inheritance.


/* A functor that produces threading locks for the templated view. */
template <typename ViewType>
class ThreadLock {
public:
    using View = ViewType;
    using Guard = std::lock_guard<std::mutex>;
    using Clock = std::chrono::high_resolution_clock;
    using Resolution = std::chrono::nanoseconds;

    /* Return a std::lock_guard for the internal mutex using RAII semantics.  The mutex
    is automatically acquired when the guard is constructed and released when it goes
    out of scope.  Any operations in between are guaranteed to be atomic. */
    inline Guard operator()() const {
        if (track_diagnostics) {
            auto start = Clock::now();

            // acquire lock
            mtx.lock();

            auto end = Clock::now();
            lock_time += std::chrono::duration_cast<Resolution>(end - start).count();
            ++lock_count;

            // create a guard using the acquired lock
            return Guard(mtx, std::adopt_lock);
        }

        return Guard(mtx);
    }

    /* Return a heap-allocated std::lock_guard for the internal mutex.  The mutex is
    automatically acquired when the guard is constructed and released when it is
    manually deleted.  Any operations in between are guaranteed to be atomic.

    NOTE: this method is generally less safe than using the standard functor operator,
    but can be used for compatibility with Python's context manager protocol. */
    inline Guard* context() const {
        if (track_diagnostics) {
            auto start = Clock::now();

            // acquire lock
            Guard* lock = new Guard(mtx);

            auto end = Clock::now();
            lock_time += std::chrono::duration_cast<Resolution>(end - start).count();
            ++lock_count;

            return lock;
        }

        return new Guard(mtx);
    }

    /* Toggle diagnostics on or off and return its current setting. */
    inline bool diagnostics(std::optional<bool> enabled = std::nullopt) const {
        if (enabled.has_value()) {
            track_diagnostics = enabled.value();
        }
        return track_diagnostics;
    }

    /* Get the total number of times the mutex has been locked. */
    inline size_t count() const {
        return lock_count;
    }

    /* Get the total time spent waiting to acquire the lock. */
    inline size_t duration() const {
        return lock_time;  // includes a small overhead for lock acquisition
    }

    /* Get the average time spent waiting to acquire the lock. */
    inline double contention() const {
        return static_cast<double>(lock_time) / lock_count;
    }

    /* Reset the internal diagnostic counters. */
    inline void reset_diagnostics() const {
        lock_count = 0;
        lock_time = 0;
    }

private:
    friend View;
    mutable std::mutex mtx;
    mutable bool track_diagnostics = false;
    mutable size_t lock_count = 0;
    mutable size_t lock_time = 0;
};


/* A functor that produces unidirectional iterators over the templated view. */
template <typename ViewType>
class IteratorFactory {
public:
    using View = ViewType;
    using Node = typename View::Node;

    // NOTE: Reverse iterators are only compiled for doubly-linked lists.

    template <
        bool reverse = false,
        typename = std::enable_if_t<!reverse || has_prev<Node>::value>
    >
    class Iterator;

    template <
        bool reverse = false,
        typename = std::enable_if_t<!reverse || has_prev<Node>::value>
    >
    using IteratorPair = CoupledIterator<Iterator<reverse>>;

    /* Return a coupled iterator for clearer access to the iterator's interface. */
    template <bool reverse = false>
    inline IteratorPair<reverse> operator()() const {
        return IteratorPair(begin<reverse>(), end());
    }

    /* Return an iterator to the head/tail of a list based on the reverse parameter. */
    template <bool reverse = false>
    inline Iterator<reverse> begin() const {
        if constexpr (reverse) {
            return Iterator(view, view.tail);
        } else {
            return Iterator(view, view.head);
        }
    }

    /* Return a null iterator to terminate the sequence. */
    template <bool reverse = false>
    inline Iterator<reverse> end() const {
        return Iterator(view);
    }

    /* An iterator that traverses a list and keeps track of each node's neighbors. */
    template <bool reverse, typename>
    class Iterator {
    public:
        using View = ViewType;
        using Node = typename View::Node;

        // iterator tags for std::iterator_traits
        using iterator_category     = std::forward_iterator_tag;
        using difference_type       = std::ptrdiff_t;
        using value_type            = Node*;
        using pointer               = Node**;
        using reference             = Node*&;

        // neighboring nodes at the current position
        Node* prev;
        Node* curr;
        Node* next;

        /////////////////////////////////
        ////    ITERATOR PROTOCOL    ////
        /////////////////////////////////

        /* Dereference the iterator to get the node at the current position. */
        inline Node* operator*() const {
            return curr;
        }

        /* Prefix increment to advance the iterator to the next node in the slice. */
        inline Iterator& operator++() {
            if constexpr (reverse) {
                next = curr;
                curr = prev;
                prev = static_cast<Node*>(prev->prev);
            } else {
                prev = curr;
                curr = next;
                next = static_cast<Node*>(next->next);
            }
            return *this;
        }

        /* Inequality comparison to terminate the slice. */
        template <bool T>
        inline bool operator!=(const Iterator<T>& other) const {
            return curr != other.curr;
        }

        //////////////////////////////
        ////    HELPER METHODS    ////
        //////////////////////////////

        /* Insert a node at the current position. */
        inline void insert(Node* node) {
            if constexpr (reverse) {
                view.link(curr, node, next);
            } else {
                view.link(prev, node, curr);
            }
            if (PyErr_Occurred()) {
                return;  // propagate
            }

            if constexpr (reverse) { prev = curr; } else { next = curr; }
            curr = node;
        }

        /* Remove the node at the current position. */
        inline Node* remove() {
            Node* removed = curr;
            view.unlink(prev, curr, next);
            if constexpr (reverse) {
                curr = prev;
                if (prev != nullptr) {
                    prev = static_cast<Node*>(prev->prev);
                }
            } else {
                curr = next;
                if (next != nullptr) {
                    next = static_cast<Node*>(next->next);
                }
            }
            return removed;
        }

        /* Replace the node at the current position. */
        inline void replace(Node* node) {
            // remove current node
            Node* removed = curr;
            view.unlink(prev, curr, next);

            // insert new node
            view.link(prev, node, next);
            if (PyErr_Occurred()) {
                view.link(prev, removed, next);  // restore old node
                return;  // propagate
            }

            // recycle old node + update iterator
            view.recycle(removed);
            curr = node;
        }

    protected:
        friend IteratorFactory;
        View& view;

        ////////////////////////////
        ////    CONSTRUCTORS    ////
        ////////////////////////////

        /* Get an iterator to the start or end of the list. */
        Iterator(View& view, Node* node) :
            prev(nullptr), curr(node), next(nullptr), view(view)
        {
            if (curr != nullptr) {
                if constexpr (reverse) {
                    prev = static_cast<Node*>(curr->prev);
                } else {
                    next = static_cast<Node*>(curr->next);
                }
            }
        }

        /* Get a null iterator to the end of the list. */
        Iterator(View& view) :
            prev(nullptr), curr(nullptr), next(nullptr), view(view)
        {}

    };

private:
    friend View;
    View& view;

    IteratorFactory(View& view) : view(view) {}
};


/* A functor that produces unidirectional iterators to a specific index of the
templated view. */
template <typename ViewType>
class IndexFactory {
public:
    using View = ViewType;
    using Node = typename View::Node;

    // NOTE: Reverse iterators are only compiled for doubly-linked lists.

    template <
        bool reverse = false,
        typename = std::enable_if_t<!reverse || has_prev<Node>::value>
    >
    class Iterator;

    template <
        bool reverse = false,
        typename = std::enable_if_t<!reverse || has_prev<Node>::value>
    >
    using IteratorPair = CoupledIterator<Iterator<reverse>>;

    /* Return an iterator to an arbitrary index of a linked list. */
    template <typename T>
    inline std::optional<Bidirectional<Iterator>> operator()(
        T index,
        bool truncate = false
    ) const {
        // normalize index
        auto opt_index = normalize(index, truncate);
        if (!opt_index.has_value()) {
            return std::nullopt;
        }

        // get iterator to index
        size_t norm_index = opt_index.value();
        if constexpr (has_prev<Node>::value) {
            if (norm_index > view.size / 2) {  // backward traversal
                Iterator<true> it(view, view.tail, view.size - 1);
                for (size_t i = view.size - 1; i > norm_index; --i) {
                    ++it;
                }
                return std::make_optional(Bidirectional(it));
            }
        }

        // forward traversal
        Iterator<false> it(view, view.head, 0);
        for (size_t i = 0; i < norm_index; ++i) {
            ++it;
        }
        return std::make_optional(Bidirectional(it));
    }

    // TODO: implement forward() and backward() methods that return iterators with a
    // specific direction, rather than bidirectional.

    /* Normalize a numeric index, applying Python-style wraparound and bounds
    checking. */
    template <typename T>
    std::optional<size_t> normalize(T index, bool truncate = false) {
        bool index_lt_zero = index < 0;

        // wraparound negative indices
        if (index_lt_zero) {
            index += view.size;
            index_lt_zero = index < 0;
        }

        // boundscheck
        if (index_lt_zero || index >= static_cast<T>(view.size)) {
            if (truncate) {
                if (index_lt_zero) {
                    return 0;
                }
                return view.size - 1;
            }
            PyErr_SetString(PyExc_IndexError, "list index out of range");
            return std::nullopt;
        }

        // return as size_t
        return std::make_optional(static_cast<size_t>(index));
    }

    /* Normalize a Python integer for use as an index to the list. */
    std::optional<size_t> normalize(PyObject* index, bool truncate = false) {
        // check that index is a Python integer
        if (!PyLong_Check(index)) {
            PyErr_SetString(PyExc_TypeError, "index must be a Python integer");
            return std::nullopt;
        }

        // comparisons are kept at the python level until we're ready to return
        PyObject* py_zero = PyLong_FromSize_t(0);  // new reference
        PyObject* py_size = PyLong_FromSize_t(view.size);  // new reference
        int lt_zero = PyObject_RichCompareBool(index, py_zero, Py_LT);

        // wraparound negative indices
        bool release_index = false;
        if (lt_zero) {
            index = PyNumber_Add(index, py_size);  // new reference
            lt_zero = PyObject_RichCompareBool(index, py_zero, Py_LT);
            release_index = true;  // remember to DECREF index later
        }

        // boundscheck - value is bad
        if (lt_zero || PyObject_RichCompareBool(index, py_size, Py_GE)) {
            Py_DECREF(py_zero);
            Py_DECREF(py_size);
            if (release_index) {
                Py_DECREF(index);
            }

            // apply truncation if directed
            if (truncate) {
                if (lt_zero) {
                    return std::make_optional(static_cast<size_t>(0));
                }
                return std::make_optional(view.size - 1);
            }

            // raise IndexError
            PyErr_SetString(PyExc_IndexError, "list index out of range");
            return std::nullopt;
        }

        // value is good - cast to size_t
        size_t result = PyLong_AsSize_t(index);

        // clean up references
        Py_DECREF(py_zero);
        Py_DECREF(py_size);
        if (release_index) {
            Py_DECREF(index);
        }

        return std::make_optional(result);
    }

    // TODO: implement a find() method that returns a forward iterator to the first
    // occurrence of an item.

    template <bool reverse>
    using BaseIterator = typename IteratorFactory<View>::template Iterator<reverse>;

    template <bool reverse, typename>
    class Iterator : public BaseIterator<reverse> {
    public:
        using Base = BaseIterator<reverse>;

        /* prefix increment to advance iterator and update index. */
        inline Iterator& operator++() {
            if constexpr (reverse) {
                --idx;
            } else {
                ++idx;
            }
            Base::operator++();
            return *this;
        }

        /* Inequality comparison to terminate the slice. */
        template <bool T>
        inline bool operator!=(const Iterator<T>& other) const {
            return idx != other.idx;
        }

        /* Get the zero-based index of the iterator within the slice. */
        inline size_t index() const {
            return idx;
        }

    protected:
        friend IndexFactory;
        size_t idx;

        Iterator(View& view, Node* node, size_t idx) :
            Base(view, node), idx(idx)
        {}

        Iterator(View& view, size_t idx) : Base(view), idx(idx) {}
    };

private:
    friend View;
    View& view;

    IndexFactory(View& view) : view(view) {}
};


/* A functor that constructs bidirectional proxies for slices within the templated
view. */
template <typename ViewType>
class SliceFactory {
public:
    using View = ViewType;
    using Node = typename View::Node;
    class Indices;
    using Slice = SliceProxy<View>;

    /* Return a Slice proxy over the given indices. */
    template <typename... Args>
    std::optional<Slice> operator()(Args... args) const {
        auto indices = normalize(args...);
        if (!indices.has_value()) {
            return std::nullopt;
        }
        return std::make_optional(Slice(view, std::move(indices.value())));
    }

    /* Normalize slice indices, applying Python-style wraparound and bounds
    checking. */
    std::optional<Indices> normalize(
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt,
        std::optional<long long> step = std::nullopt
    ) const {
        // normalize slice indices
        long long size = static_cast<long long>(view.size);
        long long default_start = (step.value_or(0) < 0) ? (size - 1) : (0);
        long long default_stop = (step.value_or(0) < 0) ? (-1) : (size);
        long long default_step = 1;

        // normalize step
        long long step_ = step.value_or(default_step);
        if (step_ == 0) {
            PyErr_SetString(PyExc_ValueError, "slice step cannot be zero");
            return std::nullopt;
        }

        // normalize start index
        long long start_ = start.value_or(default_start);
        if (start_ < 0) {
            start_ += size;
            if (start_ < 0) {
                start_ = (step_ < 0) ? (-1) : (0);
            }
        } else if (start_ >= size) {
            start_ = (step_ < 0) ? (size - 1) : (size);
        }

        // normalize stop index
        long long stop_ = stop.value_or(default_stop);
        if (stop_ < 0) {
            stop_ += size;
            if (stop_ < 0) {
                stop_ = (step_ < 0) ? -1 : 0;
            }
        } else if (stop_ > size) {
            stop_ = (step_ < 0) ? (size - 1) : (size);
        }

        // get length of slice
        size_t length = std::max(
            (stop_ - start_ + step_ - (step_ > 0 ? 1 : -1)) / step_,
            static_cast<long long>(0)
        );

        // return as Indices
        return std::make_optional(Indices(start_, stop_, step_, length, view.size));
    }

    /* Normalize a Python slice object, applying Python-style wraparound and bounds
    checking. */
    std::optional<Indices> normalize(PyObject* py_slice) const {
        // check that input is a Python slice object
        if (!PySlice_Check(py_slice)) {
            PyErr_SetString(PyExc_TypeError, "index must be a Python slice");
            return std::nullopt;
        }

        // use CPython API to get slice indices
        Py_ssize_t py_start, py_stop, py_step, py_length;
        int err = PySlice_GetIndicesEx(
            py_slice, view.size, &py_start, &py_stop, &py_step, &py_length
        );
        if (err == -1) {
            return std::nullopt;  // propagate error
        }

        // cast from Py_ssize_t
        long long start = static_cast<long long>(py_start);
        long long stop = static_cast<long long>(py_stop);
        long long step = static_cast<long long>(py_step);
        size_t length = static_cast<size_t>(py_length);

        // return as Indices
        return std::make_optional(Indices(start, stop, step, length, view.size));
    }

    /* A simple class representing the normalized indices needed to construct a
    coherent slice. */
    class Indices {
    public:
        /* Get the original indices that were supplied to the constructor. */
        inline long long start() const { return _start; }
        inline long long stop() const { return _stop; }
        inline long long step() const { return _step; }
        inline size_t abs_step() const { return _abs_step; }

        /* Get the first and last included indices. */
        inline size_t first() const { return _first; }
        inline size_t last() const { return _last; }

        /* Get the number of items included in the slice. */
        inline size_t length() const { return _length; }
        inline bool empty() const { return _length == 0; }

        /* Check if the first and last indices conform to the expected step size. */
        inline bool consistent() const { return _consistent; }
        inline bool backward() const { return _backward; }

    private:
        friend SliceFactory;
        const long long _start;
        const long long _stop;
        const long long _step;
        const size_t _abs_step;
        size_t _first;
        size_t _last;
        const size_t _length;
        bool _consistent;
        bool _backward;

        Indices(
            const long long start,
            const long long stop,
            const long long step,
            const size_t length,
            const size_t view_size
        ) : _start(start), _stop(stop), _step(step), _abs_step(llabs(step)),
            _first(0), _last(0), _length(length), _consistent(true), _backward(false)
        {
            if (length > 0) {
                // convert to closed interval [start, closed]
                long long mod = py_modulo((stop - start), step);
                long long closed = (mod == 0) ? (stop - step) : (stop - mod);

                // get direction in which to traverse slice based on singly-/doubly-
                // linked nature of the list
                std::pair<size_t, size_t> dir = slice_direction(closed, view_size);
                _first = dir.first;
                _last = dir.second;

                // because we've adjusted our indices to minimize total iterations, we
                // may not necessarily be iterating in the same direction as the step
                // size would indicate.  we need to account for this when
                // getting/setting items in the slice
                _consistent = (_step < 0) ? (_first >= _last) : (_first <= _last);
                _backward = (_first > _last);
            }
        }

        /* A modulo operator (%) that matches Python's behavior with respect to
        negative numbers. */
        template <typename T>
        inline static T py_modulo(T a, T b) {
            // NOTE: Python's `%` operator is defined such that the result has
            // the same sign as the divisor (b).  This differs from C/C++, where
            // the result has the same sign as the dividend (a).
            return (a % b + b) % b;
        }

        /* Swap the start and stop indices based on the singly-/doubly-linked nature
        of the list. */
        inline std::pair<size_t, size_t> slice_direction(
            long long closed_stop, size_t view_size
        ) {
            // NOTE: if the list is doubly-linked, then we start at whichever end is
            // closest to its respective slice boundary.  This minimizes the total
            // number of iterations, but means that we may not be iterating in the same
            // direction as the step size would indicate
            if constexpr (has_prev<Node>::value) {
                long long size = static_cast<long long>(view_size);
                if (
                    (_step > 0 && _start <= size - closed_stop) ||
                    (_step < 0 && size - _start <= closed_stop)
                ) {
                    return std::make_pair(_start, closed_stop);  // consistent with step
                }
                return std::make_pair(closed_stop, _start);  // opposite of step
            }

            // NOTE: if the list is singly-linked, then we always have to iterate from
            // the head of the list.  If the step size is negative, this means that we
            // will end up iterating in the opposite direction.
            if (_step > 0) {
                return std::make_pair(_start, closed_stop);  // consistent with step
            }
            return std::make_pair(closed_stop, _start);  // opposite of step
        }
    };

private:
    friend View;
    View& view;

    SliceFactory(View& view) : view(view) {}
};


///////////////////////
////    PROXIES    ////
///////////////////////


// TODO:
// l = LinkedList("abcdef")
// l[4:2:-1]  // segfault


/* A proxy that allows for efficient operations on slices within a list. */
template <typename ViewType>
class SliceProxy {
public:
    using View = ViewType;
    using Node = typename View::Node;
    using Indices = typename SliceFactory<View>::Indices;

    // NOTE: Reverse iterators are only compiled for doubly-linked lists.

    template <
        bool reverse = false,
        typename = std::enable_if_t<!reverse || has_prev<Node>::value>
    >
    class Iterator;
    using IteratorPair = CoupledIterator<Bidirectional<Iterator>>;

    /* Get the underlying view being referenced by the proxy. */
    inline View& view() const { return _view; }

    /* Pass through to Indices. */
    inline long long start() const { return indices.start(); }
    inline long long stop() const { return indices.stop(); }
    inline long long step() const { return indices.step(); }
    inline size_t abs_step() const { return indices.abs_step(); }
    inline size_t first() const { return indices.first(); }
    inline size_t last() const { return indices.last(); }
    inline size_t length() const { return indices.length(); }
    inline bool empty() const { return indices.empty(); }
    inline bool consistent() const { return indices.consistent(); }
    inline bool backward() const { return indices.backward(); }

    /* Return a coupled pair of iterators for more fine-grained control. */
    inline IteratorPair iter(std::optional<size_t> length = std::nullopt) const {
        if (length.has_value()) {
            return IteratorPair(
                Bidirectional(Iterator(_view, origin, indices, length.value())),
                Bidirectional(Iterator(_view, indices, length.value()))
            );
        }
        return IteratorPair(begin(), end());
    }

    /* Return an iterator to the start of the slice. */
    inline Bidirectional<Iterator> begin() const {
        if (empty()) {
            return Bidirectional(Iterator(_view, indices, length()));  // empty iterator
        }
        return Bidirectional(Iterator(_view, origin, indices, length()));
    }

    /* Return an iterator to the end of the slice. */
    inline Bidirectional<Iterator> end() const {
        return Bidirectional(Iterator(_view, indices, length()));
    }

    /////////////////////////////
    ////    INNER CLASSES    ////
    /////////////////////////////

    template <bool reverse>
    using BaseIterator = typename IndexFactory<View>::template Iterator<reverse>;

    /* A specialized iterator built for slice traversal. */
    template <bool reverse, typename>
    class Iterator : public BaseIterator<reverse> {
    public:
        using Base = BaseIterator<reverse>;

        /* Prefix increment to advance the iterator to the next node in the slice. */
        inline Iterator& operator++() {
            ++this->idx;
            if (this->idx == length_override) {
                return *this;  // don't jump on last iteration
            }

            if constexpr (reverse) {
                for (size_t i = implicit_skip; i < indices.abs_step(); ++i) {
                    this->next = this->curr;
                    this->curr = this->prev;
                    this->prev = static_cast<Node*>(this->curr->prev);
                }
            } else {
                for (size_t i = implicit_skip; i < indices.abs_step(); ++i) {
                    this->prev = this->curr;
                    this->curr = this->next;
                    this->next = static_cast<Node*>(this->curr->next);
                }
            }
            return *this;
        }

        //////////////////////////////
        ////    HELPER METHODS    ////
        //////////////////////////////

        /* Remove the node at the current position. */
        inline Node* remove() { ++implicit_skip; return Base::remove(); }

        /* Indicates whether the direction of an Iterator matches the sign of the
        step size.

        If this is true, then the iterator will yield items in the same order as
        expected from the slice parameters.  Otherwise, it will yield items in the
        opposite order, and the user will have to account for this when getting/setting
        items within the list.  This is done to minimize the total number of iterations
        required to traverse the slice without backtracking. */
        inline bool consistent() const { return indices.consistent(); }

    protected:
        friend SliceProxy;
        const Indices& indices;
        size_t length_override;
        size_t implicit_skip;

        ////////////////////////////
        ////    CONSTRUCTORS    ////
        ////////////////////////////

        /* Get an iterator to the start of the slice. */
        Iterator(
            View& view,
            Node* origin,
            const Indices& indices,
            size_t length_override
        ) :
            Base(view, nullptr, 0), indices(indices), length_override(length_override),
            implicit_skip(0)
        { init(origin); }

        /* Get an iterator to terminate the slice. */
        Iterator(View& view, const Indices& indices, size_t length_override) :
            Base(view, length_override), indices(indices),
            length_override(length_override), implicit_skip(0)
        {}

        /* Get the initial values for the iterator based on an origin node. */
        inline void init(Node* origin) {
            if constexpr (reverse) {
                this->next = origin;
                if (origin == nullptr) {
                    this->curr = this->view.tail;
                } else {
                    this->curr = static_cast<Node*>(origin->prev);
                }
                this->prev = static_cast<Node*>(this->curr->prev);
            } else {
                this->prev = origin;
                if (origin == nullptr) {
                    this->curr = this->view.head;
                } else {
                    this->curr = static_cast<Node*>(origin->next);
                }
                this->next = static_cast<Node*>(this->curr->next);
            }
        }

    };

private:
    friend View;
    friend SliceFactory<View>;
    View& _view;
    const Indices indices;
    Node* origin;  // node that immediately precedes the slice (can be NULL)

    // TODO: use a cache to find the origin node when it is first requested.  That way,
    // creating a SliceProxy is still fast, and as long as we guard using slice.empty(),
    // we can avoid the overhead of finding the origin node if we never need it.

    // Once we call any of the iterator methods, we either find the origin node or use
    // the cached value.

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    // TODO: empty slice still needs to find origin node/etc in the case of a
    // set() insertion.

    /* Construct a SliceProxy with at least one element. */
    SliceProxy(View& view, Indices&& indices) :
        _view(view), indices(indices), origin(nullptr)
    {
        if (!empty()) {
            origin = find_origin();  // find the origin for the slice
        }
    }

    /* Iterate to find the origin node for the slice. */
    Node* find_origin() {
        if constexpr (has_prev<Node>::value) {
            if (backward()) {  // backward traversal
                Node* next = nullptr;
                Node* curr = _view.tail;
                for (size_t i = _view.size - 1; i > first(); i--) {
                    next = curr;
                    curr = static_cast<Node*>(curr->prev);
                }
                return next;
            }
        }

        // forward traversal
        Node* prev = nullptr;
        Node* curr = _view.head;
        for (size_t i = 0; i < first(); i++) {
            prev = curr;
            curr = static_cast<Node*>(curr->next);
        }
        return prev;
    }

};


////////////////////////
////    LISTVIEW    ////
////////////////////////


/* A pure C++ linked list data structure with customizable node types and allocation
strategies. */
template <
    typename NodeType = DoubleNode,
    template <typename> class Allocator = DynamicAllocator
>
class ListView {
public:
    using View = ListView<NodeType, Allocator>;
    using Node = NodeType;
    using Iter = IteratorFactory<View>;
    using Lock = ThreadLock<View>;
    using Slice = SliceProxy<View>;

    Node* head;
    Node* tail;
    size_t size;
    Py_ssize_t max_size;
    PyObject* specialization;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty ListView. */
    ListView(Py_ssize_t max_size = -1, PyObject* spec = nullptr) :
        head(nullptr), tail(nullptr), size(0), max_size(max_size),
        specialization(spec), slice(*this), iter(*this), allocator(max_size)
    {
        if (spec != nullptr) {
            Py_INCREF(spec);  // hold reference to specialization if given
        }
    }

    /* Construct a ListView from an input iterable. */
    ListView(
        PyObject* iterable,
        bool reverse = false,
        Py_ssize_t max_size = -1,
        PyObject* spec = nullptr
    ) : head(nullptr), tail(nullptr), size(0), max_size(max_size),
        specialization(spec), slice(*this), iter(*this), allocator(max_size)
    {
        // hold reference to specialization, if given
        if (spec != nullptr) {
            Py_INCREF(spec);
        }

        // unpack iterable into ListView
        try {
            PyIterable sequence(iterable);
            for (auto item : sequence) {
                // allocate a new node
                Node* curr = node(item);
                if (curr == nullptr) {  // error during node initialization
                    if constexpr (DEBUG) {
                        // QoL - nothing has been allocated, so we don't actually free
                        printf("    -> free: %s\n", repr(item));
                    }
                    throw std::invalid_argument("could not allocate node");
                }

                // link the node to the staged list
                if (reverse) {
                    link(nullptr, curr, head);
                } else {
                    link(tail, curr, nullptr);
                }
                if (PyErr_Occurred()) {
                    recycle(curr);  // clean up allocated node
                    throw std::invalid_argument("could not link node");
                }
            }
        } catch (std::invalid_argument& e) {
            self_destruct();
            throw e;
        }
    }

    /* Move constructor: transfer ownership from one ListView to another. */
    ListView(ListView&& other) :
        head(other.head), tail(other.tail), size(other.size), max_size(other.max_size),
        specialization(other.specialization), slice(*this), iter(*this),
        allocator(std::move(other.allocator))
    {
        // reset other ListView
        other.head = nullptr;
        other.tail = nullptr;
        other.size = 0;
        other.max_size = 0;
        other.specialization = nullptr;
    }

    /* Move assignment: transfer ownership from one ListView to another. */
    ListView& operator=(ListView&& other) {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // free old nodes
        self_destruct();

        // transfer ownership of nodes
        head = other.head;
        tail = other.tail;
        size = other.size;
        max_size = other.max_size;
        specialization = other.specialization;
        allocator = std::move(other.allocator);

        // reset other ListView
        other.head = nullptr;
        other.tail = nullptr;
        other.size = 0;
        other.max_size = 0;
        other.specialization = nullptr;

        return *this;
    }

    /* Copy constructors. These are disabled for the sake of efficiency, preventing us
    from unintentionally copying data.  Use the explicit copy() method instead. */
    ListView(const ListView& other) = delete;
    ListView& operator=(const ListView&) = delete;

    /* Destroy a ListView and free all its nodes. */
    ~ListView() {
        self_destruct();
    }

    ////////////////////////////////
    ////    LOW-LEVEL ACCESS    ////
    ////////////////////////////////

    // NOTE: these methods allow for the direct manipulation of the underlying linked
    // list, including the allocation/deallocation and links between individual nodes.
    // They are quite user friendly given their low-level nature, but users should
    // still be careful to avoid accidental memory leaks and segfaults.

    // TODO: node() should be able to accept other nodes as input, in which case we
    // call Node::init_copy() instead of Node::init().  This would allow us to

    // -> or perhaps Node::init() can be overloaded to accept either a node or
    // PyObject*.

    /* Construct a new node for the list. */
    template <typename... Args>
    inline Node* node(Args... args) const {
        // variadic dispatch to Node::init()
        Node* result = allocator.create(args...);
        if (specialization != nullptr && result != nullptr) {
            if (!Node::typecheck(result, specialization)) {
                recycle(result);  // clean up allocated node
                return nullptr;  // propagate TypeError()
            }
        }
        return result;
    }

    /* Release a node, returning it to the allocator. */
    inline void recycle(Node* node) const {
        allocator.recycle(node);
    }

    /* Copy a node in the list. */
    inline Node* copy(Node* node) const {
        return allocator.copy(node);
    }

    /* Link a node to its neighbors to form a linked list. */
    inline void link(Node* prev, Node* curr, Node* next) {
        // delegate to node-specific link() helper
        Node::link(prev, curr, next);

        // update list parameters
        size++;
        if (prev == nullptr) {
            head = curr;
        }
        if (next == nullptr) {
            tail = curr;
        }
    }

    /* Unlink a node from its neighbors. */
    inline void unlink(Node* prev, Node* curr, Node* next) {
        // delegate to node-specific unlink() helper
        Node::unlink(prev, curr, next);

        // update list parameters
        size--;
        if (prev == nullptr) {
            head = next;
        }
        if (next == nullptr) {
            tail = prev;
        }
    }

    //////////////////////////////
    ////    LIST INTERFACE    ////
    //////////////////////////////

    /* A SliceFactory functor that allows slice proxies to be extracted from the
    list. */
    const SliceFactory<View> slice;  // slice(), slice.normalize(), etc.

    /* Make a shallow copy of the entire list. */
    std::optional<View> copy() const {
        try {
            View result(max_size, specialization);

            // copy nodes into new list
            copy_into(result);
            if (PyErr_Occurred()) {
                return std::nullopt;
            }
            return std::make_optional(std::move(result));

        } catch (std::invalid_argument&) {
            return std::nullopt;  // propagate
        }
    }

    /* Clear the list. */
    void clear() {
        Node* curr = head;  // store temporary reference to head

        // reset list parameters
        head = nullptr;
        tail = nullptr;
        size = 0;

        // recycle all nodes
        while (curr != nullptr) {
            Node* next = static_cast<Node*>(curr->next);
            recycle(curr);
            curr = next;
        }
    }

    /* Normalize a numeric index, allowing Python-style wraparound and
    bounds checking. */
    template <typename T>
    std::optional<size_t> index(T index, bool truncate) {
        bool index_lt_zero = index < 0;

        // wraparound negative indices
        if (index_lt_zero) {
            index += size;
            index_lt_zero = index < 0;
        }

        // boundscheck
        if (index_lt_zero || index >= static_cast<T>(size)) {
            if (truncate) {
                if (index_lt_zero) {
                    return 0;
                }
                return size - 1;
            }
            PyErr_SetString(PyExc_IndexError, "list index out of range");
            return std::nullopt;
        }

        // return as size_t
        return std::optional<size_t>{ static_cast<size_t>(index) };
    }

    /* Normalize a Python integer for use as an index to the list. */
    std::optional<size_t> index(PyObject* index, bool truncate) {
        // check that index is a Python integer
        if (!PyLong_Check(index)) {
            PyErr_SetString(PyExc_TypeError, "index must be a Python integer");
            return std::nullopt;
        }

        // comparisons are kept at the python level until we're ready to return
        PyObject* py_zero = PyLong_FromSize_t(0);  // new reference
        PyObject* py_size = PyLong_FromSize_t(size);  // new reference
        int index_lt_zero = PyObject_RichCompareBool(index, py_zero, Py_LT);

        // wraparound negative indices
        bool release_index = false;
        if (index_lt_zero) {
            index = PyNumber_Add(index, py_size);  // new reference
            index_lt_zero = PyObject_RichCompareBool(index, py_zero, Py_LT);
            release_index = true;  // remember to DECREF index later
        }

        // boundscheck
        if (index_lt_zero || PyObject_RichCompareBool(index, py_size, Py_GE)) {
            // clean up references
            Py_DECREF(py_zero);
            Py_DECREF(py_size);
            if (release_index) {
                Py_DECREF(index);
            }

            // apply truncation if directed
            if (truncate) {
                if (index_lt_zero) {
                    return std::optional<size_t>{ 0 };
                }
                return std::optional<size_t>{ size - 1 };
            }

            // raise IndexError
            PyErr_SetString(PyExc_IndexError, "list index out of range");
            return std::nullopt;
        }

        // value is good - convert to size_t
        size_t result = PyLong_AsSize_t(index);

        // clean up references
        Py_DECREF(py_zero);
        Py_DECREF(py_size);
        if (release_index) {
            Py_DECREF(index);
        }

        return std::optional<size_t>{ result };
    }

    /////////////////////////////
    ////    EXTRA METHODS    ////
    /////////////////////////////

    /* A ThreadLock functor that allows the list to be locked for thread safety. */
    const Lock lock;  // lock(), lock.context(), lock.diagnostics(), etc.

    /* Enforce strict type checking for elements of this list. */
    void specialize(PyObject* spec) {
        // handle null assignment
        if (spec == nullptr) {
            if (specialization != nullptr) {
                Py_DECREF(specialization);  // remember to release old spec
                specialization = nullptr;
            }
            return;
        }

        // early return if new spec is same as old spec
        if (specialization != nullptr) {
            int comp = PyObject_RichCompareBool(spec, specialization, Py_EQ);
            if (comp == -1) {  // comparison raised an exception
                return;  // propagate error
            } else if (comp == 1) {  // spec is identical
                return;  // do nothing
            }
        }

        // check the contents of the list
        Node* curr = head;
        for (size_t i = 0; i < size; i++) {
            if (!Node::typecheck(curr, spec)) {
                return;  // propagate TypeError()
            }
            curr = static_cast<Node*>(curr->next);
        }

        // replace old specialization
        Py_INCREF(spec);
        if (specialization != nullptr) {
            Py_DECREF(specialization);
        }
        specialization = spec;
    }

    /* Get the total memory consumed by the list (in bytes). */
    inline size_t nbytes() const {
        return allocator.nbytes() + sizeof(*this);
    }

    /////////////////////////////////
    ////    ITERATOR PROTOCOL    ////
    /////////////////////////////////

    /* An IteratorFactory functor that allows iteration over the list. */
    const Iter iter;  // iter(), iter.begin(), iter.end(), etc.

    /* Return an iterator to the start of the list. */
    template <bool reverse = false>
    inline typename Iter::Iterator<reverse> begin() const {
        return iter.begin();
    }

    /* Return an iterator to the end of the list. */
    template <bool reverse = false>
    inline typename Iter::Iterator<reverse> end() const {
        return iter.end();
    }

protected:
    mutable Allocator<Node> allocator;

    /* Release the resources being managed by the ListView. */
    inline void self_destruct() {
        clear();  // clear all nodes in list
        if (specialization != nullptr) {
            Py_DECREF(specialization);
        }
    }

    /* Copy all the nodes from this list into a newly-allocated view. */
    void copy_into(View& other) const {
        Node* curr = head;
        Node* copied = nullptr;
        Node* copied_tail = nullptr;

        // copy each node in list
        while (curr != nullptr) {
            copied = copy(curr);  // copy node
            if (copied == nullptr) {  // error during copy(node)
                return;  // propagate error
            }

            // link to tail of copied list
            other.link(copied_tail, copied, nullptr);
            if (PyErr_Occurred()) {  // error during link()
                return;  // propagate error
            }

            // advance to next node
            copied_tail = copied;
            curr = static_cast<Node*>(curr->next);
        }
    }

};


///////////////////////
////    SETVIEW    ////
///////////////////////


template <typename NodeType, template <typename> class Allocator>
class SetView : public ListView<Hashed<NodeType>, Allocator> {
public:
    using Base = ListView<Hashed<NodeType>, Allocator>;
    using View = SetView<NodeType, Allocator>;
    using Node = Hashed<NodeType>;

    /* Construct an empty SetView. */
    SetView(Py_ssize_t max_size = -1, PyObject* spec = nullptr) :
        Base(max_size, spec), table()
    {}

    /* Construct a SetView from an input iterable. */
    SetView(
        PyObject* iterable,
        bool reverse = false,
        Py_ssize_t max_size = -1,
        PyObject* spec = nullptr
    ) : Base(max_size, spec), table()
    {
        // C API equivalent of iter(iterable)
        PyObject* iterator = PyObject_GetIter(iterable);
        if (iterator == nullptr) {  // TypeError()
            this->self_destruct();
            throw std::invalid_argument("Value is not iterable");
        }

        // unpack iterator into SetView
        PyObject* item;
        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == nullptr) { // end of iterator or error
                if (PyErr_Occurred()) {
                    Py_DECREF(iterator);
                    this->self_destruct();
                    throw std::invalid_argument("could not get item from iterator");
                }
                break;
            }

            // allocate a new node and link it to the list
            stage(item, reverse);
            if (PyErr_Occurred()) {
                Py_DECREF(iterator);
                Py_DECREF(item);
                this->self_destruct();
                throw std::invalid_argument("could not stage item");
            }

            // advance to next item
            Py_DECREF(item);
        }

        // release reference on iterator
        Py_DECREF(iterator);
    }

    /* Move ownership from one SetView to another (move constructor). */
    SetView(SetView&& other) : Base(std::move(other)), table(std::move(other.table)) {}

    /* Move ownership from one SetView to another (move assignment). */
    SetView& operator=(SetView&& other) {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // call parent move assignment operator
        Base::operator=(std::move(other));

        // transfer ownership of hash table
        table = std::move(other.table);
        return *this;
    }

    /* Copy a node in the list. */
    inline Node* copy(Node* node) const {
        return Base::copy(node);
    }

    /* Make a shallow copy of the entire list. */
    std::optional<View> copy() const {
        try {
            View result(this->max_size, this->specialization);

            // copy nodes into new list
            Base::copy_into(result);
            if (PyErr_Occurred()) {
                return std::nullopt;
            }

            return std::make_optional(std::move(result));
        } catch (std::invalid_argument&) {
            return std::nullopt;
        }
    }

    /* Clear the list and reset the associated hash table. */
    inline void clear() {
        Base::clear();  // free all nodes
        table.reset();  // reset hash table
    }

    /* Link a node to its neighbors to form a linked list. */
    void link(Node* prev, Node* curr, Node* next) {
        // add node to hash table
        table.remember(curr);
        if (PyErr_Occurred()) {  // node is already present in table
            return;
        }

        // delegate to ListView
        Base::link(prev, curr, next);
    }

    /* Unlink a node from its neighbors. */
    void unlink(Node* prev, Node* curr, Node* next) {
        // remove node from hash table
        table.forget(curr);
        if (PyErr_Occurred()) {  // node is not present in table
            return;
        }

        // delegate to ListView
        Base::unlink(prev, curr, next);
    }

    /* Search for a node by its value. */
    template <typename T>
    inline Node* search(T* key) const {
        // NOTE: T can be either a PyObject or node pointer.  If a node is provided,
        // then its precomputed hash will be reused if available.  Otherwise, the value
        // will be passed through `PyObject_Hash()` before searching the table.
        return table.search(key);
    }

    /* A proxy class that allows for operations relative to a particular value
    within the set. */
    class RelativeProxy {
    public:
        using View = SetView<NodeType, Allocator>;
        using Node = View::Node;

        View* view;
        Node* sentinel;
        Py_ssize_t offset;

        // TODO: truncate could be handled at the proxy level.  It would just be
        // another constructor argument

        /* Construct a new RelativeProxy for the set. */
        RelativeProxy(View* view, Node* sentinel, Py_ssize_t offset) :
            view(view), sentinel(sentinel), offset(offset)
        {}

        // TODO: relative() could just return a RelativeProxy by value, which would
        // be deleted as soon as it falls out of scope.  This means we create a new
        // proxy every time a variant method is called, but we can reuse them in a
        // C++ context.

        /* Execute a function with the RelativeProxy as its first argument. */
        template <typename Func, typename... Args>
        auto execute(Func func, Args... args) {
            // function pointer must accept a RelativeProxy* as its first argument
            using ReturnType = decltype(func(std::declval<RelativeProxy*>(), args...));

            // call function with proxy
            if constexpr (std::is_void_v<ReturnType>) {
                func(this, args...);
            } else {
                return func(this, args...);
            }
        }


        // TODO: these could maybe just get the proxy's curr(), prev(), and next()
        // nodes, respectively.  We can then derive the other nodes from whichever one
        // is populated.  For example, if we've already found and cached the prev()
        // node, then curr() is just generated by getting prev()->next, and same with
        // next() and curr()->next.  If none of the nodes are cached, then we have to
        // iterate like we do now to find and cache them.  This means that in any
        // situation where we need to get all three nodes, we should always start with
        // prev().

        /* Return the node at the proxy's current location. */
        Node* walk(Py_ssize_t offset, bool truncate) {
            // check for no-op
            if (offset == 0) {
                return sentinel;
            }

            // TODO: introduce caching for the proxy's current position.  Probably
            // need to use std::optional<Node*> for these, since nullptr might be a
            // valid value.

            // if we're traversing forward from the sentinel, then the process is the
            // same for both singly- and doubly-linked lists
            if (offset > 0) {
                curr = sentinel;
                for (Py_ssize_t i = 0; i < offset; i++) {
                    if (curr == nullptr) {
                        if (truncate) {
                            return view->tail;  // truncate to end of list
                        } else {
                            return nullptr;  // index out of range
                        }
                    }
                    curr = static_cast<Node*>(curr->next);
                }
                return curr;
            }

            // if the list is doubly-linked, we can traverse backward just as easily
            if constexpr (has_prev<Node>::value) {
                curr = sentinel;
                for (Py_ssize_t i = 0; i > offset; i--) {
                    if (curr == nullptr) {
                        if (truncate) {
                            return view->head;  // truncate to beginning of list
                        } else {
                            return nullptr;  // index out of range
                        }
                    }
                    curr = static_cast<Node*>(curr->prev);
                }
                return curr;
            }

            // Otherwise, we have to iterate from the head of the list.  We do this
            // using a two-pointer approach where the `lookahead` pointer is offset
            // from the `curr` pointer by the specified number of steps.  When it
            // reaches the sentinel, then `curr` will be at the correct position.
            Node* lookahead = view->head;
            for (Py_ssize_t i = 0; i > offset; i--) {  // advance lookahead to offset
                if (lookahead == sentinel) {
                    if (truncate) {
                        return view->head;  // truncate to beginning of list
                    } else {
                        return nullptr;  // index out of range
                    }
                }
                lookahead = static_cast<Node*>(lookahead->next);
            }

            // advance both pointers until lookahead reaches sentinel
            curr = view->head;
            while (lookahead != sentinel) {
                curr = static_cast<Node*>(curr->next);
                lookahead = static_cast<Node*>(lookahead->next);
            }
            return curr;
        }

        /* Find the left and right bounds for an insertion. */
        std::pair<Node*, Node*> junction(Py_ssize_t offset, bool truncate) {
            // get the previous node for the insertion point
            prev = walk(offset - 1, truncate);

            // apply truncate rule
            if (prev == nullptr) {  // walked off end of list
                if (!truncate) {
                    return std::make_pair(nullptr, nullptr);  // error code
                }
                if (offset < 0) {
                    return std::make_pair(nullptr, view->head);  // beginning of list
                }
                return std::make_pair(view->tail, nullptr);  // end of list
            }

            // return the previous node and its successor
            curr = static_cast<Node*>(prev->next);
            return std::make_pair(prev, curr);
        }

        /* Find the left and right bounds for a removal. */
        std::tuple<Node*, Node*, Node*> neighbors(Py_ssize_t offset, bool truncate) {
            // NOTE: we can't reuse junction() here because we need access to the node
            // preceding the tail in the event that we walk off the end of the list and
            // truncate=true.
            curr = sentinel;

            // NOTE: this is trivial for doubly-linked lists
            if constexpr (has_prev<Node>::value) {
                if (offset > 0) {  // forward traversal
                    next = static_cast<Node*>(curr->next);
                    for (Py_ssize_t i = 0; i < offset; i++) {
                        if (next == nullptr) {
                            if (truncate) {
                                break;  // truncate to end of list
                            } else {
                                return std::make_tuple(nullptr, nullptr, nullptr);
                            }
                        }
                        curr = next;
                        next = static_cast<Node*>(curr->next);
                    }
                    prev = static_cast<Node*>(curr->prev);
                } else {  // backward traversal
                    prev = static_cast<Node*>(curr->prev);
                    for (Py_ssize_t i = 0; i > offset; i--) {
                        if (prev == nullptr) {
                            if (truncate) {
                                break;  // truncate to beginning of list
                            } else {
                                return std::make_tuple(nullptr, nullptr, nullptr);
                            }
                        }
                        curr = prev;
                        prev = static_cast<Node*>(curr->prev);
                    }
                    next = static_cast<Node*>(curr->next);
                }
                return std::make_tuple(prev, curr, next);
            }

            // NOTE: It gets significantly more complicated if the list is singly-linked.
            // In this case, we can only optimize the forward traversal branch if we're
            // advancing at least one node and the current node is not the tail of the
            // list.
            if (truncate && offset > 0 && curr == view->tail) {
                offset = 0;  // skip forward iteration branch
            }

            // forward iteration (efficient)
            if (offset > 0) {
                prev = nullptr;
                next = static_cast<Node*>(curr->next);
                for (Py_ssize_t i = 0; i < offset; i++) {
                    if (next == nullptr) {  // walked off end of list
                        if (truncate) {
                            break;
                        } else {
                            return std::make_tuple(nullptr, nullptr, nullptr);
                        }
                    }
                    if (prev == nullptr) {
                        prev = curr;
                    }
                    curr = next;
                    next = static_cast<Node*>(curr->next);
                }
                return std::make_tuple(prev, curr, next);
            }

            // backward iteration (inefficient)
            Node* lookahead = view->head;
            for (size_t i = 0; i > offset; i--) {  // advance lookahead to offset
                if (lookahead == curr) {
                    if (truncate) {  // truncate to beginning of list
                        next = static_cast<Node*>(view->head->next);
                        return std::make_tuple(nullptr, view->head, next);
                    } else {  // index out of range
                        return std::make_tuple(nullptr, nullptr, nullptr);
                    }
                }
                lookahead = static_cast<Node*>(lookahead->next);
            }

            // advance both pointers until lookahead reaches sentinel
            prev = nullptr;
            Node* temp = view->head;
            while (lookahead != curr) {
                prev = temp;
                temp = static_cast<Node*>(temp->next);
                lookahead = static_cast<Node*>(lookahead->next);
            }
            next = static_cast<Node*>(temp->next);
            return std::make_tuple(prev, temp, next);
        }

    private:
        // cache the proxy's current position in the set
        Node* prev;
        Node* curr;
        Node* next;
    };

    /* Generate a proxy for a set that allows operations relative to a particular
    sentinel value. */
    template <typename T, typename Func, typename... Args>
    auto relative(T* sentinel, Py_ssize_t offset, Func func, Args... args) {
        // function pointer must accept a RelativeProxy* as its first argument
        using ReturnType = decltype(func(std::declval<RelativeProxy*>(), args...));

        // search for sentinel
        Node* sentinel_node = search(sentinel);
        if (sentinel_node == nullptr) {  // sentinel not found
            PyErr_Format(PyExc_KeyError, "%R is not contained in the set", sentinel);
            return nullptr;  // propagate
        }

        // stack-allocate a temporary proxy for the set (memory-safe)
        RelativeProxy proxy(this, sentinel_node, offset);

        // call function with proxy
        if constexpr (std::is_void_v<ReturnType>) {
            func(&proxy, args...);
        } else {
            return func(&proxy, args...);
        }
    }

    /* Clear all tombstones from the hash table. */
    inline void clear_tombstones() {
        table.clear_tombstones();
    }

    /* Get the total amount of memory consumed by the set (in bytes).  */
    inline size_t nbytes() const {
        return Base::nbytes() + table.nbytes();
    }

protected:

    /* Allocate a new node for the item and add it to the set, discarding it in
    the event of an error. */
    inline void stage(PyObject* item, bool reverse) {
        // allocate a new node
        Node* curr = this->node(item);
        if (curr == nullptr) {  // error during node initialization
            if constexpr (DEBUG) {
                // QoL - nothing has been allocated, so we don't actually free
                printf("    -> free: %s\n", repr(item));
            }
            return;
        }

        // search for node in hash table
        Node* existing = search(curr);
        if (existing != nullptr) {  // item already exists
            if constexpr (has_mapped<Node>::value) {
                // update mapped value
                Py_DECREF(existing->mapped);
                Py_INCREF(curr->mapped);
                existing->mapped = curr->mapped;
            }
            this->recycle(curr);
            return;
        }

        // link the node to the staged list
        if (reverse) {
            link(nullptr, curr, this->head);
        } else {
            link(this->tail, curr, nullptr);
        }
        if (PyErr_Occurred()) {
            this->recycle(curr);  // clean up allocated node
            return;
        }
    }

private:
    HashTable<Node> table;  // stack allocated
};


////////////////////////
////    DICTVIEW    ////
////////////////////////


// TODO: we can add a separate specialization for an LRU cache that is always
// implemented as a DictView<DoubleNode, PreAllocator>.  This would allow us to
// use a pure C++ implementation for the LRU cache, which isn't even wrapped
// in a Cython class.  We could export this as a Cython alias for use in type
// inference.  Maybe the instance factories have one of these as a C-level
// member.



// TODO: If we inherit from SetView<Mapped<NodeType>, Allocator>, then we need
// to remove the hashing-related code from Mapped<>.


template <typename NodeType, template <typename> class Allocator>
class DictView {
public:
    using Node = Mapped<NodeType>;
    Node* head;
    Node* tail;
    size_t size;

    /* Disabled copy/move constructors.  These are dangerous because we're
    manually managing memory for each node. */
    DictView(const DictView& other) = delete;       // copy constructor
    DictView& operator=(const DictView&) = delete;  // copy assignment
    DictView(DictView&&) = delete;                  // move constructor
    DictView& operator=(DictView&&) = delete;       // move assignment

    /* Construct an empty DictView. */
    DictView(Py_ssize_t max_size = -1) :
        head(nullptr), tail(nullptr), size(0), specialization(nullptr),
        table(), allocator(max_size) {}

    /* Construct a DictView from an input iterable. */
    DictView(
        PyObject* iterable,
        bool reverse = false,
        PyObject* spec = nullptr,
        Py_ssize_t max_size = -1
    ) : head(nullptr), tail(nullptr), size(0), specialization(nullptr),
        table(), allocator(max_size)
    {
        // C API equivalent of iter(iterable)
        PyObject* iterator = PyObject_GetIter(iterable);
        if (iterator == nullptr) {
            throw std::invalid_argument("Value is not iterable");
        }

        // hold reference to specialization, if given
        if (spec != nullptr) {
            Py_INCREF(spec);
        }

        // unpack iterator into DictView
        PyObject* item;
        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == nullptr) { // end of iterator or error
                if (PyErr_Occurred()) {
                    Py_DECREF(iterator);
                    self_destruct();
                    throw std::runtime_error("could not get item from iterator");
                }
                break;  // end of iterator
            }

            // allocate a new node and link it to the list
            stage(item, reverse);
            if (PyErr_Occurred()) {
                Py_DECREF(iterator);
                Py_DECREF(item);
                self_destruct();
                throw std::runtime_error("could not stage item");
            }

            // advance to next item
            Py_DECREF(item);
        }

        // release reference on iterator
        Py_DECREF(iterator);
    };

    /* Destroy a DictView and free all its resources. */
    ~DictView() {
        self_destruct();
    }

    /* Construct a new node for the list. */
    template <typename... Args>
    inline Node* node(PyObject* value, Args... args) const {
        // variadic dispatch to Node::init()
        Node* result = allocator.create(value, args...);
        if (specialization != nullptr && result != nullptr) {
            if (!Node::typecheck(result, specialization)) {
                recycle(result);  // clean up allocated node
                return nullptr;  // propagate TypeError()
            }
        }

        return result;
    }

    /* Release a node, returning it to the allocator. */
    inline void recycle(Node* node) const {
        allocator.recycle(node);
    }

    /* Copy a single node in the list. */
    inline Node* copy(Node* node) const {
        return allocator.copy(node);
    }

    /* Make a shallow copy of the list. */
    DictView<NodeType, Allocator>* copy() const {
        DictView<NodeType, Allocator>* copied = new DictView<NodeType, Allocator>();
        Node* old_node = head;
        Node* new_node = nullptr;
        Node* new_prev = nullptr;

        // copy each node in list
        while (old_node != nullptr) {
            new_node = copy(old_node);  // copy node
            if (new_node == nullptr) {  // error during copy()
                delete copied;  // discard staged list
                return nullptr;
            }

            // link to tail of copied list
            copied->link(new_prev, new_node, nullptr);
            if (PyErr_Occurred()) {  // error during link()
                delete copied;  // discard staged list
                return nullptr;
            }

            // advance to next node
            new_prev = new_node;
            old_node = static_cast<Node*>(old_node->next);
        }

        // return copied view
        return copied;
    }

    /* Clear the list and reset the associated hash table. */
    inline void clear() {
        purge_list();  // free all nodes
        table.reset();  // reset hash table to initial size
    }

    /* Link a node to its neighbors to form a linked list. */
    void link(Node* prev, Node* curr, Node* next) {
        // add node to hash table
        table.remember(curr);
        if (PyErr_Occurred()) {
            return;
        }

        // delegate to node-specific link() helper
        Node::link(prev, curr, next);

        // update list parameters
        size++;
        if (prev == nullptr) {
            head = curr;
        }
        if (next == nullptr) {
            tail = curr;
        }
    }

    /* Unlink a node from its neighbors. */
    void unlink(Node* prev, Node* curr, Node* next) {
        // remove node from hash table
        table.forget(curr);
        if (PyErr_Occurred()) {
            return;
        }

        // delegate to node-specific unlink() helper
        Node::unlink(prev, curr, next);

        // update list parameters
        size--;
        if (prev == nullptr) {
            head = next;
        }
        if (next == nullptr) {
            tail = prev;
        }
    }

    /* Enforce strict type checking for elements of this list. */
    void specialize(PyObject* spec) {
        // check the contents of the list
        if (spec != nullptr) {
            Node* curr = head;
            for (size_t i = 0; i < size; i++) {
                if (!Node::typecheck(curr, spec)) {
                    return;  // propagate TypeError()
                }
                curr = static_cast<Node*>(curr->next);
            }
            Py_INCREF(spec);
        }

        // replace old specialization
        if (specialization != nullptr) {
            Py_DECREF(specialization);
        }
        specialization = spec;
    }

    /* Get the type specialization for elements of this list. */
    inline PyObject* get_specialization() const {
        if (specialization != nullptr) {
            Py_INCREF(specialization);
        }
        return specialization;  // return a new reference or NULL
    }

    /* Search for a node by its value. */
    inline Node* search(PyObject* value) const {
        return table.search(value);
    }

    /* Search for a node by its value. */
    inline Node* search(Node* value) const {
        return table.search(value);
    }

    /* Search for a node and move it to the front of the list at the same time. */
    inline Node* lru_search(PyObject* value) {
        // move node to head of list
        Node* curr = table.search(value);
        if (curr != nullptr && curr != head) {
            if (curr == tail) {
                tail = static_cast<Node*>(curr->prev);
            }
            Node* prev = static_cast<Node*>(curr->prev);
            Node* next = static_cast<Node*>(curr->next);
            Node::unlink(prev, curr, next);
            Node::link(nullptr, curr, head);
            head = curr;
        }

        return curr;
    }

    /* Clear all tombstones from the hash table. */
    inline void clear_tombstones() {
        table.clear_tombstones();
    }

    /* Get the total amount of memory consumed by the dictionary (in bytes). */
    inline size_t nbytes() const {
        return allocator.nbytes() + table.nbytes() + sizeof(*this);
    }

private:
    PyObject* specialization;  // specialized type for elements of this list
    mutable Allocator<Node>allocator;  // stack allocated
    HashTable<Node> table;  // stack allocated

    /* Allocate a new node for the item and append it to the list, discarding
    it in the event of an error. */
    void stage(PyObject* item, bool reverse) {
        // allocate a new node
        Node* curr = node(item);
        if (PyErr_Occurred()) {
            // QoL - nothing has been allocated, so we don't actually free anything
            if constexpr (DEBUG) {
                printf("    -> free: %s\n", repr(item));
            }
            return;
        }

        // link the node to the staged list
        if (reverse) {
            link(nullptr, curr, head);
        } else {
            link(tail, curr, nullptr);
        }
        if (PyErr_Occurred()) {  // node already exists
            recycle(curr);
            return;
        }
    }

    /* Clear all nodes in the list. */
    void purge_list() {
        // NOTE: this does not reset the hash table, and is therefore unsafe.
        // It should only be used to destroy a DictView or clear its contents.
        Node* curr = head;  // store temporary reference to head

        // reset list parameters
        head = nullptr;
        tail = nullptr;
        size = 0;

        // recycle all nodes
        while (curr != nullptr) {
            Node* next = static_cast<Node*>(curr->next);
            recycle(curr);
            curr = next;
        }
    }

    /* Release the resources being managed by the DictView. */
    inline void self_destruct() {
        // NOTE: allocator and table are stack allocated, so they don't need to
        // be freed here.  Their destructors will be called automatically when
        // the DictView is destroyed.
        purge_list();
        if (specialization != nullptr) {
            Py_DECREF(specialization);
        }
    }

};


///////////////////////////
////    VIEW TRAITS    ////
///////////////////////////


/* A trait that detects whether the templated view is set-like (i.e. has a
search() method). */
template <typename View>
struct is_setlike {
private:
    // Helper template to detect whether View has a search() method
    template <typename T>
    static auto test(T* t) -> decltype(t->search(nullptr), std::true_type());

    // Overload for when View does not have a search() method
    template <typename T>
    static std::false_type test(...);

public:
    static constexpr bool value = decltype(test<View>(nullptr))::value;
};


///////////////////////////////
////    VIEW DECORATORS    ////
///////////////////////////////


// TODO: Sorted<> becomes a decorator for a view, not a node.  It automatically
// converts a view of any type into a sorted view, which stores its nodes in a
// skip list.  This makes the sortedness immutable, and blocks operations that
// would unsort the list.  Every node in the list is decorated with a key value
// that is supplied by the user.  This key is provided in the constructor, and
// is cached on the node itself under a universal `key` attribute.  The SortKey
// template parameter defines what is stored in this key, and under what
// circumstances it is modified.

// using MFUCache = typename Sorted<DictView<DoubleNode>, Frequency, Descending>;

// This would create a doubly-linked skip list where each node maintains a
// value, mapped value, frequency count, hash, and prev/next pointers.  The
// view itself would maintain a hash map for fast lookups.  If the default
// SortKey is used, then we can also make the the index() method run in log(n)
// by exploiting the skip list.  These can be specific overloads in the methods
// themselves.

// This decorator can be extended to any of the existing views.



// template <
//     template <typename> class ViewType,
//     typename NodeType,
//     typename SortKey = Value,
//     typename SortOrder = Ascending
// >
// class Sorted : public ViewType<NodeType> {
// public:
//     /* A node decorator that maintains vectors of next and prev pointers for use in
//     sorted, skip list-based data structures. */
//     struct Node : public ViewType::Node {
//         std::vector<Node*> skip_next;
//         std::vector<Node*> skip_prev;

//         /* Initialize a newly-allocated node. */
//         inline static Node* init(Node* node, PyObject* value) {
//             node = static_cast<Node*>(NodeType::init(node, value));
//             if (node == nullptr) {  // Error during decorated init()
//                 return nullptr;  // propagate
//             }

//             // NOTE: skip_next and skip_prev are stack-allocated, so there's no
//             // need to initialize them here.

//             // return initialized node
//             return node;
//         }

//         /* Initialize a copied node. */
//         inline static Node* init_copy(Node* new_node, Node* old_node) {
//             // delegate to templated init_copy() method
//             new_node = static_cast<Node*>(NodeType::init_copy(new_node, old_node));
//             if (new_node == nullptr) {  // Error during templated init_copy()
//                 return nullptr;  // propagate
//             }

//             // copy skip pointers
//             new_node->skip_next = old_node->skip_next;
//             new_node->skip_prev = old_node->skip_prev;
//             return new_node;
//         }

//         /* Tear down a node before freeing it. */
//         inline static void teardown(Node* node) {
//             node->skip_next.clear();  // reset skip pointers
//             node->skip_prev.clear();
//             NodeType::teardown(node);
//         }

//         // TODO: override link() and unlink() to update skip pointers and maintain
//         // sorted order
//     }
// }


// ////////////////////////
// ////    POLICIES    ////
// ////////////////////////


// // TODO: Value and Frequency should be decorators for nodes to give them full
// // type information.  They can even wrap


// /* A SortKey that stores a reference to a node's value in its key. */
// struct Value {
//     /* Decorate a freshly-initialized node. */
//     template <typename Node>
//     inline static void decorate(Node* node) {
//         node->key = node->value;
//     }

//     /* Clear a node's sort key. */
//     template <typename Node>
//     inline static void undecorate(Node* node) {
//         node->key = nullptr;
//     }
// };


// /* A SortKey that stores a frequency counter as a node's key. */
// struct Frequency {
//     /* Initialize a node's sort key. */
//     template <typename Node>
//     inline static void decorate(Node* node) {
//         node->key = 0;
//     }

//     /* Clear a node's sort key */

// };


// /* A Sorted<> policy that sorts nodes in ascending order based on key. */
// template <typename SortValue>
// struct Ascending {
//     /* Check whether two keys are in sorted order relative to one another. */
//     template <typename KeyValue>
//     inline static bool compare(KeyValue left, KeyValue right) {
//         return left <= right;
//     }

//     /* A specialization for compare to use with Python objects as keys. */
//     template <>
//     inline static bool compare(PyObject* left, PyObject* right) {
//         return PyObject_RichCompareBool(left, right, Py_LE);  // -1 signals error
//     }
// };


// /* A specialized version of Ascending that compares PyObject* references. */
// template <>
// struct Ascending<Value> {
    
// };


// /* A Sorted<> policy that sorts nodes in descending order based on key. */
// struct Descending {
//     /* Check whether two keys are in sorted order relative to one another. */
//     template <typename KeyValue>
//     inline static int compare(KeyValue left, KeyValue right) {
//         return left >= right;
//     }

//     /* A specialization for compare() to use with Python objects as keys. */
//     template <>
//     inline static int compare(PyObject* left, PyObject* right) {
//         return PyObject_RichCompareBool(left, right, Py_GE);  // -1 signals error
//     }
// };


#endif // BERTRAND_STRUCTS_CORE_VIEW_H include guard
