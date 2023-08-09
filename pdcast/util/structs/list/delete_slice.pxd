"""Cython headers for pdcast/util/structs/list/delete_slice.h"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "delete_slice.h":
    # delete_index()
    void delete_index(ListView[SingleNode]* view, size_t index) except *
    void delete_index(SetView[SingleNode]* view, size_t index) except *
    void delete_index(DictView[SingleNode]* view, size_t index) except *
    void delete_index(ListView[DoubleNode]* view, size_t index) except *
    void delete_index(SetView[DoubleNode]* view, size_t index) except *
    void delete_index(DictView[DoubleNode]* view, size_t index) except *

    # delete_slice()
    void delete_slice(
        ListView[SingleNode]* view,
        size_t start,
        size_t stop,
        size_t step
    ) except *
    void delete_slice(
        SetView[SingleNode]* view,
        size_t start,
        size_t stop,
        size_t step
    ) except *
    void delete_slice(
        DictView[SingleNode]* view,
        size_t start,
        size_t stop,
        size_t step
    ) except *
    void delete_slice(
        ListView[DoubleNode]* view,
        size_t start,
        size_t stop,
        size_t step
    ) except *
    void delete_slice(
        SetView[DoubleNode]* view,
        size_t start,
        size_t stop,
        size_t step
    ) except *
    void delete_slice(
        DictView[DoubleNode]* view,
        size_t start,
        size_t stop,
        size_t step
    ) except *
