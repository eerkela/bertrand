import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType


##########################
####    SUPERTYPES    ####
##########################


cdef class ComplexType(ElementType):
    """Complex supertype"""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = True
        self.supertype = None
        self.subtypes = (Complex64Type, Complex128Type, CLongDoubleType)
        self.atomic_type = complex
        self.extension_type = None
        self.min = -2**53
        self.max = 2**53
        self.slug = "complex"

    def __repr__(self) -> str:
        return (
            f"{self.__class__.__name__}("
            f"categorical={self.categorical}, "
            f"sparse={self.sparse}"
            f")"
        )

    def __str__(self) -> str:
        cdef str result = self.slug

        # append extensions
        if self.categorical:
            result = f"categorical[{result}]"
        if self.sparse:
            result = f"sparse[{result}]"

        return result


########################
####    SUBTYPES    ####
########################


cdef class Complex64Type(ComplexType):
    """64-bit complex subtype"""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = True
        self.supertype = ComplexType
        self.subtypes = ()
        self.atomic_type = np.complex64
        self.extension_type = None
        self.min = -2**24
        self.max = 2**24
        self.slug = "complex64"


cdef class Complex128Type(ComplexType):
    """128-bit complex subtype"""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = True
        self.supertype = ComplexType
        self.subtypes = ()
        self.atomic_type = np.complex128
        self.extension_type = None
        self.min = -2**53
        self.max = 2**53
        self.slug = "complex128"


cdef class CLongDoubleType(ComplexType):
    """complex long double subtype (platform-dependent)"""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = True
        self.supertype = ComplexType
        self.subtypes = ()
        self.atomic_type = np.clongdouble
        self.extension_type = None
        self.min = -2**64
        self.max = 2**64
        self.slug = "clongdouble"
