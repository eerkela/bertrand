"""Cython headers for pdcast/util/structs/list/append.h"""
from cpython cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport DynamicListView, DynamicSetView, DynamicDictView

cdef extern from "append.h":
    void append(DynamicListView[SingleNode]* view, PyObject* item, bint left) except *
    void append(DynamicSetView[SingleNode]* view, PyObject* item, bint left) except *
    void append(DynamicDictView[SingleNode]* view, PyObject* item, bint left) except *
    void append(
        DynamicDictView[SingleNode]* view,
        PyObject* item,
        PyObject* mapped,
        bint left
    ) except *
    void append(DynamicListView[DoubleNode]* view, PyObject* item, bint left) except *
    void append(DynamicSetView[DoubleNode]* view, PyObject* item, bint left) except *
    void append(DynamicDictView[DoubleNode]* view, PyObject* item, bint left) except *
    void append(
        DynamicDictView[DoubleNode]* view,
        PyObject* item,
        PyObject* mapped,
        bint left
    ) except *
