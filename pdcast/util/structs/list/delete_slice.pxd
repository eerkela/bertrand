"""Cython headers for pdcast/util/structs/list/delete_slice.h"""
from cpython.ref cimport PyObject

from .view cimport ListView, SetView, DictView

cdef extern from "delete_slice.h":
    # singly-linked
    void delete_slice_single[T](
        ListView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except *
    void delete_slice_single[T](
        SetView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except *
    void delete_slice_single[T](
        DictView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except *

    # doubly-linked
    void delete_slice_double[T](
        ListView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except *
    void delete_slice_double[T](
        SetView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except *
    void delete_slice_double[T](
        DictView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except *
