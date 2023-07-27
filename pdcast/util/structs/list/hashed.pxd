"""Cython headers for pdcast/util/structs/list/hashed.pyx"""
from cpython.ref cimport PyObject

from .base cimport LinkedList


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


cdef packed struct HashNode:
    PyObject* value   # reference to underlying Python object
    HashNode* next    # reference to the next node in the list
    HashNode* prev    # reference to the previous node in the list
    Py_hash_t hash    # cached value of `PyObject_Hash(value)`


cdef packed struct KeyNode:
    HashNode* node    # decorated HashNode
    PyObject* key     # computed key to use during sort()
    KeyNode* next     # reference to the next node in the list
    KeyNode* prev     # reference to the previous node in the list


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
    cdef void _sort_decorated(self, PyObject* key, bint reverse)
    cdef HashNode* _allocate_node(self, PyObject* value)
    cdef void _free_node(self, HashNode* node)
    cdef void _link_node(self, HashNode* prev, HashNode* curr, HashNode* next)
    cdef void _unlink_node(self, HashNode* curr)
    cdef (HashNode*, HashNode*, size_t) _stage_nodes(
        self, PyObject* items, bint reverse, set override = *
    )
    cdef HashNode* _node_at_index(self, size_t index)
    cdef (size_t, size_t) _get_slice_direction(
        self,
        size_t start,
        size_t stop,
        ssize_t step,
    )
    cdef (KeyNode*, KeyNode*) _decorate(self, PyObject* key)
    cdef (HashNode*, HashNode*) _undecorate(self, KeyNode* head)
    cdef HashNode* _split(self, HashNode* head, size_t length)
    cdef KeyNode* _split_decorated(self, KeyNode* curr, size_t length)
    cdef (HashNode*, HashNode*) _merge(
        self,
        HashNode* left,
        HashNode* right,
        HashNode* temp,
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
        HashNode* head,
        HashNode* tail,
        HashNode* sub_left,
        HashNode* sub_right,
        HashNode* curr,
    )
    cdef size_t _free_decorated(self, KeyNode* head)
    cdef void _remember_node(self, HashNode* node)
    cdef void _forget_node(self, HashNode* node)
    cdef HashNode* _search(self, PyObject* key)
    cdef HashNode* _search_node(self, HashNode* node)
    cdef void _resize_table(self)
    cdef void _clear_tombstones(self)
