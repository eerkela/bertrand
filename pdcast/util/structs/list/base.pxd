"""Cython headers for pdcast/util/structs/list/base.pyx"""
from cpython.ref cimport PyObject

cdef extern from "Python.h":
    void Py_INCREF(PyObject* obj)
    void Py_DECREF(PyObject* obj)
    PyObject* PyErr_Occurred()
    int Py_EQ, Py_LT
    int PyObject_RichCompareBool(PyObject* obj1, PyObject* obj2, int opid)
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


ctypedef fused IsUnique:
    HashNode
    DictNode


#######################
####    CLASSES    ####
#######################


cdef class LinkedList:
    cdef:
        size_t size

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
    cdef size_t _normalize_index(self, long index)


#########################
####    FUNCTIONS    ####
#########################


cdef void raise_exception() except *

