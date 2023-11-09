"""Cython headers for pdcast/util/structs/list/hashed.pyx"""
from cpython.ref cimport PyObject
from libcpp.optional cimport optional, nullopt

from .base cimport Slot


# NOTE: LinkedSet is a compound class with both a C++ and Python implementation.  Both
# versions are virtually identical, but the C++ implementation can store non-Python
# values and has slightly higher performance due to reduced indirection.  Otherwise,
# they are exactly the same, and can be easily ported from one to another.  In fact,
# the Cython class is actually just a thin wrapper around a C++ LinkedSet that casts
# its inputs to and from a Python runtime.  Different template configurations can be
# selected via constructor arguments in the Cython class.


###################
####    C++    ####
###################



######################
####    CYTHON    ####
######################


cdef extern from "set_cython.h" namespace "bertrand::structs::linked::cython":
    cdef cppclass CyLinkedSet:
        # constructors
        CyLinkedSet(
            optional[size_t] max_size,
            PyObject* spec,
            bint singly_linked
        ) except +
        CyLinkedSet(
            PyObject* iterable,
            optional[size_t] max_size,
            PyObject* spec,
            bint reverse,
            bint singly_linked
        ) except +

        # low-level interface
        bint empty()
        size_t size()
        size_t capacity()
        optional[size_t] max_size()
        bint dynamic()
        bint frozen()
        void reserve(size_t capacity) except +  # TODO: not void
        void defragment() except +
        PyObject* specialization()
        void specialize(PyObject* spec) except +
        size_t nbytes()
        PyObject* iter() except +
        PyObject* riter() except +

        # list interface
        cppclass Lock:
            PyObject* operator()() except +
            PyObject* shared() except +
            size_t count()
            size_t duration()
            double contention()
            void reset_diagnostics()
        const Lock lock

        # set interface
        void add(PyObject* item, bint left) except *
        void discard(PyObject* item) except *
        int isdisjoint(PyObject* other) except -1
        int issubset(PyObject* other, bint strict) except -1
        int issuperset(PyObject* other, bint strict) except -1
        CyLinkedSet* union_(PyObject* other, bint left) except NULL
        CyLinkedSet* intersection(PyObject* other) except NULL
        CyLinkedSet* difference(PyObject* other) except NULL
        CyLinkedSet* symmetric_difference(PyObject* other) except NULL
        void update(PyObject* items, bint left) except *
        void intersection_update(PyObject* items) except *
        void difference_update(PyObject* items) except *
        void symmetric_difference_update(PyObject* items) except *

        # extra methods
        Py_ssize_t distance(PyObject* item1, PyObject* item2) except? MAX_SIZE_T
        void swap(PyObject* item1, PyObject* item2) except *
        void move(PyObject* item, Py_ssize_t steps) except *
        void move_to_index[T](PyObject* item, T index) except *

        # relative operations
        cppclass RelativeProxy:
            PyObject* get() except NULL
            void insert(PyObject* value) except *
            void add(PyObject* value) except *
            void extend(PyObject* items, bint reverse) except *
            void update(PyObject* items, bint reverse) except *
            void remove() except *
            void discard() except*
            PyObject* pop() except NULL
            void clear(Py_ssize_t length) except *
            void move(PyObject* value) except *


cdef class LinkedSet:
    cdef:
        Slot[CyLinkedSet] variant  # stack-allocated
        # NOTE: uncommenting this line causes a compiler warning about memory alignment
        # object __weakref__  # allows LinkedSet to be weak-referenced from Python

    @staticmethod
    cdef LinkedSet from_view(CyLinkedSet* view)
