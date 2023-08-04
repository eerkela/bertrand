"""Cython headers for pdcast/util/structs/list/extend.h (`DoublyLinked` namespace)"""
from cpython.ref cimport PyObject

from .node cimport ListView, SetView, DictView

cdef extern from "extend.h":
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

cdef extern from "extend.h" namespace "DoublyLinked":
    void extendbefore[NodeType](
        SetView[NodeType*] view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
    void extendbefore[NodeType](
        DictView[NodeType*] view,
        PyObject* sentinel,
        PyObject* items
    ) except +*
