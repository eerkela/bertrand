"""Cython headers for pdcast/util/structs/list/double.pyx"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView, normalize_index
from .append cimport append, appendleft
from .contains cimport contains
from .count cimport count_single, count_double
from .extend cimport extend, extendleft
from .index cimport index_single, index_double
from .insert cimport insert_single, insert_double
# from .move_double cimport move, moveforward, movebackward, movebefore, moveafter, move_to_end
from .pop cimport popleft, pop_single, popright_single, pop_double, popright_double
from .remove cimport remove
from .reverse cimport reverse_single, reverse_double
from .rotate cimport rotate_single, rotate_double
# from .slice_double cimport get_slice, set_slice, delete_slice
from .sort cimport sort


cdef extern from "Python.h":
    void Py_INCREF(PyObject* obj)
    void Py_DECREF(PyObject* obj)


cdef class LinkedList:
    pass


cdef class SinglyLinkedList(LinkedList):
    cdef:
        ListView[SingleNode]* view


cdef class DoublyLinkedList(LinkedList):  # inherit from SinglyLinkedList if possible
    cdef:
        ListView[DoubleNode]* view
