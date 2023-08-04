"""Cython headers for pdcast/util/structs/list/remove.h (`DoublyLinked` namespace),"""
from cpython.ref cimport PyObject

from .node cimport ListView, SetView, DictView

cdef extern from "remove.h":
    void remove[NodeType](ListView[NodeType]* view, PyObject* item) except *
    void removeafter[NodeType](SetView[NodeType]* view, PyObject* sentinel) except *
    void removeafter[NodeType](DictView[NodeType]* view, PyObject* sentinel) except *

cdef extern from "remove.h" namespace "DoublyLinked":
    void remove[NodeType](SetView[NodeType]* view, PyObject* item) except *
    void remove[NodeType](DictView[NodeType]* view, PyObject* item) except *
    void removebefore[NodeType](SetView[NodeType]* view, PyObject* sentinel) except *
    void removebefore[NodeType](DictView[NodeType]* view, PyObject* sentinel) except *
