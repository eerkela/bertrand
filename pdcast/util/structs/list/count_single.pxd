"""Cython headers for pdcast/util/structs/list/count.h (`SinglyLinked` namespace)"""
from cpython.ref cimport PyObject

from .node cimport ListView, SetView, DictView
from .view cimport MAX_SIZE_T

cdef extern from "count.h" namespace "SinglyLinked":
    size_t count[NodeType](
        ListView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count[NodeType](
        SetView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count[NodeType](
        DictView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
