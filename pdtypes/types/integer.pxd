from .base cimport ElementType

cdef class BaseIntegerType(ElementType):
    pass

##########################
####    SUPERTYPES    ####
##########################

cdef class IntegerType(BaseIntegerType):
    cdef readonly:
        float min
        float max

cdef class SignedIntegerType(BaseIntegerType):
    cdef readonly:
        long min
        long max

cdef class UnsignedIntegerType(BaseIntegerType):
    cdef readonly:
        unsigned long min
        unsigned long max

########################
####    SUBTYPES    ####
########################

cdef class Int8Type(BaseIntegerType):
    cdef readonly:
        char min
        char max

cdef class Int16Type(BaseIntegerType):
    cdef readonly:
        short min
        short max

cdef class Int32Type(BaseIntegerType):
    cdef readonly:
        int min
        int max

cdef class Int64Type(BaseIntegerType):
    cdef readonly:
        long min
        long max

cdef class UInt8Type(BaseIntegerType):
    cdef readonly:
        unsigned char min
        unsigned char max

cdef class UInt16Type(BaseIntegerType):
    cdef readonly:
        unsigned short min
        unsigned short max

cdef class UInt32Type(BaseIntegerType):
    cdef readonly:
        unsigned int min
        unsigned int max

cdef class UInt64Type(BaseIntegerType):
    cdef readonly:
        unsigned long min
        unsigned long max
