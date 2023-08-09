"""Cython headers for pdcast/util/structs/list/rotate.h (`SinglyLinked` namespace)"""
from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "rotate.h":
    void rotate(ListView[SingleNode]* view, ssize_t steps)
    void rotate(SetView[SingleNode]* view, ssize_t steps)
    void rotate(DictView[SingleNode]* view, ssize_t steps)
    void rotate(ListView[DoubleNode]* view, ssize_t steps)
    void rotate(SetView[DoubleNode]* view, ssize_t steps)
    void rotate(DictView[DoubleNode]* view, ssize_t steps)
