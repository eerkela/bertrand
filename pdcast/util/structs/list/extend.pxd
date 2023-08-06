"""Cython headers for pdcast/util/structs/list/extend.h"""
from cpython.ref cimport PyObject

from .view cimport ListView, SetView, DictView

cdef extern from "extend.h":
    # shared
    void extend[NodeType](ListView[NodeType]* view, PyObject* items) except +*
    void extend[NodeType](SetView[NodeType]* view, PyObject* items) except +*
    void extend[NodeType](DictView[NodeType]* view, PyObject* items) except +*
    void extendleft[NodeType](ListView[NodeType]* view, PyObject* items) except +*
    void extendleft[NodeType](SetView[NodeType]* view, PyObject* items) except +*
    void extendleft[NodeType](DictView[NodeType]* view, PyObject* items) except +*
    void extendafter[NodeType](
        SetView[NodeType*] view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
    void extendafter[NodeType](
        DictView[NodeType*] view,
        PyObject* sentinel,
        PyObject* items
    ) except +*

    # singly-linked
    void extendbefore_single[NodeType](
        SetView[NodeType*] view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
    void extendbefore_single[NodeType](
        DictView[NodeType*] view,
        PyObject* sentinel,
        PyObject* items
    ) except +*

    # doubly-linked
    void extendbefore_double[NodeType](
        SetView[NodeType*] view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
    void extendbefore_double[NodeType](
        DictView[NodeType*] view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
