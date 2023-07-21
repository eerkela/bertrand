"""Cython headers for pdcast/util/structs/list/double.pyx"""
from cpython.ref cimport PyObject

from .base cimport LinkedList


######################
####   STRUCTS    ####
######################


# traditional doubly-linked list node with manual reference counting
cdef packed struct ListNode:
    PyObject* value   # reference to underlying Python object
    Node* next  # reference to the next node in the list
    Node* prev  # reference to the previous node in the list


#######################
####    CLASSES    ####
#######################


cdef class DoublyLinkedList(LinkedList):
    cdef:
        ListNode* head
        ListNode* tail

    cdef ListNode* _allocate_node(PyObject* value)
    cdef void _free_node(self, ListNode* node)
    cdef void _link_node(self, ListNode* prev, ListNode* curr, ListNode* next)
    cdef void _unlink_node(self, ListNode* curr)
    cdef ListNode* _node_at_index(self, size_t index)
    cdef (size_t, size_t) _get_slice_direction(
            self,
            size_t start,
            size_t stop,
            ssize_t step,
        )
    cdef ListNode* _split(self, ListNode* head, size_t length)
    cdef (ListNode*, ListNode*) _merge(
        self,
        ListNode* left,
        ListNode* right,
        ListNode* temp
    )
