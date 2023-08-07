"""Cython headers for pdcast/util/structs/list/set_slice.h"""
from cpython.ref cimport PyObject

from .view cimport ListView, SetView, DictView

cdef extern from "set_slice.h":
    # singly-linked
    void set_slice_single[T](
        ListView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step,
        PyObject* items
    ) except *
    void set_slice_single[T](
        SetView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step,
        PyObject* items
    ) except *
    void set_slice_single[T](
        DictView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step,
        PyObject* items
    ) except *

    # doubly-linked
    void set_slice_double[T](
        ListView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step,
        PyObject* items
    ) except *
    void set_slice_double[T](
        SetView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step,
        PyObject* items
    ) except *
    void set_slice_double[T](
        DictView[T]* view,
        size_t start,
        size_t stop,
        ssize_t step,
        PyObject* items
    ) except *
