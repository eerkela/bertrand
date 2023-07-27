"""Cython headers for pdcast/util/structs/list/table.pyx"""
from cpython.ref cimport PyObject

from .base cimport HashNode, DictNode


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


cdef struct HashTable:
    HashNode** map          # array of HashNode references
    HashNode* tombstone
    size_t size
    size_t occupied
    size_t tombstones
    size_t prime            # prime number used for double hashing
    unsigned_char exponent  # log2(size) - log2(INITIAL_TABLE_SIZE)


cdef struct DictTable:
    DictNode** map          # DictNodes contain mapped values
    DictNode* tombstone
    size_t size
    size_t occupied
    size_t tombstones
    size_t prime
    unsigned_char exponent



ctypedef fused ListTable:
    HashTable
    DictTable


######################
####    PUBLIC    ####
######################


# allocate_hash_table(unsigned char exponent, HashTable previous = NULL)
# allocate_dict_table(unsigned char exponent, DictTable previous = NULL)
# search(ListTable table, PyObject* key)
# search_node(ListTable table, Unique node)
# remember_node(ListTable table, Unique node)
# forget_node(ListTable table, Unique node)
# clear_tombstones(ListTable table)
