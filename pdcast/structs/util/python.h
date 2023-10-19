// include guard: BERTRAND_STRUCTS_UTIL_PYTHON_H
#ifndef BERTRAND_STRUCTS_UTIL_PYTHON_H
#define BERTRAND_STRUCTS_UTIL_PYTHON_H

#include <string_view>  // std::string_view
#include <Python.h>  // CPython API
#include "coupled_iter.h"  // CoupledIterator
#include "slot.h"  // Slot
#include "string.h"  // PyName


/* NOTE: This file contains a collection of helper classes for interacting with the
 * Python C API using C++ RAII principles.  This allows automated handling of reference
 * counts and other memory management concerns, and simplifies overall communication
 * between C++ and Python.
 */


namespace bertrand {
namespace structs {
namespace util {


////////////////////////////////
////    PYTHON ITERABLES    ////
////////////////////////////////


/* NOTE: Python iterables are somewhat tricky to interact with from C++.  Reference
 * counts have to be carefully managed to avoid memory leaks, and the C API is not
 * particularly intuitive.  These helper classes simplify the interface and allow us to
 * use RAII to automatically manage reference counts.
 * 
 * PyIterator is a wrapper around a C++ iterator that allows it to be used from Python.
 * It can only be used for iterators that dereference to PyObject*, and it uses a
 * manually-defined PyTypeObject (whose name must be given as a template argument) to
 * expose the iterator to Python.  This type defines the __iter__() and __next__()
 * magic methods, which are used to implement the iterator protocol in Python.
 * 
 * PyIterable is essentially the inverse.  It represents a C++ wrapper around a Python
 * iterator that defines the __iter__() and __next__() magic methods.  The wrapper can
 * be iterated over using normal C++ syntax, and it automatically manages reference
 * counts for both the iterator itself and each element as we access them.
 * 
 * PySequence is a C++ wrapper around a Python sequence (list or tuple) that allows
 * elements to be accessed by index.  It corresponds to the PySequence_FAST() family of
 * C API functions.  Just like PyIterable, the wrapper automatically manages reference
 * counts for the sequence and its contents as they are accessed.
 */


/* A wrapper around a C++ iterator that allows it to be used from Python. */
template <typename Iterator>
class PyIterator {
    // sanity check
    static_assert(
        std::is_convertible_v<typename Iterator::value_type, PyObject*>,
        "Iterator must dereference to PyObject*"
    );

    /* Store coupled iterators as raw data buffers.
    
    NOTE: PyObject_New() does not allow for traditional stack allocation like we would
    normally use to store the wrapped iterators.  Instead, we have to delay construction
    until the init() method is called.  We could use pointers to heap-allocated memory
    for this, but this adds extra allocation overhead.  Using raw data buffers avoids
    this and places the iterators on the stack, where they belong. */
    PyObject_HEAD
    Slot<Iterator> first;
    Slot<Iterator> second;

    /* Force users to use init() factory method. */
    PyIterator() = delete;
    PyIterator(const PyIterator&) = delete;
    PyIterator(PyIterator&&) = delete;

public:
    /* Construct a Python iterator from a C++ iterator range. */
    inline static PyObject* init(Iterator&& begin, Iterator&& end) {
        // create new iterator instance
        PyIterator* result = PyObject_New(PyIterator, &Type);
        if (result == nullptr) {
            throw std::runtime_error("could not allocate Python iterator");
        }

        // initialize (NOTE: PyObject_New() does not call stack constructors)
        new (&(result->first)) Slot<Iterator>();
        new (&(result->second)) Slot<Iterator>();

        // construct iterators within raw storage
        result->first.construct(std::move(begin));
        result->second.construct(std::move(end));

        // return as PyObject*
        return reinterpret_cast<PyObject*>(result);
    }

    /* Construct a Python iterator from a coupled iterator. */
    inline static PyObject* init(CoupledIterator<Iterator>&& iter) {
        return init(iter.begin(), iter.end());
    }

    /* Call next(iter) from Python. */
    inline static PyObject* iter_next(PyIterator* self) {
        Iterator& begin = *(self->first);
        Iterator& end = *(self->second);

        if (!(begin != end)) {  // terminate the sequence
            PyErr_SetNone(PyExc_StopIteration);
            return nullptr;
        }

        // increment iterator and return current value
        PyObject* result = *begin;
        ++begin;
        return Py_NewRef(result);  // new reference
    }

    /* Free the Python iterator when its reference count falls to zero. */
    inline static void dealloc(PyIterator* self) {
        Type.tp_free(self);
    }

private:
    /* Initialize a PyTypeObject to represent this iterator from Python. */
    static PyTypeObject init_type() {
        PyTypeObject type_obj;  // zero-initialize
        type_obj.tp_name = PyName<Iterator>::value.data();
        type_obj.tp_doc = "Python-compatible wrapper around a C++ iterator.";
        type_obj.tp_basicsize = sizeof(PyIterator);
        type_obj.tp_flags = (
            Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE |
            Py_TPFLAGS_DISALLOW_INSTANTIATION
        );
        type_obj.tp_alloc = PyType_GenericAlloc;
        type_obj.tp_iter = PyObject_SelfIter;
        type_obj.tp_iternext = (iternextfunc) iter_next;
        type_obj.tp_dealloc = (destructor) dealloc;

        // register iterator type with Python
        if (PyType_Ready(&type_obj) < 0) {
            throw std::runtime_error("could not initialize PyIterator type");
        }
        return type_obj;
    }

    /* C-style Python type declaration. */
    inline static PyTypeObject Type = init_type();

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


}  // namespace util
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_UTIL_PYTHON_H
