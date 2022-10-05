import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType


##########################
####    SUPERTYPES    ####
##########################


cdef class FloatType(ElementType):
    """Float supertype"""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = True
        self.supertype = None
        self.subtypes = (
            Float16Type, Float32Type, Float64Type, LongDoubleType
        )
        self.atomic_type = float
        self.extension_type = None
        self.min = -2**53
        self.max = 2**53
        self.slug = "float"

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


cdef class Float16Type(FloatType):
    """16-bit float subtype"""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = True
        self.supertype = FloatType
        self.subtypes = ()
        self.atomic_type = np.float16
        self.extension_type = None
        self.min = -2**11
        self.max = 2**11
        self.slug = "float16"


cdef class Float32Type(FloatType):
    """32-bit float subtype"""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = True
        self.supertype = FloatType
        self.subtypes = ()
        self.atomic_type = np.float32
        self.extension_type = None
        self.min = -2**24
        self.max = 2**24
        self.slug = "float32"


cdef class Float64Type(FloatType):
    """64-bit float subtype"""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = True
        self.supertype = FloatType
        self.subtypes = ()
        self.atomic_type = np.float64
        self.extension_type = None
        self.min = -2**53
        self.max = 2**53
        self.slug = "float64"


cdef class LongDoubleType(FloatType):
    """Long double float subtype (platform-dependent)"""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = True
        self.supertype = FloatType
        self.subtypes = ()
        self.atomic_type = np.longdouble
        self.extension_type = None
        self.min = -2**64
        self.max = 2**64
        self.slug = "longdouble"
