
cdef class LRUDict(dict):
    cdef readonly:
        unsigned short maxsize
        list priority

    cdef void move_to_end(self, key)
    cdef void purge(self)
