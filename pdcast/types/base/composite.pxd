cimport numpy as np
from .registry cimport Type


cdef class CompositeType(Type):
    cdef readonly:
        set types
        np.ndarray index

    cdef void forget_index(self)
