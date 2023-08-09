"""Cython headers for pdcast/util/structs/list/insert.h (`SinglyLinked` namespace)."""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "insert.h":
    # singly-linked
    void insert_single(
        ListView[SingleNode]* view,
        size_t index,
        PyObject* item
    ) except *
    void insert_single(
        SetView[SingleNode]* view,
        size_t index,
        PyObject* item
    ) except *
    void insert_single(
        DictView[SingleNode]* view,
        size_t index,
        PyObject* item
    ) except *
    void insert_single(
        ListView[DoubleNode]* view,
        size_t index,
        PyObject* item
    ) except *
    void insert_single(
        SetView[DoubleNode]* view,
        size_t index,
        PyObject* item
    ) except *
    void insert_single(
        DictView[DoubleNode]* view,
        size_t index,
        PyObject* item
    ) except *

    # doubly-linked
    void insert_double(
        ListView[DoubleNode]* view,
        size_t index,
        PyObject* item
    ) except *
    void insert_double(
        SetView[DoubleNode]* view,
        size_t index,
        PyObject* item
    ) except *
    void insert_double(
        DictView[DoubleNode]* view,
        size_t index,
        PyObject* item
    ) except *
