"""Cython headers for pdcast/util/structs/list/double.pyx"""
from cpython.ref cimport PyObject

from libcpp.stack cimport stack
from libcpp.utility cimport pair

from .view cimport MAX_SIZE_T, normalize_index, normalize_bounds

cdef extern from "Python.h":
    void Py_INCREF(PyObject* obj)
    void Py_DECREF(PyObject* obj)

cdef extern from "list.h":
    cdef cppclass VariantList:
        VariantList(bint doubly_linked, ssize_t max_size) except +
        VariantList(
            PyObject* iterable,
            bint doubly_linked,
            bint reverse,
            PyObject* spec,
            ssize_t max_size
        ) except +
        void append(PyObject* item, bint left) except *
        void insert(size_t index, PyObject* item) except *
        void extend(PyObject* items, bint left) except *
        size_t index(PyObject* item, size_t start, size_t stop) except? MAX_SIZE_T
        size_t count(PyObject* item, size_t start, size_t stop) except? MAX_SIZE_T
        void remove(PyObject*) except *
        PyObject* pop(size_t index) except NULL
        VariantList* copy() except *
        void clear()
        void sort(PyObject* key, bint reverse) except *
        void reverse()
        void rotate(ssize_t steps)
        PyObject* get_specialization()
        void specialize(PyObject* spec) except *
        size_t nbytes()
        size_t size()
        PyObject* get_index(size_t index) except NULL
        VariantList* get_slice(
            Py_ssize_t start,
            Py_ssize_t stop,
            Py_ssize_t step
        ) except *
        void set_index(size_t index, PyObject* item) except *
        void set_slice(
            Py_ssize_t start,
            Py_ssize_t stop,
            Py_ssize_t step,
            PyObject* items
        ) except *
        void delete_index(size_t index) except *
        void delete_slice(
            Py_ssize_t start,
            Py_ssize_t stop,
            Py_ssize_t step
        ) except *
        int contains(PyObject* item) except *


cdef class LinkedList:
    cdef: 
        VariantList* view

    @staticmethod
    cdef LinkedList from_view(VariantList* view)