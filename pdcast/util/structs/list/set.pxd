"""Cython headers for pdcast/util/structs/list/hashed.pyx"""
from cpython.ref cimport PyObject
from libcpp.stack cimport stack

from .base cimport MAX_SIZE_T, SingleNode, DoubleNode, Py_INCREF, Py_DECREF
from .list cimport LinkedList, VariantList


cdef extern from "set.h":
    cdef cppclass VariantSet(VariantList):
        VariantSet(bint doubly_linked, Py_ssize_t max_size, PyObject* spec) except +
        VariantSet(
            PyObject* iterable,
            bint doubly_linked,
            bint reverse,
            Py_ssize_t max_size,
            PyObject* spec
        ) except +
        void add(PyObject* item, bint left) except *
        void discard(PyObject* item) except *
        int isdisjoint(PyObject* other) except -1
        int issubset(PyObject* other, bint strict) except -1
        int issuperset(PyObject* other, bint strict) except -1
        VariantSet* union_(PyObject* other, bint left) except NULL
        VariantSet* intersection(PyObject* other) except NULL
        VariantSet* difference(PyObject* other) except NULL
        VariantSet* symmetric_difference(PyObject* other) except NULL
        void update(PyObject* items, bint left) except *
        void intersection_update(PyObject* items) except *
        void difference_update(PyObject* items) except *
        void symmetric_difference_update(PyObject* items) except *
        Py_ssize_t distance(PyObject* item1, PyObject* item2) except? MAX_SIZE_T
        void swap(PyObject* item1, PyObject* item2) except *
        void move(PyObject* item, Py_ssize_t steps) except *
        void move_to_index[T](PyObject* item, T index) except *
        void move_relative(
            PyObject* item,
            PyObject* sentinel,
            Py_ssize_t offset
        ) except *
        PyObject* get_relative(PyObject* sentinel, Py_ssize_t offset) except NULL
        void insert_relative(
            PyObject* item,
            PyObject* sentinel,
            Py_ssize_t offset
        ) except *
        void add_relative(
            PyObject* item,
            PyObject* sentinel,
            Py_ssize_t offset
        ) except *
        void extend_relative(
            PyObject* items,
            PyObject* sentinel,
            Py_ssize_t offset,
            bint reverse
        ) except *
        void update_relative(
            PyObject* items,
            PyObject* sentinel,
            Py_ssize_t offset,
            bint reverse
        ) except *
        void remove_relative(PyObject* sentinel, Py_ssize_t offset) except *
        void discard_relative(PyObject* sentinel, Py_ssize_t offset) except *
        PyObject* pop_relative(PyObject* sentinel, Py_ssize_t offset) except NULL
        void clear_relative(
            PyObject* sentinel,
            Py_ssize_t offset,
            Py_ssize_t length
        ) except *


cdef class LinkedSet(LinkedList):
    @staticmethod
    cdef LinkedSet from_view(VariantSet* view)
