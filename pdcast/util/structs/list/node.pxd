"""Cython headers for pdcast/util/structs/list/node.h"""
from cpython.ref cimport PyObject

cdef extern from "node.h":
    # NOTE: since the objects in this module deal with direct memory allocation
    # and reference counting, only a subset of the node.h API is exposed here.
    # these are mostly for diagnostics from the Python side, and for templated
    # type declarations.

    struct SingleNode:
        PyObject* value
        SingleNode* next

    struct DoubleNode:
        PyObject* value
        DoubleNode* next
        DoubleNode* prev

    cdef cppclass BaseAllocator:
        size_t allocated()
        size_t nbytes()

    cdef cppclass DirectAllocator(BaseAllocator):
        pass

    cdef cppclass FreeListAllocator(BaseAllocator):
        size_t reserved()

    cdef cppclass PreAllocator(BaseAllocator):
        size_t reserved()
