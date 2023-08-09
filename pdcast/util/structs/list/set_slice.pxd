"""Cython headers for pdcast/util/structs/list/set_slice.h"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "set_slice.h":
    # set_index()
    void set_index_single(ListView[SingleNode]* view, size_t index, PyObject* item) except *
    void set_index_single(SetView[SingleNode]* view, size_t index, PyObject* item) except *
    void set_index_single(DictView[SingleNode]* view, size_t index, PyObject* item) except *
    void set_index_single(ListView[DoubleNode]* view, size_t index, PyObject* item) except *
    void set_index_single(SetView[DoubleNode]* view, size_t index, PyObject* item) except *
    void set_index_single(DictView[DoubleNode]* view, size_t index, PyObject* item) except *
    void set_index_double(ListView[DoubleNode]* view, size_t index, PyObject* item) except *
    void set_index_double(SetView[DoubleNode]* view, size_t index, PyObject* item) except *
    void set_index_double(DictView[DoubleNode]* view, size_t index, PyObject* item) except *

    # set_slice()
    void set_slice_single(
        ListView[SingleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) except *
    void set_slice_single(
        SetView[SingleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) except *
    void set_slice_single(
        DictView[SingleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) except *
    void set_slice_single(
        ListView[DoubleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) except *
    void set_slice_single(
        SetView[DoubleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) except *
    void set_slice_single(
        DictView[DoubleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) except *
    void set_slice_double(
        ListView[DoubleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) except *
    void set_slice_double(
        SetView[DoubleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) except *
    void set_slice_double(
        DictView[DoubleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) except *
