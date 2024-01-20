from .list cimport HashedList


cdef class LRUDict(dict):
    cdef readonly:
        long long maxsize
        HashedList order

    cdef void purge(self)
