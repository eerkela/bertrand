from .base cimport ElementType


cdef class DecimalType(ElementType):
    cdef readonly:
        float min
        float max
