"""Cython headers for pdcast/util/structs/list/delete_slice.h"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "delete_slice.h":
    # delete_index()
    void delete_index_single(ListView[SingleNode]* view, size_t index) except *
    void delete_index_single(SetView[SingleNode]* view, size_t index) except *
    void delete_index_single(DictView[SingleNode]* view, size_t index) except *
    void delete_index_single(ListView[DoubleNode]* view, size_t index) except *
    void delete_index_single(SetView[DoubleNode]* view, size_t index) except *
    void delete_index_single(DictView[DoubleNode]* view, size_t index) except *
    void delete_index_double(ListView[DoubleNode]* view, size_t index) except *
    void delete_index_double(SetView[DoubleNode]* view, size_t index) except *
    void delete_index_double(DictView[DoubleNode]* view, size_t index) except *

    # delete_slice()
    void delete_slice_single(
        ListView[SingleNode]* view,
        size_t start,
        size_t stop,
        size_t step
    ) except *
    void delete_slice_single(
        SetView[SingleNode]* view,
        size_t start,
        size_t stop,
        size_t step
    ) except *
    void delete_slice_single(
        DictView[SingleNode]* view,
        size_t start,
        size_t stop,
        size_t step
    ) except *
    void delete_slice_single(
        ListView[DoubleNode]* view,
        size_t start,
        size_t stop,
        size_t step
    ) except *
    void delete_slice_single(
        SetView[DoubleNode]* view,
        size_t start,
        size_t stop,
        size_t step
    ) except *
    void delete_slice_single(
        DictView[DoubleNode]* view,
        size_t start,
        size_t stop,
        size_t step
    ) except *
    void delete_slice_double(
        ListView[DoubleNode]* view,
        size_t start,
        size_t stop,
        size_t step
    ) except *
    void delete_slice_double(
        SetView[DoubleNode]* view,
        size_t start,
        size_t stop,
        size_t step
    ) except *
    void delete_slice_double(
        DictView[DoubleNode]* view,
        size_t start,
        size_t stop,
        size_t step
    ) except *
