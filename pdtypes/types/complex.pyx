import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType


cdef class BaseComplexType(ElementType):
    """Base class for float types."""


##########################
####    SUPERTYPES    ####
##########################


cdef class ComplexType(BaseComplexType):
    """Float supertype"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("complex numbers have no valid extension type")
        self.supertype = None
        self.subtypes = (Complex64Type, Complex128Type, CLongDoubleType)
        self.atomic_type = complex
        self.extension_type = None
        self.min = -2**53
        self.max = 2**53


########################
####    SUBTYPES    ####
########################


cdef class Complex64Type(BaseComplexType):
    """64-bit complex subtype"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("complex numbers have no valid extension type")
        self.supertype = ComplexType
        self.subtypes = ()
        self.atomic_type = np.complex64
        self.extension_type = None
        self.min = -2**24
        self.max = 2**24


cdef class Complex128Type(BaseComplexType):
    """128-bit complex subtype"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("complex numbers have no valid extension type")
        self.supertype = ComplexType
        self.subtypes = ()
        self.atomic_type = np.complex128
        self.extension_type = None
        self.min = -2**53
        self.max = 2**53


cdef class CLongDoubleType(BaseComplexType):
    """complex long double subtype (platform-dependent)"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("complex numbers have no valid extension type")
        self.supertype = ComplexType
        self.subtypes = ()
        self.atomic_type = np.clongdouble
        self.extension_type = None
        self.min = -2**64
        self.max = 2**64
