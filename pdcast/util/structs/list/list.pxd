"""Cython headers for pdcast/util/structs/list/double.pyx"""
from cpython.ref cimport PyObject
from libcpp.memory cimport unique_ptr, make_unique
from libcpp.optional cimport optional, nullopt, make_optional
from libcpp.stack cimport stack

from .base cimport MAX_SIZE_T, SingleNode, DoubleNode, Py_INCREF, Py_DECREF


cdef extern from "<mutex>" namespace "std":
    cdef cppclass mutex:
        mutex() except +
        void lock() except +
        bint try_lock() except +
        void unlock() except +

    cdef cppclass lock_guard[MutexType]:
        lock_guard(MutexType& mtx) except +


cdef extern from "list.h":
    cdef cppclass VariantList:
        # constructors
        VariantList(bint doubly_linked, Py_ssize_t max_size, PyObject* spec) except +
        VariantList(
            PyObject* iterable,
            bint doubly_linked,
            bint reverse,
            Py_ssize_t max_size,
            PyObject* spec
        ) except +

        # list interface
        void append(PyObject* item, bint left) except *
        void insert[T](T index, PyObject* item) except *
        void extend(PyObject* items, bint left) except *
        size_t index[T](PyObject* item, T start, T stop) except? MAX_SIZE_T
        size_t count[T](PyObject* item, T start, T stop) except? MAX_SIZE_T
        void remove(PyObject*) except *
        PyObject* pop[T](T index) except NULL
        VariantList* copy() except NULL
        void clear()
        void sort(PyObject* key, bint reverse) except *
        void reverse()
        void rotate(ssize_t steps)
        PyObject* get_index[T](T index) except NULL
        void set_index[T](T index, PyObject* item) except *
        void set_slice(
            Py_ssize_t start,
            Py_ssize_t stop,
            Py_ssize_t step,
            PyObject* items
        ) except *
        void delete_index[T](T index) except *
        void delete_slice(
            Py_ssize_t start,
            Py_ssize_t stop,
            Py_ssize_t step
        ) except *
        int contains(PyObject* item) except *
        size_t size()

        cppclass SliceProxy:
            VariantList* extract() except NULL
            void replace(PyObject* items) except *
            void drop() except *

        SliceProxy slice(long long start, long long stop, long long step)

        # extra methods
        PyObject* specialization()
        void specialize(PyObject* spec) except *
        lock_guard[mutex] lock() except +  # RAII-style threading lock
        lock_guard[mutex]* lock_context() except +  # Heap-allocated threading lock
        size_t nbytes()
        bint doubly_linked()
        SingleNode* get_head_single() except +
        SingleNode* get_tail_single() except +
        DoubleNode* get_head_double() except +
        DoubleNode* get_tail_double() except +


cdef class ThreadGuard:
    cdef lock_guard[mutex]* context  # context manager threading lock


cdef class LinkedList:
    cdef VariantList* view

    @staticmethod
    cdef LinkedList from_view(VariantList* view)
