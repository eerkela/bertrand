from . cimport scalar


cdef class AdapterType(scalar.ScalarType):
    cdef:
        scalar.ScalarType _wrapped
