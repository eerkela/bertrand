"""Cython headers for pdcast/util/structs/list/remove.h (`SinglyLinked` namespace)."""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "remove.h":
    # remove()
    void remove(ListView[SingleNode]* view, PyObject* item) except *
    void remove(SetView[SingleNode]* view, PyObject* item) except *
    void remove(DictView[SingleNode]* view, PyObject* item) except *
    void remove(ListView[DoubleNode]* view, PyObject* item) except *
    void remove(SetView[DoubleNode]* view, PyObject* item) except *
    void remove(DictView[DoubleNode]* view, PyObject* item) except *

    # discard()
    void discard(SetView[SingleNode]* view, PyObject* item) except *
    void discard(DictView[SingleNode]* view, PyObject* item) except *
    void discard(SetView[DoubleNode]* view, PyObject* item) except *
    void discard(DictView[DoubleNode]* view, PyObject* item) except *

    # discardafter()
    void discardafter(SetView[SingleNode]* view, PyObject* sentinel) except *
    void discardafter(DictView[SingleNode]* view, PyObject* sentinel) except *
    void discardafter(SetView[DoubleNode]* view, PyObject* sentinel) except *
    void discardafter(DictView[DoubleNode]* view, PyObject* sentinel) except *

    # discardbefore()
    void discardbefore(SetView[SingleNode]* view, PyObject* sentinel) except *
    void discardbefore(DictView[SingleNode]* view, PyObject* sentinel) except *
    void discardbefore(SetView[DoubleNode]* view, PyObject* sentinel) except *
    void discardbefore(DictView[DoubleNode]* view, PyObject* sentinel) except *
