"""Cython headers for pdcast/util/structs/list/sort.pyx"""
from cpython.ref cimport PyObject
from libc.stdlib cimport malloc, free

from .base cimport (
    DEBUG, ListNode, SingleNode, DoubleNode, HashNode, DictNode, Pair, raise_exception,
    Py_DECREF, Py_LT, PyObject_RichCompareBool, PyObject_CallFunctionObjArgs
)


#######################
####    STRUCTS    ####
#######################


cdef packed struct KeyedSingleNode:
    SingleNode* node
    PyObject* key
    KeyedSingleNode* next


cdef packed struct KeyedDoubleNode:
    DoubleNode* node
    PyObject* key
    KeyedDoubleNode* next
    KeyedDoubleNode* prev


cdef packed struct KeyedHashNode:
    HashNode* node
    PyObject* key
    KeyedHashNode* next
    KeyedHashNode* prev


cdef packed struct KeyedDictNode:
    DictNode* node
    PyObject* key
    KeyedDictNode* next
    KeyedDictNode* prev


ctypedef fused SortNode:
    SingleNode
    DoubleNode
    HashNode
    DictNode
    KeyedSingleNode
    KeyedDoubleNode
    KeyedHashNode
    KeyedDictNode


ctypedef fused KeyedNode:
    KeyedSingleNode
    KeyedDoubleNode
    KeyedHashNode
    KeyedDictNode


ctypedef fused HasPrev:
    DoubleNode
    HashNode
    DictNode
    KeyedDoubleNode
    KeyedHashNode
    KeyedDictNode


#######################
####    CLASSES    ####
#######################


cdef class SortError(Exception):
    """Exception raised when an error occurs during a sort operation."""
    cdef:
        Exception original  # original exception that was raised
        void* head          # to recover the list
        void* tail          # to recover the list


######################
####    PUBLIC    ####
######################


cdef Pair* merge_sort(SortNode* head, SortNode* tail, size_t size, bint reverse = *)
cdef (KeyedSingleNode*, KeyedSingleNode*) decorate_single(
    SingleNode* head,
    SingleNode* tail,
    PyObject* key,
)
cdef (KeyedDoubleNode*, KeyedDoubleNode*) decorate_double(
    DoubleNode* head,
    DoubleNode* tail,
    PyObject* key,
)
cdef (KeyedHashNode*, KeyedHashNode*) decorate_hash(
    HashNode* head,
    HashNode* tail,
    PyObject* key,
)
cdef (KeyedDictNode*, KeyedDictNode*) decorate_dict(
    DictNode* head,
    DictNode* tail,
    PyObject* key,
)
cdef (SingleNode*, SingleNode*) undecorate_single(KeyedSingleNode* head)
cdef (DoubleNode*, DoubleNode*) undecorate_double(KeyedDoubleNode* head)
cdef (HashNode*, HashNode*) undecorate_hash(KeyedHashNode* head)
cdef (DictNode*, DictNode*) undecorate_dict(KeyedDictNode* head)
