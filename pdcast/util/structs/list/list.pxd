"""Cython headers for pdcast/util/structs/list/double.pyx"""
from cpython.ref cimport PyObject
from libcpp.optional cimport optional, nullopt
from libcpp.stack cimport stack

from .base cimport MAX_SIZE_T, SingleNode, DoubleNode, Py_INCREF, Py_DECREF


cdef extern from "list_cython.h":
    cdef cppclass VariantList:
        # nested types/classes
        cppclass Lock:
            cppclass Guard:
                pass
            Guard operator()()
            Guard* context()
            size_t count()
            size_t duration()
            double contention()
            void reset_diagnostics()

        cppclass Index:
            PyObject* get() except +*
            void set(PyObject* item) except +*
            void delete "del" () except +*  # del() shadows Cython `delete` keyword

        cppclass Slice:
            VariantList* get() except +*
            void set(PyObject* items) except +*
            void delete "del" () except +*  # del() shadows Cython `delete` keyword

        # constructors
        VariantList(bint doubly_linked, Py_ssize_t max_size, PyObject* spec) except +
        VariantList(
            PyObject* iterable,
            bint doubly_linked,
            bint reverse,
            Py_ssize_t max_size,
            PyObject* spec
        ) except +

        # functors
        const Lock lock  # lock() functor

        # list interface
        void append(PyObject* item, bint left) except +*
        void insert[T](T index, PyObject* item) except +*
        void extend(PyObject* items, bint left) except +*
        size_t index[T](PyObject* item, T start, T stop) except +*
        size_t count[T](PyObject* item, T start, T stop) except +*
        int contains(PyObject* item) except +*
        void remove(PyObject*) except *
        PyObject* pop[T](T index) except NULL
        VariantList* copy() except NULL
        void clear()
        void sort(PyObject* key, bint reverse) except +*
        void reverse()
        void rotate(ssize_t steps)
        size_t size()
        Index operator[](PyObject* index)
        Slice slice(PyObject* py_slice)

        # extra methods
        PyObject* specialization()
        void specialize(PyObject* spec) except +*
        size_t nbytes()
        PyObject* iter() except +*
        PyObject* riter() except +*


cdef class LinkedList:
    cdef VariantList* view

    @staticmethod
    cdef LinkedList from_view(VariantList* view)


cdef class ThreadGuard:
    cdef LinkedList parent
    cdef VariantList.Lock.Guard* context  # context manager threading lock
