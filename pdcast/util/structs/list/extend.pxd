"""Cython headers for pdcast/util/structs/list/extend.h"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "extend.h":
    # shared
    void extend(ListView[SingleNode]* view, PyObject* items) except +*
    void extend(SetView[SingleNode]* view, PyObject* items) except +*
    void extend(DictView[SingleNode]* view, PyObject* items) except +*
    void extend(ListView[DoubleNode]* view, PyObject* items) except +*
    void extend(SetView[DoubleNode]* view, PyObject* items) except +*
    void extend(DictView[DoubleNode]* view, PyObject* items) except +*
    void extendleft(ListView[SingleNode]* view, PyObject* items) except +*
    void extendleft(SetView[SingleNode]* view, PyObject* items) except +*
    void extendleft(DictView[SingleNode]* view, PyObject* items) except +*
    void extendleft(ListView[DoubleNode]* view, PyObject* items) except +*
    void extendleft(SetView[DoubleNode]* view, PyObject* items) except +*
    void extendleft(DictView[DoubleNode]* view, PyObject* items) except +*
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

    # singly-linked
    void extendbefore_single(
        SetView[SingleNode]* view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
    void extendbefore_single(
        DictView[SingleNode]* view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
    void extendbefore_single(
        SetView[DoubleNode]* view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
    void extendbefore_single(
        DictView[DoubleNode]* view,
        PyObject* sentinel,
        PyObject* items
    ) except +*

    # doubly-linked
    void extendbefore_double(
        SetView[DoubleNode]* view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
    void extendbefore_double(
        DictView[DoubleNode]* view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
