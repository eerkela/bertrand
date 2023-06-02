cimport numpy as np
from .registry cimport Type
from .atomic cimport AtomicType


cdef class CompositeType(Type):
    cdef readonly:
        set types
        AtomicType[:] _index

    cdef void forget_index(self)
