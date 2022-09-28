from .base cimport ElementType

cdef class BaseComplexType(ElementType):
    pass

cdef class ComplexType(BaseComplexType):
    cdef readonly:
        long min
        long max

cdef class Complex64Type(BaseComplexType):
    cdef readonly:
        int min
        int max

cdef class Complex128Type(BaseComplexType):
    cdef readonly:
        long min
        long max

cdef class CLongDoubleType(BaseComplexType):
    cdef readonly:
        object min
        object max
