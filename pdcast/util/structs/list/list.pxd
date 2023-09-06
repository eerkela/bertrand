"""Cython headers for pdcast/util/structs/list/double.pyx"""
from cpython.ref cimport PyObject
from libcpp.memory cimport unique_ptr, make_unique
from libcpp.optional cimport optional, nullopt, make_optional
from libcpp.stack cimport stack

from .base cimport MAX_SIZE_T, SingleNode, DoubleNode, Py_INCREF, Py_DECREF


cdef extern from "list.h":
    cdef cppclass VariantList:
        # nested types/classes
        cppclass Lock:
            cppclass Guard:
                pass
            Guard operator()()
            Guard* context()
            bint diagnostics(optional[bint] enabled = nullopt)
            size_t count()
            size_t duration()
            double contention()
            void reset_diagnostics()

        cppclass Slice:
            VariantList* get() except NULL
            void set(PyObject* items) except *
            void delete "del" () except *  # del() shadows Cython `delete` keyword

        cppclass SliceFactory:
            cppclass Indices:
                long long start, stop, step
                size_t abs_step, first, last, length
                bint empty, consistent, backward
            Slice operator()(PyObject* py_slice)
            Slice operator()(
                optional[long long] start = nullopt,
                optional[long long] stop = nullopt,
                optional[long long] step = nullopt
            )
            Indices* normalize(PyObject* py_slice) except NULL
            Indices* normalize(
                optional[long long] start = nullopt,
                optional[long long] stop = nullopt,
                optional[long long] step = nullopt
            ) except NULL

        # functors
        const Lock lock  # lock() functor
        const SliceFactory slice  # slice() functor

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
        int contains(PyObject* item) except *
        size_t size()

        # extra methods
        PyObject* specialization()
        void specialize(PyObject* spec) except *
        size_t nbytes()
        bint doubly_linked()
        SingleNode* get_head_single() except +
        SingleNode* get_tail_single() except +
        DoubleNode* get_head_double() except +
        DoubleNode* get_tail_double() except +


cdef class LinkedList:
    cdef VariantList* view

    @staticmethod
    cdef LinkedList from_view(VariantList* view)


cdef class ThreadGuard:
    cdef LinkedList parent
    cdef VariantList.Lock.Guard* context  # context manager threading lock
