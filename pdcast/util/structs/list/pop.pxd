"""Cython headers for pdcast/util/structs/list/pop.h (`SinglyLinked` namespace)."""
from cpython.ref cimport PyObject

from .view cimport ListView, SetView, DictView

cdef extern from "pop.h":
    # shared
    PyObject* popleft[NodeType](ListView[NodeType]* view) except NULL
    PyObject* popleft[NodeType](SetView[NodeType]* view) except NULL
    PyObject* popleft[NodeType](DictView[NodeType]* view) except NULL

    # singly-linked
    PyObject* pop_single[NodeType](ListView[NodeType]* view, size_t index) except NULL
    PyObject* pop_single[NodeType](SetView[NodeType]* view, size_t index) except NULL
    PyObject* pop_single[NodeType](DictView[NodeType]* view, size_t index) except NULL
    PyObject* pop_single[NodeType](
        DictView[NodeType]* view,
        PyObject* key,
        PyObject* default_value
    )
    PyObject* popright_single[NodeType](ListView[NodeType]* view) except NULL
    PyObject* popright_single[NodeType](SetView[NodeType]* view) except NULL
    PyObject* popright_single[NodeType](DictView[NodeType]* view) except NULL

    # doubly-linked
    PyObject* pop_double[NodeType](ListView[NodeType]* view, size_t index) except NULL
    PyObject* pop_double[NodeType](SetView[NodeType]* view, size_t index) except NULL
    PyObject* pop_double[NodeType](DictView[NodeType]* view, size_t index) except NULL
    PyObject* pop_double[NodeType](
        DictView[NodeType]* view,
        PyObject* key,
        PyObject* default_value
    )
    PyObject* popright_double[NodeType](ListView[NodeType]* view) except NULL
    PyObject* popright_double[NodeType](SetView[NodeType]* view) except NULL
    PyObject* popright_double[NodeType](DictView[NodeType]* view) except NULL
