"""Cython headers for pdcast/util/structs/list/reverse.h"""
from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "reverse.h":
    # singly-linked
    void reverse_single(ListView[SingleNode]* view)
    void reverse_single(SetView[SingleNode]* view)
    void reverse_single(DictView[SingleNode]* view)
    void reverse_single(ListView[DoubleNode]* view)
    void reverse_single(SetView[DoubleNode]* view)
    void reverse_single(DictView[DoubleNode]* view)

    # doubly-linked
    void reverse_double(ListView[DoubleNode]* view)
    void reverse_double(SetView[DoubleNode]* view)
    void reverse_double(DictView[DoubleNode]* view)
