cimport numpy as np

cimport pdcast.types as types


cdef class SeriesWrapper:
    cdef:
        object _max
        object _min
        object _original_index
        tuple _original_shape
        object _series

    cdef readonly:
        types.BaseType _element_type
        object _hasnans  # bint can't store None
