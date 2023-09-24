// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_UTIL_H
#define BERTRAND_STRUCTS_CORE_UTIL_H

#include <string>  // std::string
#include <typeinfo>  // std::bad_typeid
#include <type_traits>  // std::decay_t<>
#include <Python.h>  // CPython API
#include "node.h"  // has_prev<>


/////////////////////////
////    UTILITIES    ////
/////////////////////////


/* Round a number up to the nearest power of two. */
template <typename T, std::enable_if_t<std::is_unsigned_v<T>, int> = 0>
inline T next_power_of_two(T n) {
    constexpr size_t bits = sizeof(T) * 8;
    --n;
    for (size_t i = 1; i < bits; i <<= 2) {
        n |= (n >> i);
    }
    return ++n;
}


//////////////////////
////    ERRORS    ////
//////////////////////


/* Convert the most recent Python error into a C++ exception. */
template <typename Exception>
Exception catch_python() {
    // sanity check
    static_assert(
        std::is_constructible_v<Exception, std::string>,
        "Exception type must be constructible from std::string"
    );

    PyObject* exc_type;  // PyErr_Fetch() initializes these for us
    PyObject* exc_value;
    PyObject* exc_traceback;

    // Get the most recent Python error
    PyErr_Fetch(&exc_type, &exc_value, &exc_traceback);  // clears error indicator
    PyErr_NormalizeException(&exc_type, &exc_value, &exc_traceback);

    // Get the error message from the exception if one exists
    std::string msg("Unknown error");
    if (exc_type != nullptr) {
        // Get msg from exc_value.__str__()
        PyObject* str = PyObject_Str(exc_value);
        if (str == nullptr) {
            msg = "Unknown error (could not get exception message)";
        } else if (!PyUnicode_Check(str)) {
            msg = "Unknown error (exception message is not a string)";
        } else {
            const char* utf8_str = PyUnicode_AsUTF8(str);
            if (utf8_str == nullptr) {
                msg = "Unknown error (exception message failed UTF-8 conversion)";
            } else {
                msg = utf8_str;
            }
        }
        Py_XDECREF(str);  // release python string
    }

    // Decrement reference counts
    Py_XDECREF(exc_type);
    Py_XDECREF(exc_value);
    Py_XDECREF(exc_traceback);

    return Exception(msg);
}


/* Subclass std::bad_typeid() to allow automatic conversion to Python TypeError by
Cython. */
class type_error : public std::bad_typeid {
private:
    std::string message;

public:
    using std::bad_typeid::bad_typeid;  // inherit default constructors

    /* Allow construction from a custom error message. */
    type_error(const std::string& what) : message(what) {}
    type_error(const char* what) : message(what) {}

    const char* what() const noexcept override { return message.c_str(); }
};


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
template <typename IteratorType>
class CoupledIterator {
public:
    using Iterator = IteratorType;

    // iterator tags for std::iterator_traits
    using iterator_category     = typename Iterator::iterator_category;
    using difference_type       = typename Iterator::difference_type;
    using value_type            = typename Iterator::value_type;
    using pointer               = typename Iterator::pointer;
    using reference             = typename Iterator::reference;

    // couple the begin() and end() iterators into a single object
    CoupledIterator(const Iterator& first, const Iterator& second) :
        first(std::move(first)), second(std::move(second))
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


/* A wrapper around a C++ iterator that allows it to be used from Python. */
template <typename Iterator, const char* type_name>
class PyIterator {
private:
    using Self = PyIterator<Iterator, type_name>;

    // sanity check
    static_assert(
        std::is_same_v<typename Iterator::value_type, PyObject*>,
        "Iterator must dereference to PyObject*"
    );

    /* Store coupled iterators as raw data buffers.
    
    NOTE: PyObject_New() does not allow for traditional stack allocation like we would
    normally use to store the wrapped iterators.  Instead, we have to be able to assign
    the iterators after construction.  We could use pointers to heap-allocated memory
    for this, but this adds extra allocation overhead.  Using raw data buffers avoids
    this and places the iterators on the stack, where they belong. */
    PyObject_HEAD
    alignas(Iterator) char first[sizeof(Iterator)];  // access via reinterpret_cast
    alignas(Iterator) char second[sizeof(Iterator)];

    /* Force users to use create() factory method. */
    PyIterator() = delete;
    PyIterator(const Self&) = delete;
    PyIterator(Self&&) = delete;

    /* Initialize a PyTypeObject to represent this iterator from Python. */
    static PyTypeObject init_type() {
        PyTypeObject type_obj = {};  // zero-initialize
        type_obj.tp_name = type_name;
        type_obj.tp_doc = "Python-compatible wrapper around a C++ iterator.";
        type_obj.tp_flags = Py_TPFLAGS_DEFAULT;
        type_obj.tp_new = PyType_GenericNew;
        type_obj.tp_iter = PyObject_SelfIter;
        type_obj.tp_iternext = iter_next;
        type_obj.tp_basicsize = sizeof(Self);
        type_obj.tp_itemsize = 0;
        type_obj.tp_dealloc = dealloc;

        // register iterator type with Python
        if (PyType_Ready(&type_obj) < 0) {
            throw std::runtime_error("could not initialize PyIterator type");
        }
        return type_obj;
    }

public:
    /* C-style Python type declaration. */
    inline static PyTypeObject Type = init_type();

    /* Construct a Python iterator from a C++ iterator range. */
    static Self* create(Iterator&& begin, Iterator&& end) {
        // create new iterator instance
        Self* result = PyObject_New(Self, &Type);
        if (result == nullptr) {
            PyErr_SetString(PyExc_RuntimeError, "could not allocate PyIterator");
            return nullptr;  // propagate error
        }

        // initialize iterators into raw storage
        new (&result->first) Iterator(std::move(begin));  // placement new
        new (&result->second) Iterator(std::move(end));
        return result;
    }

    /* Construct a Python iterator from a coupled iterator. */
    inline static Self* create(CoupledIterator<Iterator>&& iter) {
        return create(iter.begin(), iter.end());
    }

    /* Free the Python iterator when its reference count falls to zero. */
    inline static void dealloc(PyObject* self) {
        Self* ref = reinterpret_cast<Self*>(self);
        reinterpret_cast<Iterator&>(ref->first).~Iterator();
        reinterpret_cast<Iterator&>(ref->second).~Iterator();
        Py_TYPE(self)->tp_free(self);
    }

    /* Call next(iter) from Python. */
    inline static PyObject* iter_next(PyObject* self) {
        Self* ref = reinterpret_cast<Self*>(self);
        Iterator& begin = reinterpret_cast<Iterator&>(ref->first);
        Iterator& end = reinterpret_cast<Iterator&>(ref->second);

        if (!(begin != end)) {  // terminate the sequence
            PyErr_SetNone(PyExc_StopIteration);
            return nullptr;
        }

        // increment iterator and return current value
        PyObject* result = *begin;
        ++begin;
        return Py_NewRef(result);  // new reference
    }

};


/* A wrapper around a Python iterator that manages reference counts and enables
for-each loop syntax in C++. */
class PyIterable {
public:
    class Iterator;
    using IteratorPair = CoupledIterator<Iterator>;

    /* Construct a PyIterable from a Python sequence. */
    PyIterable(PyObject* seq) : py_iterator(PyObject_GetIter(seq)) {
        if (py_iterator == nullptr) {
            throw std::invalid_argument("could not get iter(sequence)");
        }
    }

    /* Release the Python sequence. */
    ~PyIterable() { Py_DECREF(py_iterator); }

    /* Iterate over the sequence. */
    inline IteratorPair iter() const { return IteratorPair(begin(), end()); }
    inline Iterator begin() const { return Iterator(py_iterator); }
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
            curr = PyIter_Next(py_iterator);
            if (curr == nullptr && PyErr_Occurred()) {
                throw std::runtime_error("could not get next(iterator)");
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
        PyObject* py_iterator;
        PyObject* curr;

        /* Return an iterator to the start of the sequence. */
        Iterator(PyObject* py_iterator) : py_iterator(py_iterator), curr(nullptr) {
            if (py_iterator != nullptr) {
                curr = PyIter_Next(py_iterator);
                if (curr == nullptr && PyErr_Occurred()) {
                    throw std::runtime_error("could not get next(iterator)");
                }
            }
        }

        /* Return an iterator to the end of the sequence. */
        Iterator() : py_iterator(nullptr), curr(nullptr) {}
    };

protected:
    PyObject* py_iterator;
};


/* A wrapper around a fast Python sequence (list or tuple) that manages reference
counts and simplifies access. */
class PySequence {
public:

    /* Construct a PySequence from an iterable or other sequence. */
    PySequence(PyObject* items, const char* err_msg = "could not get sequence") :
        sequence(PySequence_Fast(items, err_msg)),
        length(static_cast<size_t>(PySequence_Fast_GET_SIZE(sequence)))
    {
        if (sequence == nullptr) {
            throw catch_python<type_error>();  // propagate error
        }
    }

    /* Release the Python sequence on destruction. */
    ~PySequence() { Py_DECREF(sequence); }

    /* Get the length of the sequence. */
    inline size_t size() const { return length; }

    /* Iterate over the sequence. */
    inline PyIterable iter() const { return PyIterable(sequence); }

    /* Get underlying PyObject* array. */
    inline PyObject** array() const { return PySequence_Fast_ITEMS(sequence); }

    /* Get the value at a particular index of the sequence. */
    inline PyObject* operator[](size_t index) const {
        if (index >= length) {
            throw std::out_of_range("index out of range");
        }
        return PySequence_Fast_GET_ITEM(sequence, index);  // borrowed reference
    }

protected:
    PyObject* sequence;
    size_t length;
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


/* enum to make iterator direction hints more readable. */
enum class Direction {
    forward,
    backward
};


/* Conditionally-compiled base class for Bidirectional iterators that respects the
reversability of the associated view. */
template <template <Direction> class Iterator, bool doubly_linked = false>
class BidirectionalBase {
public:
    const bool backward;

protected:
    union {
        Iterator<Direction::forward> forward;
    } it;

    /* Forward iteration only. */
    BidirectionalBase(const Iterator<Direction::forward>& iter) :
        backward(false), it{.forward = iter}
    {}

    /* Copy constructor. */
    BidirectionalBase(const BidirectionalBase& other) :
        backward(other.backward), it{.forward = other.it.forward}
    {}

    /* Move constructor. */
    BidirectionalBase(BidirectionalBase&& other) noexcept :
        backward(other.backward), it{.forward = std::move(other.it.forward)}
    {}

    /* Assignment operators.  These are deleted due to const members. */
    BidirectionalBase& operator=(const BidirectionalBase&) = delete;
    BidirectionalBase& operator=(BidirectionalBase&&) = delete;

    /* Call the contained type's destructor. */
    ~BidirectionalBase() { it.forward.~Iterator(); }
};


/* Specialization for doubly-linked lists. */
template <template <Direction> class Iterator>
class BidirectionalBase<Iterator, true> {
private:
    // delegated constructors for copy/move semantics
    BidirectionalBase(bool is_backward, const Iterator<Direction::forward>& iter) 
        : backward(is_backward), it{.forward = iter} {}
    BidirectionalBase(bool is_backward, const Iterator<Direction::backward>& iter) 
        : backward(is_backward), it{.backward = iter} {}

public:
    const bool backward;

protected:
    union {
        Iterator<Direction::forward> forward;
        Iterator<Direction::backward> backward;
    } it;

    /* Forward and backward variants allowed. */
    BidirectionalBase(const Iterator<Direction::forward>& iter) :
        backward(false), it{.forward = iter}
    {}
    BidirectionalBase(const Iterator<Direction::backward>& iter) :
        backward(true), it{.backward = iter}
    {}

    /* Copy constructor. */
    BidirectionalBase(const BidirectionalBase& other) :
        backward(other.backward),
        it(other.backward ? decltype(it){.backward = other.it.backward}
                          : decltype(it){.forward = other.it.forward})
    {}

    /* Move constructor. */
    BidirectionalBase(BidirectionalBase&& other) noexcept :
        backward(other.backward),
        it(other.backward ? decltype(it){.backward = std::move(other.it.backward)}
                          : decltype(it){.forward = std::move(other.it.forward)})
    {}

    /* Assignment operators.  These are deleted due to const members. */
    BidirectionalBase& operator=(const BidirectionalBase&) = delete;
    BidirectionalBase& operator=(BidirectionalBase&&) = delete;

    /* Call the contained type's destructor. */
    ~BidirectionalBase() {
        if (backward) {
            it.backward.~Iterator();
        } else {
            it.forward.~Iterator();
        }
    }
};


/* A type-erased iterator that can contain either a forward or backward iterator. */
template <template <Direction> class Iterator>
class Bidirectional : public BidirectionalBase<
    Iterator,
    has_prev<typename Iterator<Direction::forward>::Node>::value
> {
public:
    using ForwardIterator = Iterator<Direction::forward>;
    using Node = typename ForwardIterator::Node;
    inline static constexpr bool doubly_linked = has_prev<Node>::value;
    using Base = BidirectionalBase<Iterator, doubly_linked>;

    // iterator tags for std::iterator_traits
    using iterator_category     = typename ForwardIterator::iterator_category;
    using difference_type       = typename ForwardIterator::difference_type;
    using value_type            = typename ForwardIterator::value_type;
    using pointer               = typename ForwardIterator::pointer;
    using reference             = typename ForwardIterator::reference;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Initialize the union using an existing iterator. */
    template <Direction dir>
    explicit Bidirectional(const Iterator<dir>& it) : Base(it) {}

    /* Copy constructor. */
    Bidirectional(const Bidirectional& other) : Base(other) {}

    /* Move constructor. */
    Bidirectional(Bidirectional&& other) noexcept : Base(std::move(other)) {}

    // destructor is automatically called by BidirectionalBase

    /////////////////////////////////
    ////    ITERATOR PROTOCOL    ////
    /////////////////////////////////

    /* Dereference the iterator to get the node at the current position. */
    inline value_type operator*() const {
        /*
         * HACK: we rely on a special property of the templated iterators: that both
         * forward and backward iterators use the same implementation of the
         * dereference operator.  This, coupled with the fact that unions occupy the
         * same space in memory, means that we can safely dereference the iterators
         * using only the forward operator, even when the data we access is taken from
         * the backward iterator.  This avoids the need for any extra branches.
         *
         * If this specific implementation detail ever changes, then this hack should
         * be reconsidered in favor of a solution more like the one below.
         */
        return *(this->it.forward);

        /* 
         * if constexpr (doubly_linked) {
         *     if (this->backward) {
         *         return *(this->it.backward);
         *     }
         * }
         * return *(this->it.forward);
         */
    }

    /* Prefix increment to advance the iterator to the next node in the slice. */
    inline Bidirectional& operator++() {
        if constexpr (doubly_linked) {
            if (this->backward) {
                ++(this->it.backward);
                return *this;
            }
        }
        ++(this->it.forward);
        return *this;
    }

    /* Inequality comparison to terminate the slice. */
    inline bool operator!=(const Bidirectional& other) const {
        /*
         * HACK: We rely on a special property of the templated iterators: that both
         * forward and backward iterators can be safely compared using the same operator
         * implementation between them.  This, coupled with the fact that unions occupy
         * the same space in memory, means that we can directly compare the iterators
         * without any extra branches, regardless of which type is currently active.
         *
         * In practice, what this solution does is always use the forward-to-forward
         * comparison operator, but using data from the backward iterator if it is
         * currently active.  This is safe becase the backward-to-backward comparison
         * is exactly the same as the forward-to-forward comparison, as are all the
         * other possible combinations.  In other words, the directionality of the
         * iterator is irrelevant to the comparison.
         *
         * If these specific implementation details ever change, then this hack should
         * be reconsidered in favor of a solution more like the one below.
         */
        return this->it.forward != other.it.forward;

        /*
         * using OtherNode = typename std::decay_t<decltype(other)>::Node;
         *
         * if constexpr (doubly_linked) {
         *     if (this->backward) {
         *         if constexpr (has_prev<OtherNode>::value) {
         *             if (other.backward) {
         *                 return this->it.backward != other.it.backward;
         *             }
         *         }
         *         return this->it.backward != other.it.forward;
         *     }
         * }
         *
         * if constexpr (has_prev<OtherNode>::value) {
         *     if (other.backward) {
         *         return this->it.forward != other.it.backward;
         *     }
         * }
         * return this->it.forward != other.it.forward;
         */
    }

    ///////////////////////////////////
    ////    CONDITIONAL METHODS    ////
    ///////////////////////////////////

    // NOTE: this uses SFINAE to detect the presence of these methods on the template
    // Iterator.  If the Iterator does not implement the named method, then it will not
    // be compiled, and users will get compile-time errors if they try to access it.
    // This avoids the need to manually extend the Bidirectional interface to match
    // that of the Iterator.  See https://en.cppreference.com/w/cpp/language/sfinae
    // for more information.

    /* Insert a node at the current position. */
    template <typename T = ForwardIterator>
    inline auto insert(value_type value) -> decltype(std::declval<T>().insert(value)) {
        if constexpr (doubly_linked) {
            if (this->backward) {
                return this->it.backward.insert(value);
            }
        }
        return this->it.forward.insert(value);
    }

    /* Remove the node at the current position. */
    template <typename T = ForwardIterator>
    inline auto remove() -> decltype(std::declval<T>().remove()) {
        if constexpr (doubly_linked) {
            if (this->backward) {
                return this->it.backward.remove();
            }
        }
        return this->it.forward.remove();
    }

    /* Replace the node at the current position. */
    template <typename T = ForwardIterator>
    inline auto replace(value_type value) -> decltype(std::declval<T>().replace(value)) {
        if constexpr (doubly_linked) {
            if (this->backward) {
                return this->it.backward.replace(value);
            }
        }
        return this->it.forward.replace(value);
    }

    /* Get the index of the current position. */
    template <typename T = ForwardIterator>
    inline auto index() -> decltype(std::declval<T>().index()) const {
        if constexpr (doubly_linked) {
            if (this->backward) {
                return this->it.backward.index();
            }
        }
        return this->it.forward.index();
    }

    /* Check whether the iterator direction is consistent with a slice's step size. */
    template <typename T = ForwardIterator>
    inline auto inverted() -> decltype(std::declval<T>().inverted()) const {
        if constexpr (doubly_linked) {
            if (this->backward) {
                return this->it.backward.inverted();
            }
        }
        return this->it.forward.inverted();
    }

};


#endif  // BERTRAND_STRUCTS_CORE_UTIL_H
