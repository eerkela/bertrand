"""Cython headers for pdcast/util/structs/list/insert.h (`SinglyLinked` namespace)."""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "insert.h":
    void insert(ListView[SingleNode]* view, size_t index, PyObject* item) except *
    void insert(SetView[SingleNode]* view, size_t index, PyObject* item) except *
    void insert(DictView[SingleNode]* view, size_t index, PyObject* item) except *
    void insert(ListView[DoubleNode]* view, size_t index, PyObject* item) except *
    void insert(SetView[DoubleNode]* view, size_t index, PyObject* item) except *
    void insert(DictView[DoubleNode]* view, size_t index, PyObject* item) except *
