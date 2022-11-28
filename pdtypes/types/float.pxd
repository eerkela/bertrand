from .base cimport ElementType


cdef class FloatType(ElementType):
    cdef:
        ElementType _equiv_complex

    cdef readonly:
        object min
        object max


cdef class Float16Type(FloatType):
    pass


cdef class Float32Type(FloatType):
    pass


cdef class Float64Type(FloatType):
    pass


cdef class LongDoubleType(FloatType):
    pass
