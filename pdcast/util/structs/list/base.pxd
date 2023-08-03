"""Cython headers for pdcast/util/structs/list/base.pyx"""
from cpython.ref cimport PyObject

from libcpp.utility cimport pair

from .node cimport *
from .sort cimport *

cdef extern from "Python.h":
    void Py_INCREF(PyObject* obj)
    void Py_DECREF(PyObject* obj)
    PyObject* PyErr_Occurred()
    int Py_EQ, Py_LT
    int PyObject_RichCompareBool(PyObject* obj1, PyObject* obj2, int opid)
    Py_hash_t PyObject_Hash(PyObject* obj)
    PyObject* PyObject_CallFunctionObjArgs(PyObject* callable, ...)
    PyObject* PyObject_GetIter(PyObject* obj)
    PyObject* PyIter_Next(PyObject* obj)


#########################
####    CONSTANTS    ####
#########################


cdef bint DEBUG


#######################
####    STRUCTS    ####
#######################


cdef packed struct Pair:
    void* first
    void* second


cdef packed struct ListView:
    void* head
    void* tail
    size_t size


cdef packed struct SingleNode:
    PyObject* value
    SingleNode* next


cdef packed struct DoubleNode:
    PyObject* value
    DoubleNode* next
    DoubleNode* prev


cdef packed struct HashNode:
    PyObject* value
    Py_hash_t hash
    HashNode* next
    HashNode* prev


cdef packed struct DictNode:
    PyObject* value
    PyObject* mapped
    Py_hash_t hash
    DictNode* next
    DictNode* prev


ctypedef fused ListNode:
    SingleNode
    DoubleNode
    HashNode
    DictNode


ctypedef fused HasPrev:
    DoubleNode
    HashNode
    DictNode


ctypedef fused Unique:
    HashNode
    DictNode


#########################
####    FUNCTIONS    ####
#########################


cdef SingleNode* allocate_single_node(PyObject* value)
cdef DoubleNode* allocate_double_node(PyObject* value)
cdef HashNode* allocate_hash_node(PyObject* value)
cdef DictNode* allocate_dict_node(PyObject* value, PyObject* mapped)
cdef void free_node(ListNode* node)
cdef size_t normalize_index(long index, size_t size)
cdef (size_t, size_t) get_slice_direction(
    size_t start,
    size_t stop,
    ssize_t step,
    ListNode* head,
    ListNode* tail,
    size_t size,
)
cdef ListNode* node_at_index(
    size_t index, ListNode* head, ListNode* tail, size_t size
)
cdef void raise_exception() except *


#######################
####    CLASSES    ####
#######################


cdef class LinkedList:
    cdef void _append(self, PyObject* item)
    cdef void _appendleft(self, PyObject* item)
    cdef void _insert(self, PyObject* item, long index)
    cdef void _extend(self, PyObject* items)
    cdef void _extendleft(self, PyObject* items)
    cdef size_t _index(self, PyObject* item, long start = *, long stop = *)
    cdef size_t _count(self, PyObject* item)
    cdef void _remove(self, PyObject* item)
    cdef PyObject* _pop(self, long index = *)
    cdef PyObject* _popleft(self)
    cdef PyObject* _popright(self)
    cdef void _clear(self)
    cdef void _sort(self, PyObject* key = *, bint reverse = *)
    cdef void _reverse(self)
    cdef size_t _nbytes(self)
    cdef LinkedList _copy(self)
    cdef void _rotate(self, ssize_t steps = *)
