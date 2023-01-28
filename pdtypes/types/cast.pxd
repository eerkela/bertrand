cimport numpy as np

cimport pdtypes.types.atomic as atomic


cdef class SeriesWrapper:
    cdef:
        object _hasinfs  # bint can't store None
        object _hasnans  # bint can't store None
        atomic.BaseType _element_type
        object _series

    cdef readonly:
        object fill_value
        object original_index
        unsigned int size

    cdef public:
        dict cache
