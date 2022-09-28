import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType


cdef class BaseFloatType(ElementType):
    """Base class for float types."""


##########################
####    SUPERTYPES    ####
##########################


cdef class FloatType(BaseFloatType):
    """Float supertype"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("floats have no valid extension type")
        self.supertype = None
        self.subtypes = (
            Float16Type, Float32Type, Float64Type, LongDoubleType
        )
        self.atomic_type = float
        self.extension_type = None
        self.min = -2**53
        self.max = 2**53


########################
####    SUBTYPES    ####
########################


cdef class Float16Type(BaseFloatType):
    """16-bit float subtype"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("floats have no valid extension type")
        self.supertype = FloatType
        self.subtypes = ()
        self.atomic_type = np.float16
        self.extension_type = None
        self.min = -2**11
        self.max = 2**11


cdef class Float32Type(BaseFloatType):
    """32-bit float subtype"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("floats have no valid extension type")
        self.supertype = FloatType
        self.subtypes = ()
        self.atomic_type = np.float32
        self.extension_type = None
        self.min = -2**24
        self.max = 2**24


cdef class Float64Type(BaseFloatType):
    """64-bit float subtype"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("floats have no valid extension type")
        self.supertype = FloatType
        self.subtypes = ()
        self.atomic_type = np.float64
        self.extension_type = None
        self.min = -2**53
        self.max = 2**53


cdef class LongDoubleType(BaseFloatType):
    """Long double float subtype (platform-dependent)"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("floats have no valid extension type")
        self.supertype = FloatType
        self.subtypes = ()
        self.atomic_type = np.longdouble
        self.extension_type = None
        self.min = -2**64
        self.max = 2**64
