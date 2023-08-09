"""Cython headers for pdcast/util/structs/list/count.h"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView
from .index cimport MAX_SIZE_T

cdef extern from "count.h":
    size_t count(
        ListView[SingleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count(
        SetView[SingleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count(
        DictView[SingleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count(
        ListView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count(
        SetView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count(
        DictView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
