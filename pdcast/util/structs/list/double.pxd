"""Cython headers for pdcast/util/structs/list/double.pyx"""
from cpython.ref cimport PyObject

from .base cimport LinkedList


######################
####   STRUCTS    ####
######################


cdef packed struct ListNode:
    PyObject* value   # reference to underlying Python object
    ListNode* next    # reference to the next node in the list
    ListNode* prev    # reference to the previous node in the list


cdef packed struct KeyNode:
    ListNode* node    # decorated ListNode
    PyObject* key     # computed key to use during sort()
    KeyNode* next     # reference to the next node in the list
    KeyNode* prev     # reference to the previous node in the list


#######################
####    CLASSES    ####
#######################


cdef class DoublyLinkedList(LinkedList):
    cdef:
        ListNode* head
        ListNode* tail

    cdef void _sort_decorated(self, PyObject* key, bint reverse)
    cdef ListNode* _allocate_node(self, PyObject* value)
    cdef void _free_node(self, ListNode* node)
    cdef void _link_node(self, ListNode* prev, ListNode* curr, ListNode* next)
    cdef void _unlink_node(self, ListNode* curr)
    cdef (ListNode*, ListNode*, size_t) _stage_nodes(
        self, PyObject* items, bint reverse
    )
    cdef ListNode* _node_at_index(self, size_t index)
    cdef (size_t, size_t) _get_slice_direction(
        self,
        size_t start,
        size_t stop,
        ssize_t step,
    )
    cdef (KeyNode*, KeyNode*) _decorate(self, PyObject* key)
    cdef (ListNode*, ListNode*) _undecorate(self, KeyNode* head)
    cdef ListNode* _split(self, ListNode* curr, size_t length)
    cdef KeyNode* _split_decorated(self, KeyNode* curr, size_t length)
    cdef (ListNode*, ListNode*) _merge(
        self,
        ListNode* left,
        ListNode* right,
        ListNode* temp,
        bint reverse,
    )
    cdef (KeyNode*, KeyNode*) _merge_decorated(
        self,
        KeyNode* left,
        KeyNode* right,
        KeyNode* temp,
        bint reverse,
    )
    cdef void _recover_list(
        self,
        ListNode* head,
        ListNode* tail,
        ListNode* sub_left,
        ListNode* sub_right,
        ListNode* curr,
    )
    cdef size_t _free_decorated(self, KeyNode* head)
