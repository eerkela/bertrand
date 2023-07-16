
cdef class LRUDict(dict):
    cdef readonly:
        unsigned int maxsize
        list order

    cdef void move_to_end(self, key)
    cdef void purge(self)
