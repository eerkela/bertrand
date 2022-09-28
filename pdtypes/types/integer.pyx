import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType


cdef class BaseIntegerType(ElementType):
    """Base class for integer types."""
    pass


##########################
####    SUPERTYPES    ####
##########################


cdef class IntegerType(BaseIntegerType):
    """Integer supertype"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("integer supertype has no valid extension type")
        self.supertype = None
        self.subtypes = (
            Int8Type, Int16Type, Int32Type, Int64Type, UInt8Type, UInt16Type,
            UInt32Type, UInt64Type
        )
        self.atomic_type = int
        self.extension_type = None
        self.min = -np.inf
        self.max = np.inf


cdef class SignedIntegerType(BaseIntegerType):
    """Signed integer supertype"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("signed integer supertype has no valid extension "
                             "type")
        self.supertype = IntegerType
        self.subtypes = (Int8Type, Int16Type, Int32Type, Int64Type)
        self.atomic_type = None
        self.extension_type = None
        self.min = -2**63
        self.max = 2**63 - 1


cdef class UnsignedIntegerType(BaseIntegerType):
    """Unsigned integer supertype"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("unsigned integer supertype has no valid "
                             "extension type")
        self.supertype = IntegerType
        self.subtypes = (UInt8Type, UInt16Type, UInt32Type, UInt64Type)
        self.atomic_type = None
        self.extension_type = None
        self.min = 0
        self.max = 2**64 - 1


########################
####    SUBTYPES    ####
########################


cdef class Int8Type(BaseIntegerType):
    """8-bit integer subtype"""

    def __cinit__(self):
        self.supertype = IntegerType
        self.subtypes = ()
        self.atomic_type = np.int8
        self.extension_type = pd.Int8Dtype()
        self.min = -2**7
        self.max = 2**7 - 1


cdef class Int16Type(BaseIntegerType):
    """16-bit integer subtype"""

    def __cinit__(self):
        self.supertype = IntegerType
        self.subtypes = ()
        self.atomic_type = np.int16
        self.extension_type = pd.Int16Dtype()
        self.min = -2**15
        self.max = 2**15 - 1


cdef class Int32Type(BaseIntegerType):
    """32-bit integer subtype"""

    def __cinit__(self):
        self.supertype = IntegerType
        self.subtypes = ()
        self.atomic_type = np.int32
        self.extension_type = pd.Int32Dtype()
        self.min = -2**31
        self.max = 2**31 - 1


cdef class Int64Type(BaseIntegerType):
    """64-bit integer subtype"""

    def __cinit__(self):
        self.supertype = IntegerType
        self.subtypes = ()
        self.atomic_type = np.int64
        self.extension_type = pd.Int64Dtype()
        self.min = -2**63
        self.max = 2**63 - 1


cdef class UInt8Type(BaseIntegerType):
    """8-bit unsigned integer subtype"""

    def __cinit__(self):
        self.supertype = IntegerType
        self.subtypes = ()
        self.atomic_type = np.uint8
        self.extension_type = pd.UInt8Dtype()
        self.min = 0
        self.max = 2**8 - 1


cdef class UInt16Type(BaseIntegerType):
    """16-bit unsigned integer subtype"""

    def __cinit__(self):
        self.supertype = IntegerType
        self.subtypes = ()
        self.atomic_type = np.uint16
        self.extension_type = pd.UInt16Dtype()
        self.min = 0
        self.max = 2**16 - 1


cdef class UInt32Type(BaseIntegerType):
    """32-bit unsigned integer subtype"""

    def __cinit__(self):
        self.supertype = IntegerType
        self.subtypes = ()
        self.atomic_type = np.uint32
        self.extension_type = pd.UInt32Dtype()
        self.min = 0
        self.max = 2**32 - 1


cdef class UInt64Type(BaseIntegerType):
    """32-bit unsigned integer subtype"""

    def __cinit__(self):
        self.supertype = IntegerType
        self.subtypes = ()
        self.atomic_type = np.uint64
        self.extension_type = pd.UInt64Dtype()
        self.min = 0
        self.max = 2**64 - 1
