"""Cython headers for pdcast/util/structs/list/mergesort.pyx"""
from cpython.ref cimport PyObject

from .base cimport SingleNode, DoubleNode, HashNode, DictNode

cdef extern from "Python.h":
    void Py_INCREF(PyObject* obj)
    void Py_DECREF(PyObject* obj)
    PyObject* PyErr_Occurred()
    void PyErr_Clear()
    int Py_EQ, Py_LT
    int PyObject_RichCompareBool(PyObject* obj1, PyObject* obj2, int opid)
    PyObject* PyObject_CallFunctionObjArgs(PyObject* callable, ...)
    PyObject* PyObject_GetIter(PyObject* obj)
    PyObject* PyIter_Next(PyObject* obj)


#######################
####    STRUCTS    ####
#######################


cdef packed struct Pair:
    void* first
    void* second


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


ctypedef fused KeyedNode:
    KeyedSingleNode
    KeyedDoubleNode
    KeyedHashNode
    KeyedDictNode


ctypedef fused SortNode:
    SingleNode
    DoubleNode
    HashNode
    DictNode
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


######################
####    PUBLIC    ####
######################


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
cdef (SingleNode*, SingleNode*) undecorate_single(self, KeyedSingleNode* head)
cdef (DoubleNode*, DoubleNode*) undecorate_double(self, KeyedDoubleNode* head)
cdef (HashNode*, HashNode*) undecorate_hash(self, KeyedHashNode* head)
cdef (DictNode*, DictNode*) undecorate_dict(self, KeyedDictNode* head)
