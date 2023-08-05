"""Cython headers for pdcast/util/structs/list/remove.h (`SinglyLinked` namespace)."""
from cpython.ref cimport PyObject

from .node cimport ListView, SetView, DictView

cdef extern from "remove.h":
    # shared
    void remove[NodeType](ListView[NodeType]* view, PyObject* item) except *
    void removeafter[NodeType](SetView[NodeType]* view, PyObject* sentinel) except *
    void removeafter[NodeType](DictView[NodeType]* view, PyObject* sentinel) except *

    # singly-linked
    void remove_single[NodeType](
        SetView[NodeType]* view,
        PyObject* item
    ) except *
    void remove_single[NodeType](
        DictView[NodeType]* view,
        PyObject* item
    ) except *
    void removebefore_single[NodeType](
        SetView[NodeType]* view,
        PyObject* sentinel
    ) except *
    void removebefore_single[NodeType](
        DictView[NodeType]* view,
        PyObject* sentinel
    ) except *

    # doubly-linked
    void remove_double[NodeType](
        SetView[NodeType]* view,
        PyObject* item
    ) except *
    void remove_double[NodeType](
        DictView[NodeType]* view,
        PyObject* item
    ) except *
    void removebefore_double[NodeType](
        SetView[NodeType]* view,
        PyObject* sentinel
    ) except *
    void removebefore_double[NodeType](
        DictView[NodeType]* view,
        PyObject* sentinel
    ) except *
