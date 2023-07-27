from cpython.ref cimport PyObject


#########################
####    CONSTANTS    ####
#########################


cdef bint DEBUG


#######################
####    STRUCTS    ####
#######################


cdef packed struct SingleNode:
    PyObject* value
    SingleNode* next


cdef packed struct DoubleNode:
    PyObject* value   # reference to underlying Python object
    DoubleNode* next    # reference to the next node in the list
    DoubleNode* prev    # reference to the previous node in the list


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

