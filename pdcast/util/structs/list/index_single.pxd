"""Cython headers for pdcast/util/structs/list/index.h (`SinglyLinked` namespace)."""
from cpython.ref cimport PyObject

from .node cimport ListView, SetView, DictView
from .view cimport MAX_SIZE_T

cdef extern from "index.h" namespace "SinglyLinked":
    size_t index[NodeType](
        ListView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index[NodeType](
        SetView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index[NodeType](
        DictView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
