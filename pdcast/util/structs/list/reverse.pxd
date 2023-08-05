"""Cython headers for pdcast/util/structs/list/reverse.h"""
from .view cimport ListView, SetView, DictView

cdef extern from "reverse.h":
    # singly-linked
    void reverse_single[NodeType](ListView[NodeType]* view)
    void reverse_single[NodeType](SetView[NodeType]* view)
    void reverse_single[NodeType](DictView[NodeType]* view)

    # doubly-linked
    void reverse_double[NodeType](ListView[NodeType]* view)
    void reverse_double[NodeType](SetView[NodeType]* view)
    void reverse_double[NodeType](DictView[NodeType]* view)
