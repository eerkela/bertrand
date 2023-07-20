from cpython.ref cimport PyObject


#########################
####    CONSTANTS    ####
#########################


cdef bint DEBUG
cdef size_t INITIAL_TABLE_SIZE
cdef float MAX_LOAD_FACTOR
cdef float MAX_TOMBSTONES
cdef size_t[28] PRIMES


cdef void raise_exception() except *


#######################
####    STRUCTS    ####
#######################


# simple pair type that can store arbitrary pointers
cdef packed struct Pair:
    void* first
    void* second


# traditional doubly-linked list node with manual reference counting
cdef packed struct Node:
    PyObject* value   # reference to underlying Python object
    size_t ref_count  # manual refcounter to avoid memory leaks/dangling pointers
    Node* next  # reference to the next node in the list
    Node* prev  # reference to the previous node in the list


# specialized Node that stores the hash of the associated object
cdef packed struct HashNode:
    PyObject* value
    size_t ref_count
    HashNode* next
    HashNode* prev
    Py_hash_t hash    # cached value of `PyObject_Hash()`


# specialized HashNode that also stores a reference to a mapped value,
# allowing it to act as a dictionary
cdef packed struct DictNode:
    PyObject* value
    size_t ref_count
    DictNode* next
    DictNode* prev
    Py_hash_t hash
    PyObject* mapped  # reference to the mapped value


# fused type that describes any kind of linked list-compatible struct type
cdef typedef fused ListNode:
    Node
    HashNode
    DictNode


# fused type that describes all ListNodes that maintain a hash value
cdef typedef fused HashableNode:
    HashNode
    DictNode


# hash map that looks up structs by their underlying value
cdef typedef fused FlatTable:
    HashNode** lookup     # array of HashNode references
    HashNode* tombstone   # sentinel for a value that was removed from the table
    size_t tombstone_count  # counts the number of tombstones in table
    size_t size      # total number of slots in table
    size_t occupied  # counts number of occupied slots in table (incl.tombstones)
    size_t exponent  # log2(size) - log2(INITIAL_TABLE_SIZE)
    size_t prime     # prime number used for double hashing


# hash map that also stores a reference to a mapped value, allowing it to act
# as a Python-to-Python dictionary
cdef struct DictTable:
    DictNode** lookup     # array of DictNode references
    DictNode* tombstone
    size_t tombstone_count
    size_t size
    size_t occupied
    size_t exponent
    size_t prime


# fused type that describes any kind of hash map-compatible struct type
cdef typedef fused ListTable:
    FlatTable
    DictTable


##################################
####    LISTSTRUCT METHODS    ####
##################################


cdef Node* allocate_node(PyObject* value)
cdef HashNode* allocate_hash_node(PyObject* value)
cdef DictNode* allocate_dict_node(PyObject* value, PyObject* mapped)
cdef void incref(ListNode* node)
cdef void decref(ListNode* node)
cdef void replace_value(ListNode* node, PyObject* value)
cdef void replace_mapped(DictNode* node, PyObject* mapped)


#################################
####    LISTTABLE METHODS    ####
#################################


cdef FlatTable* allocate_flat_table()
cdef DictTable* allocate_dict_table()
cdef void free_table(ListTable* table)
cdef void insert(ListTable* table, ListNode* node)
cdef ListNode* search(ListTable* table, PyObject* value)
cdef void remove(ListTable* table, ListNode* node)
cdef void resize(ListTable* table)
