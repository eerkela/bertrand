"""Cython headers for pdcast/util/structs/list/double.pyx"""
from cpython.ref cimport PyObject

from libcpp.stack cimport stack

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, normalize_index
from .append cimport append, appendleft
from .contains cimport contains
from .count cimport count
from .delete_slice cimport delete_index, delete_slice
from .extend cimport extend, extendleft
from .get_slice cimport get_index, get_slice
from .index cimport index
from .insert cimport insert
from .pop cimport pop, popleft, popright
from .remove cimport remove
from .reverse cimport reverse
from .rotate cimport rotate
from .set_slice cimport set_index, set_slice
from .sort cimport sort

cdef extern from "Python.h":
    void Py_INCREF(PyObject* obj)
    void Py_DECREF(PyObject* obj)


cdef class LinkedList:
    pass


cdef class SinglyLinkedList(LinkedList):
    cdef:
        ListView[SingleNode]* view


cdef class DoublyLinkedList(LinkedList):
    cdef:
        ListView[DoubleNode]* view
