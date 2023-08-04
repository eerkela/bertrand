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

    cdef cppclass Hashed[T]:
        PyObject* value
        Py_hash_t hash
        Hashed[T]* next
        Hashed[T]* prev  # not all Hashed nodes have this

    cdef cppclass Mapped[T]:
        PyObject* value
        PyObject* mapped
        Py_hash_t hash
        Mapped[T]* next
        Mapped[T]* prev  # not all Mapped nodes have this
