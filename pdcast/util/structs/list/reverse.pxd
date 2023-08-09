"""Cython headers for pdcast/util/structs/list/reverse.h"""
from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "reverse.h":
    void reverse(ListView[SingleNode]* view)
    void reverse(SetView[SingleNode]* view)
    void reverse(DictView[SingleNode]* view)
    void reverse(ListView[DoubleNode]* view)
    void reverse(SetView[DoubleNode]* view)
    void reverse(DictView[DoubleNode]* view)
