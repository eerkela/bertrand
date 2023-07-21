"""Cython headers for pdcast/util/structs/list.pyx"""
from cpython.ref cimport PyObject

from .base cimport ListNode, ListTable


cdef NodeWrapper node_from_struct(ListNode* c_struct)


#######################
####    CLASSES    ####
#######################


cdef class LinkedList:
    cdef:
        ListNode* _head
        ListNode* _tail
        long long size

    cdef LinkedList copy(self)
    cdef void append(self, object item)
    cdef void appendleft(self, object item)
    cdef void insert(self, object item, long long index)
    cdef void extend(self, object items)
    cdef void extendleft(self, object items)
    cdef long long count(self, object item)
    cdef long long index(
        self,
        object item,
        long long start = *,
        long long stop = *
    )
    cdef void sort(self)
    cdef void rotate(self, long long steps = *)
    cdef void reverse(self)
    cdef void remove(self, object item)
    cdef void clear(self)
    cdef object pop(self, long long index = *)
    cdef object popleft(self)
    cdef object popright(self)
    cdef void _add_struct(self, ListNode* prev, ListNode* curr, ListNode* next)
    cdef void _remove_struct(self, ListNode* curr)
    cdef ListNode* _struct_at_index(self, long long index)
    cdef long long _normalize_index(self, long long index)
    cdef (long long, long long) _get_slice_direction(
        self,
        long long start,
        long long stop,
        long long step,
    )
    cdef ListNode* _split(self, ListNode* head, long long length)
    cdef (ListNode*, ListNode*) _merge(
        self,
        ListNode* left,
        ListNode* right,
        ListNode* temp
    )


cdef class HashedList(LinkedList):
    cdef:
        ListTable* table
