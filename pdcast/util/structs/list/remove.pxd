"""Cython headers for pdcast/util/structs/list/remove.h (`SinglyLinked` namespace)."""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "remove.h":
    # shared
    void remove(ListView[SingleNode]* view, PyObject* item) except *
    void remove(ListView[DoubleNode]* view, PyObject* item) except *
    void removeafter(SetView[SingleNode]* view, PyObject* sentinel) except *
    void removeafter(DictView[SingleNode]* view, PyObject* sentinel) except *
    void removeafter(SetView[DoubleNode]* view, PyObject* sentinel) except *
    void removeafter(DictView[DoubleNode]* view, PyObject* sentinel) except *

    # singly-linked
    void remove_single(SetView[SingleNode]* view, PyObject* item) except *
    void remove_single(DictView[SingleNode]* view, PyObject* item) except *
    void remove_single(SetView[DoubleNode]* view, PyObject* item) except *
    void remove_single(DictView[DoubleNode]* view, PyObject* item) except *
    void removebefore_single(SetView[SingleNode]* view, PyObject* sentinel) except *
    void removebefore_single(DictView[SingleNode]* view, PyObject* sentinel) except *
    void removebefore_single(SetView[DoubleNode]* view, PyObject* sentinel) except *
    void removebefore_single(DictView[DoubleNode]* view, PyObject* sentinel) except *
    
    # doubly-linked
    void remove_double(SetView[DoubleNode]* view, PyObject* item) except *
    void remove_double(DictView[DoubleNode]* view, PyObject* item) except *
    void removebefore_double(SetView[DoubleNode]* view, PyObject* sentinel) except *
    void removebefore_double(DictView[DoubleNode]* view, PyObject* sentinel) except *
