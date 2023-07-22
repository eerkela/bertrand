from cpython.ref cimport PyObject


#########################
####    CONSTANTS    ####
#########################


cdef bint DEBUG


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
    cdef LinkedList _copy(self)
    cdef void _rotate(self, ssize_t steps = *)
    cdef size_t _normalize_index(self, long index)


#########################
####    FUNCTIONS    ####
#########################


cdef void raise_exception() except *

