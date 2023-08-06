"""Cython headers for pdcast/util/structs/list/index.h"""
from cpython.ref cimport PyObject

from .view cimport MAX_SIZE_T, ListView, SetView, DictView

cdef extern from "index.h":
    # singly-linked
    size_t index_single[NodeType](
        ListView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index_single[NodeType](
        SetView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index_single[NodeType](
        DictView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T

    # doubly-linked
    size_t index_double[NodeType](
        ListView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index_double[NodeType](
        SetView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index_double[NodeType](
        DictView[NodeType]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
