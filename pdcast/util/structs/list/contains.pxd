"""Cython headers for pdcast/util/structs/list/contains.h"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "contains.h":
    int contains(ListView[SingleNode]* view, PyObject* item) except -1
    int contains(SetView[SingleNode]* view, PyObject* item) except *
    int contains(DictView[SingleNode]* view, PyObject* item) except *
    int contains(ListView[DoubleNode]* view, PyObject* item) except -1
    int contains(SetView[DoubleNode]* view, PyObject* item) except *
    int contains(DictView[DoubleNode]* view, PyObject* item) except *
