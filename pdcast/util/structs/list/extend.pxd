"""Cython headers for pdcast/util/structs/list/extend.h"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "extend.h":
    # extend()
    void extend(ListView[SingleNode]* view, PyObject* items) except +*
    void extend(SetView[SingleNode]* view, PyObject* items) except +*
    void extend(DictView[SingleNode]* view, PyObject* items) except +*
    void extend(ListView[DoubleNode]* view, PyObject* items) except +*
    void extend(SetView[DoubleNode]* view, PyObject* items) except +*
    void extend(DictView[DoubleNode]* view, PyObject* items) except +*

    # extendleft()
    void extendleft(ListView[SingleNode]* view, PyObject* items) except +*
    void extendleft(SetView[SingleNode]* view, PyObject* items) except +*
    void extendleft(DictView[SingleNode]* view, PyObject* items) except +*
    void extendleft(ListView[DoubleNode]* view, PyObject* items) except +*
    void extendleft(SetView[DoubleNode]* view, PyObject* items) except +*
    void extendleft(DictView[DoubleNode]* view, PyObject* items) except +*

    # extendafter()
    void extendafter(
        SetView[SingleNode]* view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
    void extendafter(
        DictView[SingleNode]* view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
    void extendafter(
        SetView[DoubleNode]* view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
    void extendafter(
        DictView[DoubleNode]* view,
        PyObject* sentinel,
        PyObject* items
    ) except +*

    # extendbefore()
    void extendbefore(
        SetView[SingleNode]* view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
    void extendbefore(
        DictView[SingleNode]* view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
    void extendbefore(
        SetView[DoubleNode]* view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
    void extendbefore(
        DictView[DoubleNode]* view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
