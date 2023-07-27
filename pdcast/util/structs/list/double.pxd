"""Cython headers for pdcast/util/structs/list/double.pyx"""
from cpython.ref cimport PyObject

from .base cimport LinkedList, DoubleNode


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
    cdef DoubleNode* _node_at_index(self, size_t index)
    cdef (size_t, size_t) _get_slice_direction(
        self,
        size_t start,
        size_t stop,
        ssize_t step,
    )
