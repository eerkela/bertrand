cimport numpy as np

cimport pdtypes.types.atomic as atomic


cdef class SeriesWrapper:
    cdef:
        object _series

    cdef readonly:
        atomic.BaseType _element_type
        object _hasnans  # bint can't store None
        object fill_value
        object original_index
        unsigned int size

    cdef public:
        dict cache
