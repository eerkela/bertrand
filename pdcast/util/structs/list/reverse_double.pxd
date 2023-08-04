"""Cython headers for pdcast/util/structs/list/reverse.h"""
from .view cimport ListView, SetView, DictView

cdef extern from "reverse.h" namespace "DoublyLinked":
    void reverse[NodeType](ListView[NodeType]* view)
    void reverse[NodeType](SetView[NodeType]* view)
    void reverse[NodeType](DictView[NodeType]* view)
