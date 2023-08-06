"""Cython headers for pdcast/util/structs/list/sort.h"""
from cpython.ref cimport PyObject

from .node cimport ListView, SetView, DictView

cdef extern from "slice.h":
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
