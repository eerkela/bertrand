"""Cython headers for pdcast/util/structs/list/append.h"""
from cpython cimport PyObject

from .node cimport ListView, SetView, DictView

cdef extern from "append.h":
    void append[NodeType](ListView[NodeType]* view, PyObject* item) except +*
    void append[NodeType](SetView[NodeType]* view, PyObject* item) except +*
    void append[NodeType](DictView[NodeType]* view, PyObject* item) except +*
    void append[NodeType](DictView[NodeType]* view, PyObject* item, PyObject* mapped) except +*
    void appendleft[NodeType](ListView[NodeType]* view, PyObject* item) except +*
    void appendleft[NodeType](SetView[NodeType]* view, PyObject* item) except +*
    void appendleft[NodeType](DictView[NodeType]* view, PyObject* item) except +*
    void appendleft[NodeType](DictView[NodeType]* view, PyObject* item, PyObject* mapped) except +*
