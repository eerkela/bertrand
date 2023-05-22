cimport numpy as np
from .registry cimport BaseType


cdef class CompositeType(BaseType):
    cdef readonly:
        set _types

    cdef public:
        np.ndarray index

    cdef void forget_index(self)
