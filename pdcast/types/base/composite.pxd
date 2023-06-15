cimport numpy as np
from .registry cimport Type
from .scalar cimport ScalarType


cdef class CompositeType(Type):
    cdef readonly:
        np.ndarray _index
        set types

    cdef void forget_index(self)
