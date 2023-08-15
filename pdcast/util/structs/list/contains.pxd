"""Cython headers for pdcast/util/structs/list/contains.h"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport DynamicListView, DynamicSetView, DynamicDictView

cdef extern from "contains.h":
    int contains(DynamicListView[SingleNode]* view, PyObject* item) except -1
    int contains(DynamicSetView[SingleNode]* view, PyObject* item) except *
    int contains(DynamicDictView[SingleNode]* view, PyObject* item) except *
    int contains(DynamicListView[DoubleNode]* view, PyObject* item) except -1
    int contains(DynamicSetView[DoubleNode]* view, PyObject* item) except *
    int contains(DynamicDictView[DoubleNode]* view, PyObject* item) except *
