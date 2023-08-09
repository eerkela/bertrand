"""Cython headers for pdcast/util/structs/list/index.h"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "index.h":
    # shared
    const size_t MAX_SIZE_T
    size_t normalize_index(
        PyObject* index,
        size_t size,
        bint truncate
    ) except? MAX_SIZE_T

    # singly-linked
    size_t index(
        ListView[SingleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index(
        SetView[SingleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index(
        DictView[SingleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T

    # doubly-linked
    size_t index(
        ListView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index(
        SetView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index(
        DictView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
