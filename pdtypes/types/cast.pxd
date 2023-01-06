cimport pdtypes.types.atomic as atomic


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
