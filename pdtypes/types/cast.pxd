cimport numpy as np

cimport pdtypes.types.atomic as atomic


cdef np.ndarray[object] _apply_with_errors(
    np.ndarray[object] arr,
    object call,
    object na_value,
    str errors
)


cdef class SeriesWrapper:
    cdef:
        object _is_na
        object _hasnans  # bint cannot hold None
        object _is_inf
        object _hasinfs  # bint cannot hold None
        object _max
        object _min
        unsigned int _idxmax
        unsigned int _idxmin
        object _real
        object _imag

    cdef readonly:
        atomic.BaseType element_type

    cdef public:
        object series
