"""Cython headers for pdcast/util/structs/list/hashed.pyx"""
from cpython.ref cimport PyObject
from libc.stdlib cimport malloc, calloc, free

from .base cimport (
    DEBUG, LinkedList, HashNode, Pair, allocate_hash_node, free_node, normalize_index,
    get_slice_direction, node_at_index, raise_exception, Py_INCREF, Py_DECREF,
    PyErr_Occurred, Py_EQ, PyObject_Hash, PyObject_RichCompareBool, PyObject_GetIter,
    PyIter_Next
)
from .sort cimport (
    KeyedHashNode, SortError, merge_sort, decorate_hash, undecorate_hash
)

cdef extern from "table.h":
    cdef cppclass ListTable[T]:
        ListTable() except +
        int remember(T* node) except -1
        int forget(T* node) except -1
        void clear() except +
        T* search(PyObject* value) except? NULL
        T* search_node(T* node) except? NULL
        void resize(unsigned char new_exponent) except +
        void clear_tombstones() except +
        size_t nbytes()


#######################
####    CLASSES    ####
#######################


cdef class HashedList(LinkedList):
    cdef:
        ListTable[HashNode]* table
        HashNode* head
        HashNode* tail

    cdef void _insertafter(self, PyObject* sentinel, PyObject* item)
    cdef void _insertbefore(self, PyObject* sentinel, PyObject* item)
    cdef void _extendafter(self, PyObject* sentinel, PyObject* other)
    cdef void _extendbefore(self, PyObject* sentinel, PyObject* other)
    cdef void _moveleft(self, PyObject* item, size_t steps = *)
    cdef void _moveright(self, PyObject* item, size_t steps = *)
    cdef void _moveafter(self, PyObject* sentinel, PyObject* item)
    cdef void _movebefore(self, PyObject* sentinel, PyObject* item)
    cdef void _move(self, PyObject* item, long index)
    cdef void _link_node(self, HashNode* prev, HashNode* curr, HashNode* next)
    cdef void _unlink_node(self, HashNode* curr)
    cdef (HashNode*, HashNode*, size_t) _stage_nodes(
        self, PyObject* items, bint reverse, set override = *
    )
