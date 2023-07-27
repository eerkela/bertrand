"""Cython headers for pdcast/util/structs/list/double.pyx"""
from cpython.ref cimport PyObject
from libc.stdlib cimport malloc, free

from .base cimport (
    DEBUG, LinkedList, DoubleNode, Pair, normalize_index, get_slice_direction,
    node_at_index, raise_exception, Py_INCREF, Py_DECREF, PyErr_Occurred, Py_EQ,
    PyObject_RichCompareBool, PyObject_GetIter, PyIter_Next
)
from .sort cimport (
    KeyedDoubleNode, SortError, merge_sort, decorate_double, undecorate_double
)

#######################
####    CLASSES    ####
#######################


cdef class DoublyLinkedList(LinkedList):
    cdef:
        DoubleNode* head
        DoubleNode* tail

    cdef DoubleNode* _allocate_node(self, PyObject* value)
    cdef void _free_node(self, DoubleNode* node)
    cdef void _link_node(self, DoubleNode* prev, DoubleNode* curr, DoubleNode* next)
    cdef void _unlink_node(self, DoubleNode* curr)
    cdef (DoubleNode*, DoubleNode*, size_t) _stage_nodes(
        self, PyObject* items, bint reverse
    )
