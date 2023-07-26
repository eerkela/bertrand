"""Cython headers for pdcast/util/structs/list/hashed.pyx"""
from cpython.ref cimport PyObject

from .base cimport LinkedList


#######################
####    STRUCTS    ####
#######################


# specialized Node that stores the hash of the associated object
cdef packed struct HashNode:
    PyObject* value
    HashNode* next
    HashNode* prev
    Py_hash_t hash    # cached value of `PyObject_Hash(value)`


# hash map that looks up structs by their underlying value
cdef struct ListTable:
    HashNode** lookup     # array of HashNode references
    HashNode* tombstone   # sentinel for a value that was removed from the table
    size_t tombstone_count  # counts the number of tombstones in table
    size_t size      # total number of slots in table
    size_t occupied  # counts number of occupied slots in table (incl.tombstones)
    size_t exponent  # log2(size) - log2(INITIAL_TABLE_SIZE)
    size_t prime     # prime number used for double hashing


#######################
####    CLASSES    ####
#######################


cdef class HashedList(LinkedList):
    cdef:
        ListTable table
        HashNode* head
        HashNode* tail

    cdef HashNode* _allocate_node(self, PyObject* value)
    cdef void _free_node(self, HashNode* node)
    cdef void _link_node(self, HashNode* prev, HashNode* curr, HashNode* next)
    cdef void _unlink_node(self, HashNode* curr)
    cdef HashNode* _node_at_index(self, size_t index)
    cdef (size_t, size_t) _get_slice_direction(
            self,
            size_t start,
            size_t stop,
            ssize_t step,
        )
    cdef HashNode* _split(self, HashNode* head, size_t length)
    cdef (HashNode*, HashNode*) _merge(
        self,
        HashNode* left,
        HashNode* right,
        HashNode* temp
    )
    cdef void _resize_table(self)
    cdef void _clear_tombstones(self)
    cdef HashNode* _search(self, PyObject* key)
