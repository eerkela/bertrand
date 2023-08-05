"""Cython headers for pdcast/util/structs/list/insert.h (`SinglyLinked` namespace)."""
from cpython.ref cimport PyObject

from .node cimport ListView, SetView, DictView

cdef extern from "insert.h" namespace "SinglyLinked":
    void insert[NodeType](
        ListView[NodeType]* view,
        size_t index,
        PyObject* item
    ) except *
    void insert[NodeType](
        SetView[NodeType]* view,
        size_t index,
        PyObject* item
    ) except *
    void insert[NodeType](
        DictView[NodeType]* view,
        size_t index,
        PyObject* item
    ) except *
