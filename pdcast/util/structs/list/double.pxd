"""Cython headers for pdcast/util/structs/list/double.pyx"""
from cpython.ref cimport PyObject

from .base cimport (
    normalize_index, get_slice_direction, node_at_index, raise_exception, Py_INCREF,
    Py_DECREF, PyErr_Occurred, Py_EQ, PyObject_RichCompareBool, PyObject_GetIter,
    PyIter_Next
)
from .node cimport DoubleNode, ListView
from .append cimport append, appendleft, extend, extendleft
from .sort cimport sort


cdef class DoublyLinkedList(LinkedList):
    cdef:
        ListView[DoubleNode]* view
