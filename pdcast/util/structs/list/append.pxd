"""Cython headers for pdcast/util/structs/list/append.h"""
from cpython cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "append.h":
    # append()
    void append(ListView[SingleNode]* view, PyObject* item) except *
    void append(SetView[SingleNode]* view, PyObject* item) except *
    void append(DictView[SingleNode]* view, PyObject* item) except *
    void append(DictView[SingleNode]* view, PyObject* item, PyObject* mapped) except *
    void append(ListView[DoubleNode]* view, PyObject* item) except *
    void append(SetView[DoubleNode]* view, PyObject* item) except *
    void append(DictView[DoubleNode]* view, PyObject* item) except *
    void append(DictView[DoubleNode]* view, PyObject* item, PyObject* mapped) except *

    # appendleft()
    void appendleft(ListView[SingleNode]* view, PyObject* item) except *
    void appendleft(SetView[SingleNode]* view, PyObject* item) except *
    void appendleft(DictView[SingleNode]* view, PyObject* item) except *
    void appendleft(
        DictView[SingleNode]* view,
        PyObject* item,
        PyObject* mapped
    ) except *
    void appendleft(ListView[DoubleNode]* view, PyObject* item) except *
    void appendleft(SetView[DoubleNode]* view, PyObject* item) except *
    void appendleft(DictView[DoubleNode]* view, PyObject* item) except *
    void appendleft(
        DictView[DoubleNode]* view,
        PyObject* item,
        PyObject* mapped
    ) except *
