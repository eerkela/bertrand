"""Cython headers for pdcast/util/structs/list/double.pyx"""
from cpython.ref cimport PyObject

from .base cimport (
    normalize_index, get_slice_direction, node_at_index, raise_exception, Py_INCREF,
    Py_DECREF, PyErr_Occurred, Py_EQ, PyObject_RichCompareBool, PyObject_GetIter,
    PyIter_Next
)
from .node cimport DoubleNode
from .view cimport ListView, SetView, DictView
from .append cimport append, appendleft
from .extend_double cimport extend, extendleft
# from .insert_double cimport insert
from .remove_double cimport remove
# from .pop_double cimport pop, popleft, popright
from .count_double cimport count
# from .index_double cimport index
from .contains cimport contains
from .sort cimport sort
# from .move_double cimport move, moveforward, movebackward, movebefore, moveafter, move_to_end
from .rotate_double cimport rotate
from .reverse_double cimport reverse
# from .slice_double cimport get_slice, set_slice, delete_slice


cdef class DoublyLinkedList(LinkedList):
    cdef:
        ListView[DoubleNode]* view


cdef class DoublyLinkedSet(LinkedSet):
    cdef:
        SetView[DoubleNode]* view


cdef class DoublyLinkedDict(LinkedDict):
    cdef:
        DictView[DoubleNode]* view
