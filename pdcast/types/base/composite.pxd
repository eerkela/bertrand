cimport numpy as np
from .atomic cimport BaseType


cdef class CompositeType(BaseType):
    cdef readonly:
        set atomic_types

    cdef public:
        np.ndarray index

    cdef void forget_index(self)
