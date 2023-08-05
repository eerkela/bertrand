"""Cython headers for pdcast/util/structs/list/insert.h (`SinglyLinked` namespace)."""
from cpython.ref cimport PyObject

from .node cimport ListView, SetView, DictView

cdef extern from "insert.h":
    # singly-linked
    void insert_single[NodeType](
        ListView[NodeType]* view,
        size_t index,
        PyObject* item
    ) except *
    void insert_single[NodeType](
        SetView[NodeType]* view,
        size_t index,
        PyObject* item
    ) except *
    void insert_single[NodeType](
        DictView[NodeType]* view,
        size_t index,
        PyObject* item
    ) except *

    # doubly-linked
    void insert_double[NodeType](
        ListView[NodeType]* view,
        size_t index,
        PyObject* item
    ) except *
    void insert_double[NodeType](
        SetView[NodeType]* view,
        size_t index,
        PyObject* item
    ) except *
    void insert_double[NodeType](
        DictView[NodeType]* view,
        size_t index,
        PyObject* item
    ) except *
