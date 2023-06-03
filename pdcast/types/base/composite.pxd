cimport numpy as np
from .registry cimport Type
from .scalar cimport ScalarType


cdef class CompositeType(Type):
    cdef readonly:
        set types
        ScalarType[:] _index

    cdef void forget_index(self)
