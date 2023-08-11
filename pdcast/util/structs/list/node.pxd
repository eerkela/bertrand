"""Cython headers for pdcast/util/structs/list/node.h"""
from cpython.ref cimport PyObject

cdef extern from "node.h":
    struct SingleNode:
        PyObject* value
        SingleNode* next
        SingleNode* prev

    struct DoubleNode:
        PyObject* value
        DoubleNode* next
        DoubleNode* prev
