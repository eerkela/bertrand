from .base cimport ElementType


cdef class ComplexType(ElementType):
    cdef:
        ElementType _equiv_float

    cdef readonly:
        object min
        object max


cdef class Complex64Type(ComplexType):
    pass


cdef class Complex128Type(ComplexType):
    pass


cdef class CLongDoubleType(ComplexType):
    pass
