from .base cimport ElementType

cdef class BaseFloatType(ElementType):
    pass

cdef class FloatType(BaseFloatType):
    cdef readonly:
        long min
        long max

cdef class Float16Type(BaseFloatType):
    cdef readonly:
        short min
        short max

cdef class Float32Type(BaseFloatType):
    cdef readonly:
        int min
        int max

cdef class Float64Type(BaseFloatType):
    cdef readonly:
        long min
        long max

cdef class LongDoubleType(BaseFloatType):
    cdef readonly:
        object min
        object max
