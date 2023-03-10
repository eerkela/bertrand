from .atomic cimport ScalarType


cdef class AdapterType(ScalarType):
    cdef:
        ScalarType _wrapped
