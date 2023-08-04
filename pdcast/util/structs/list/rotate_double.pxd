"""Cython headers for pdcast/util/structs/list/rotate.h (`SinglyLinked` namespace)"""
from .view cimport ListView, SetView, DictView

cdef extern from "rotate.h" namespace "DoublyLinked":
    void rotate[NodeType](ListView[NodeType]* view, ssize_t steps)
    void rotate[NodeType](SetView[NodeType]* view, ssize_t steps)
    void rotate[NodeType](DictView[NodeType]* view, ssize_t steps)
