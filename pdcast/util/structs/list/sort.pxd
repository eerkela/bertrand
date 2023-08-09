"""Cython headers for pdcast/util/structs/list/sort.h"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "sort.h":
    void sort(ListView[SingleNode]* view, PyObject* key, bint reverse) except *
    void sort(SetView[SingleNode]* view, PyObject* key, bint reverse) except *
    void sort(DictView[SingleNode]* view, PyObject* key, bint reverse) except *
    void sort(ListView[DoubleNode]* view, PyObject* key, bint reverse) except *
    void sort(SetView[DoubleNode]* view, PyObject* key, bint reverse) except *
    void sort(DictView[DoubleNode]* view, PyObject* key, bint reverse) except *
