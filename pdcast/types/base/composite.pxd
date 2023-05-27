cimport numpy as np
from .registry cimport BaseType


cdef class CompositeType(BaseType):
    cdef readonly:
        set types
        np.ndarray index

    cdef void forget_index(self)
