"""Cython headers for pdcast/util/structs/list/hashed.pyx"""
from cpython.ref cimport PyObject
from libcpp.stack cimport stack

from .base cimport MAX_SIZE_T, SingleNode, DoubleNode, Py_INCREF, Py_DECREF
from .list cimport LinkedList, VariantList


cdef extern from "set.h":
    cdef cppclass VariantSet(VariantList):
        VariantSet(bint doubly_linked, Py_ssize_t max_size) except +
        VariantSet(
            PyObject* iterable,
            bint doubly_linked,
            bint reverse,
            Py_ssize_t max_size,
            PyObject* spec
        ) except +


cdef class LinkedSet(LinkedList):
    @staticmethod
    cdef LinkedSet from_view(VariantSet* view)
