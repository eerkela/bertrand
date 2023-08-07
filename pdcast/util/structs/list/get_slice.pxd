"""Cython headers for pdcast/util/structs/list/get_slice.h"""
from cpython.ref cimport PyObject

from .view cimport ListView, SetView, DictView

cdef extern from "get_slice.h":
    # singly-linked
    ListView[T]* get_slice_single[T](
        ListView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except NULL
    SetView[T]* get_slice_single[T](
        SetView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except NULL
    DictView[T]* get_slice_single[T](
        DictView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except NULL

    # doubly-linked
    ListView[T]* get_slice_double[T](
        ListView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except NULL
    SetView[T]* get_slice_double[T](
        SetView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except NULL
    DictView[T]* get_slice_double[T](
        DictView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step
    ) except NULL
