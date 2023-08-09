"""Cython headers for pdcast/util/structs/list/index.h"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport MAX_SIZE_T, ListView, SetView, DictView

cdef extern from "index.h":
    # singly-linked
    size_t index_single(
        ListView[SingleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index_single(
        SetView[SingleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index_single(
        DictView[SingleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index_single(
        ListView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index_single(
        SetView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index_single(
        DictView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T

    # doubly-linked
    size_t index_double(
        ListView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index_double(
        SetView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t index_double(
        DictView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
