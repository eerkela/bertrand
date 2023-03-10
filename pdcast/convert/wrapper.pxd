cimport numpy as np

cimport pdcast.types as types


cdef class SeriesWrapper:
    cdef:
        object _max
        object _min
        object _orig_index
        tuple _orig_shape
        types.AdapterType _orig_type
        object _series

    cdef readonly:
        types.BaseType _element_type
        object _hasnans  # bint can't store None
