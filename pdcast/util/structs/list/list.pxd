"""Cython headers for pdcast/util/structs/list/double.pyx"""
from cpython.ref cimport PyObject
from libcpp.stack cimport stack

from .base cimport MAX_SIZE_T, SingleNode, DoubleNode, Py_INCREF, Py_DECREF


cdef extern from "list.h":
    cdef cppclass VariantList:
        VariantList(bint doubly_linked, ssize_t max_size) except +
        VariantList(
            PyObject* iterable,
            bint doubly_linked,
            bint reverse,
            ssize_t max_size,
            PyObject* spec
        ) except +
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
        PyObject* get_specialization()
        void specialize(PyObject* spec) except *
        size_t nbytes()
        size_t size()
        PyObject* get_index[T](T index) except NULL
        VariantList* get_slice(
            Py_ssize_t start,
            Py_ssize_t stop,
            Py_ssize_t step
        ) except *
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
        bint doubly_linked()
        SingleNode* get_head_single() except +
        DoubleNode* get_head_double() except +
        DoubleNode* get_tail_double() except +


cdef class LinkedList:
    cdef: 
        VariantList* view

    @staticmethod
    cdef LinkedList from_view(VariantList* view)
