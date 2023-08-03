"""Cython headers for pdcast/util/structs/list/index.h"""
from cpython.ref cimport PyObject

from .node cimport ListView, SetView, DictView

cdef extern from "index.h":
    size_t MAX_SIZE_T
    size_t normalize_index(long long index, size_t size, bint truncate) except? MAX_SIZE_T
    int contains[NodeType](ListView[NodeType], PyObject* item) except -1
    int contains[NodeType](SetView[NodeType], PyObject* item) except *
    int contains[NodeType](DictView[NodeType], PyObject* item) except *

