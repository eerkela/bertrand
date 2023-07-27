"""Cython headers for pdcast/util/structs/list/hashed.pyx"""
from cpython.ref cimport PyObject
from libc.stdlib cimport malloc, calloc, free

from .base cimport (
    DEBUG, LinkedList, HashNode, Pair, normalize_index, get_slice_direction,
    node_at_index, raise_exception, Py_INCREF, Py_DECREF, PyErr_Occurred, Py_EQ,
    PyObject_Hash, PyObject_RichCompareBool, PyObject_GetIter, PyIter_Next
)
from .sort cimport (
    KeyedHashNode, SortError, merge_sort, decorate_hash, undecorate_hash
)


#########################
####    CONSTANTS    ####
#########################


cdef size_t INITIAL_TABLE_SIZE
cdef float MAX_LOAD_FACTOR
cdef float MAX_TOMBSTONES
cdef size_t[29] PRIMES


#######################
####    STRUCTS    ####
#######################


cdef struct ListTable:
    HashNode** map        # array of HashNode references
    HashNode* tombstone   # sentinel for a value that was removed from the table
    size_t tombstones     # counts the number of tombstones in table
    size_t size           # total number of slots in table
    size_t occupied       # counts number of occupied slots in table (incl.tombstones)
    size_t exponent       # log2(size) - log2(INITIAL_TABLE_SIZE)
    size_t prime          # prime number used for double hashing


#######################
####    CLASSES    ####
#######################


cdef class HashedList(LinkedList):
    cdef:
        ListTable* table
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
    cdef HashNode* _allocate_node(self, PyObject* value)
    cdef void _free_node(self, HashNode* node)
    cdef void _link_node(self, HashNode* prev, HashNode* curr, HashNode* next)
    cdef void _unlink_node(self, HashNode* curr)
    cdef (HashNode*, HashNode*, size_t) _stage_nodes(
        self, PyObject* items, bint reverse, set override = *
    )
    cdef void _remember_node(self, HashNode* node)
    cdef void _forget_node(self, HashNode* node)
    cdef HashNode* _search(self, PyObject* key)
    cdef HashNode* _search_node(self, HashNode* node)
    cdef void _resize_table(self)
    cdef void _clear_tombstones(self)
