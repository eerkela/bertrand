"""Cython headers for pdcast/util/structs/list/double.pyx"""
from cpython.ref cimport PyObject

from .node cimport SingleNode, DoubleNode
from .view cimport ListView
from .append cimport append, appendleft
from .contains cimport contains
from .count cimport count
from .delete_slice cimport (
    delete_index_single, delete_index_double, delete_slice_single, delete_slice_double
)
from .extend cimport extend, extendleft
from .get_slice cimport (
    get_index_single, get_index_double, get_slice_single, get_slice_double
)
from .index cimport index, normalize_index
from .insert cimport insert
from .pop cimport pop, popleft, popright
from .remove cimport remove
from .reverse cimport reverse
from .rotate cimport rotate
from .set_slice cimport (
    set_index_single, set_index_double, set_slice_single, set_slice_double
)
from .sort cimport sort


cdef extern from "Python.h":
    void Py_INCREF(PyObject* obj)
    void Py_DECREF(PyObject* obj)



ctypedef fused ListNode:
    SingleNode
    DoubleNode


cdef class LinkedList:
    pass


# cdef class SinglyLinkedList(LinkedList):
#     cdef:
#         ListView[SingleNode]* view


cdef class DoublyLinkedList(LinkedList):
    cdef:
        ListView[DoubleNode]* view
