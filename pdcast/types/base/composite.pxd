cimport numpy as np
from .registry cimport Type
from .scalar cimport ScalarType


cdef class CompositeType(Type):
    cdef:
        np.ndarray _index

    cdef readonly:
        set types

    cdef void forget_index(self)
