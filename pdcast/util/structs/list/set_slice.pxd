"""Cython headers for pdcast/util/structs/list/set_slice.h"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "set_slice.h":
    # set_index()
    void set_index(ListView[SingleNode]* view, size_t index, PyObject* item) except *
    void set_index(SetView[SingleNode]* view, size_t index, PyObject* item) except *
    void set_index(DictView[SingleNode]* view, size_t index, PyObject* item) except *
    void set_index(ListView[DoubleNode]* view, size_t index, PyObject* item) except *
    void set_index(SetView[DoubleNode]* view, size_t index, PyObject* item) except *
    void set_index(DictView[DoubleNode]* view, size_t index, PyObject* item) except *

    # set_slice()
    void set_slice(
        ListView[SingleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) except *
    void set_slice(
        SetView[SingleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) except *
    void set_slice(
        DictView[SingleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) except *
    void set_slice(
        ListView[DoubleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) except *
    void set_slice(
        SetView[DoubleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) except *
    void set_slice(
        DictView[DoubleNode]* view,
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) except *
