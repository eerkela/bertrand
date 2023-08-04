"""Cython headers for pdcast/util/structs/list/contains.h"""
from cpython.ref cimport PyObject*

from .node cimport ListView, SetView, DictView

cdef extern from "contains.h":
    int contains[NodeType](ListView[NodeType]* view, PyObject* item) except -1
    int contains[NodeType](SetView[NodeType], PyObject* item) except *
    int contains[NodeType](DictView[NodeType], PyObject* item) except *
