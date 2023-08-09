"""Cython headers for pdcast/util/structs/list/get_slice.h"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "get_slice.h":
    # get_index()
    PyObject* get_index(ListView[SingleNode]* view, size_t index) except NULL
    PyObject* get_index(SetView[SingleNode]* view, size_t index) except NULL
    PyObject* get_index(DictView[SingleNode]* view, size_t index) except NULL
    PyObject* get_index(ListView[DoubleNode]* view, size_t index) except NULL
    PyObject* get_index(SetView[DoubleNode]* view, size_t index) except NULL
    PyObject* get_index(DictView[DoubleNode]* view, size_t index) except NULL

    # get_slice()
    ListView[SingleNode]* get_slice(
        ListView[SingleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step
    ) except NULL
    SetView[SingleNode]* get_slice(
        SetView[SingleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step
    ) except NULL
    DictView[SingleNode]* get_slice(
        DictView[SingleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step
    ) except NULL
    ListView[DoubleNode]* get_slice(
        ListView[DoubleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step
    ) except NULL
    SetView[DoubleNode]* get_slice(
        SetView[DoubleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step
    ) except NULL
    DictView[DoubleNode]* get_slice(
        DictView[DoubleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step
    ) except NULL
