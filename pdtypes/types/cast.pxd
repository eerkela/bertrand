cimport numpy as np

cimport pdtypes.types.atomic as atomic


cdef class SeriesWrapper:
    cdef:
        object _max
        object _min
        object _original_index
        tuple _original_shape
        object _series

    cdef readonly:
        atomic.BaseType _element_type
        object _hasnans  # bint can't store None
