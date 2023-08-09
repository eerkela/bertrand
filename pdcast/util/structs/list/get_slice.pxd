"""Cython headers for pdcast/util/structs/list/get_slice.h"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "get_slice.h":
    # get_index()
    PyObject* get_index_single(ListView[SingleNode]* view, size_t index) except NULL
    PyObject* get_index_single(SetView[SingleNode]* view, size_t index) except NULL
    PyObject* get_index_single(DictView[SingleNode]* view, size_t index) except NULL
    PyObject* get_index_double(ListView[DoubleNode]* view, size_t index) except NULL
    PyObject* get_index_double(SetView[DoubleNode]* view, size_t index) except NULL
    PyObject* get_index_double(DictView[DoubleNode]* view, size_t index) except NULL

    # get_slice()
    ListView[SingleNode]* get_slice_single(
        ListView[SingleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step
    ) except NULL
    SetView[SingleNode]* get_slice_single(
        SetView[SingleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step
    ) except NULL
    DictView[SingleNode]* get_slice_single(
        DictView[SingleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step
    ) except NULL
    ListView[DoubleNode]* get_slice_double(
        ListView[DoubleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step
    ) except NULL
    SetView[DoubleNode]* get_slice_double(
        SetView[DoubleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step
    ) except NULL
    DictView[DoubleNode]* get_slice_double(
        DictView[DoubleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step
    ) except NULL
