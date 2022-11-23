from .base cimport ElementType


cdef class StringType(ElementType):
    # public
    cdef readonly:
        str storage

    # private
    cdef:
        bint is_default
