"""Cython headers for pdcast/util/structs/list/rotate.h (`SinglyLinked` namespace)"""
from .node cimport SingleNode, DoubleNode
from .view cimport ListView, SetView, DictView

cdef extern from "rotate.h":
    # singly-linked
    void rotate_single(ListView[SingleNode]* view, ssize_t steps)
    void rotate_single(SetView[SingleNode]* view, ssize_t steps)
    void rotate_single(DictView[SingleNode]* view, ssize_t steps)
    void rotate_single(ListView[DoubleNode]* view, ssize_t steps)
    void rotate_single(SetView[DoubleNode]* view, ssize_t steps)
    void rotate_single(DictView[DoubleNode]* view, ssize_t steps)

    # doubly-linked
    void rotate_double(ListView[DoubleNode]* view, ssize_t steps)
    void rotate_double(SetView[DoubleNode]* view, ssize_t steps)
    void rotate_double(DictView[DoubleNode]* view, ssize_t steps)
