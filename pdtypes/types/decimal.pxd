from .base cimport ElementType

cdef class BaseDecimalType(ElementType):
    pass

cdef class DecimalType(BaseDecimalType):
    cdef readonly:
        float min
        float max
