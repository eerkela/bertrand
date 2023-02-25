cdef dict epoch_aliases

cdef class Epoch:

    cdef readonly:
        object offset  # offset from UTC in ns
        long int year
        unsigned char month
        unsigned char day
        unsigned short ordinal
