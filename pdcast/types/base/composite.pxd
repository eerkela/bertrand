cimport numpy as np
from . cimport registry


cdef class CompositeType(registry.BaseType):
    cdef readonly:
        set _types

    cdef public:
        np.ndarray index

    cdef void forget_index(self)
