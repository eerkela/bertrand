from .base cimport ElementType


##########################
####    SUPERTYPES    ####
##########################


cdef class IntegerType(ElementType):
    cdef readonly:
        object min
        object max


cdef class SignedIntegerType(IntegerType):
    pass


cdef class UnsignedIntegerType(IntegerType):
    pass


########################
####    SUBTYPES    ####
########################


cdef class Int8Type(SignedIntegerType):
    pass


cdef class Int16Type(SignedIntegerType):
    pass


cdef class Int32Type(SignedIntegerType):
    pass


cdef class Int64Type(SignedIntegerType):
    pass


cdef class UInt8Type(UnsignedIntegerType):
    pass


cdef class UInt16Type(UnsignedIntegerType):
    pass


cdef class UInt32Type(UnsignedIntegerType):
    pass


cdef class UInt64Type(UnsignedIntegerType):
    pass
