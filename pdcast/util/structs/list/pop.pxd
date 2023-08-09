"""Cython headers for pdcast/util/structs/list/pop.h (`SinglyLinked` namespace)."""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "pop.h":
    # shared
    PyObject* popleft(ListView[SingleNode]* view) except NULL
    PyObject* popleft(SetView[SingleNode]* view) except NULL
    PyObject* popleft(DictView[SingleNode]* view) except NULL
    PyObject* popleft(ListView[DoubleNode]* view) except NULL
    PyObject* popleft(SetView[DoubleNode]* view) except NULL
    PyObject* popleft(DictView[DoubleNode]* view) except NULL

    # singly-linked
    PyObject* pop_single(ListView[SingleNode]* view, size_t index) except NULL
    PyObject* pop_single(SetView[SingleNode]* view, size_t index) except NULL
    PyObject* pop_single(DictView[SingleNode]* view, size_t index) except NULL
    PyObject* pop_single(
        DictView[SingleNode]* view,
        PyObject* key,
        PyObject* default_value
    )
    PyObject* pop_single(ListView[DoubleNode]* view, size_t index) except NULL
    PyObject* pop_single(SetView[DoubleNode]* view, size_t index) except NULL
    PyObject* pop_single(DictView[DoubleNode]* view, size_t index) except NULL
    PyObject* pop_single(
        DictView[DoubleNode]* view,
        PyObject* key,
        PyObject* default_value
    )
    PyObject* popright_single(ListView[SingleNode]* view) except NULL
    PyObject* popright_single(SetView[SingleNode]* view) except NULL
    PyObject* popright_single(DictView[SingleNode]* view) except NULL
    PyObject* popright_single(ListView[DoubleNode]* view) except NULL
    PyObject* popright_single(SetView[DoubleNode]* view) except NULL
    PyObject* popright_single(DictView[DoubleNode]* view) except NULL

    # doubly-linked
    PyObject* pop_double(ListView[DoubleNode]* view, size_t index) except NULL
    PyObject* pop_double(SetView[DoubleNode]* view, size_t index) except NULL
    PyObject* pop_double(DictView[DoubleNode]* view, size_t index) except NULL
    PyObject* pop_double(
        DictView[DoubleNode]* view,
        PyObject* key,
        PyObject* default_value
    )
    PyObject* popright_double(ListView[DoubleNode]* view) except NULL
    PyObject* popright_double(SetView[DoubleNode]* view) except NULL
    PyObject* popright_double(DictView[DoubleNode]* view) except NULL
