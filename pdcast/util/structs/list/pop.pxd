"""Cython headers for pdcast/util/structs/list/pop.h (`SinglyLinked` namespace)."""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "pop.h":
    PyObject* pop(ListView[SingleNode]* view, size_t index) except NULL
    PyObject* pop(SetView[SingleNode]* view, size_t index) except NULL
    PyObject* pop(DictView[SingleNode]* view, size_t index) except NULL
    PyObject* pop(
        DictView[SingleNode]* view,
        PyObject* key,
        PyObject* default_value
    )
    PyObject* pop(ListView[DoubleNode]* view, size_t index) except NULL
    PyObject* pop(SetView[DoubleNode]* view, size_t index) except NULL
    PyObject* pop(DictView[DoubleNode]* view, size_t index) except NULL
    PyObject* pop(
        DictView[DoubleNode]* view,
        PyObject* key,
        PyObject* default_value
    )
