"""Cython headers for pdcast/util/structs/list/rotate.h (`SinglyLinked` namespace)"""
from .view cimport ListView, SetView, DictView

cdef extern from "rotate.h":
    # singly-linked
    void rotate_single[NodeType](ListView[NodeType]* view, ssize_t steps)
    void rotate_single[NodeType](SetView[NodeType]* view, ssize_t steps)
    void rotate_single[NodeType](DictView[NodeType]* view, ssize_t steps)

    # doubly-linked
    void rotate_double[NodeType](ListView[NodeType]* view, ssize_t steps)
    void rotate_double[NodeType](SetView[NodeType]* view, ssize_t steps)
    void rotate_double[NodeType](DictView[NodeType]* view, ssize_t steps)
