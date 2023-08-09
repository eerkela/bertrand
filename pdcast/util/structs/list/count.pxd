"""Cython headers for pdcast/util/structs/list/count.h"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport MAX_SIZE_T, ListView, SetView, DictView

cdef extern from "count.h":
    # singly-linked
    size_t count_single(
        ListView[SingleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count_single(
        SetView[SingleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count_single(
        DictView[SingleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count_single(
        ListView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count_single(
        SetView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count_single(
        DictView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T

    # doubly-linked
    size_t count_double(
        ListView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count_double(
        SetView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
    size_t count_double(
        DictView[DoubleNode]* view,
        PyObject* item,
        size_t start,
        size_t stop
    ) except? MAX_SIZE_T
