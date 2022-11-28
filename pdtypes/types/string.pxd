from .base cimport ElementType


cdef class StringType(ElementType):
    # public
    cdef readonly:
        str storage
        bint is_default
