"""Cython headers for pdcast/util/structs/list/append.h"""
from cpython cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "append.h":
    void append(ListView[SingleNode]* view, PyObject* item, bint left) except *
    void append(SetView[SingleNode]* view, PyObject* item, bint left) except *
    void append(DictView[SingleNode]* view, PyObject* item, bint left) except *
    void append(
        DictView[SingleNode]* view,
        PyObject* item,
        PyObject* mapped,
        bint left
    ) except *
    void append(ListView[DoubleNode]* view, PyObject* item, bint left) except *
    void append(SetView[DoubleNode]* view, PyObject* item, bint left) except *
    void append(DictView[DoubleNode]* view, PyObject* item, bint left) except *
    void append(
        DictView[DoubleNode]* view,
        PyObject* item,
        PyObject* mapped,
        bint left
    ) except *
