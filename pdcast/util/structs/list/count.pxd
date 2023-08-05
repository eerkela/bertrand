"""Cython headers for pdcast/util/structs/list/count.h"""
from cpython.ref cimport PyObject

from .node cimport ListView, SetView, DictView
from .view cimport MAX_SIZE_T

cdef extern from "count.h":
    # singly-linked
    size_t count_single[NodeType](
        ListView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count_single[NodeType](
        SetView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count_single[NodeType](
        DictView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T

    # doubly-linked
    size_t count_double[NodeType](
        ListView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count_double[NodeType](
        SetView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count_double[NodeType](
        DictView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T