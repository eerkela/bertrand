import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport compute_hash, ElementType, shared_registry


##########################
####    SUPERTYPES    ####
##########################


cdef class FloatType(ElementType):
    """Float supertype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = True
        self.supertype = None
        self.subtypes = frozenset(
            t.instance(sparse=sparse, categorical=categorical)
            for t in (Float16Type, Float32Type, Float64Type, LongDoubleType)
        )
        self.atomic_type = float
        self.numpy_type = None
        self.pandas_type = None
        self.slug = "float"
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**53
        self.max = 2**53


########################
####    SUBTYPES    ####
########################


cdef class Float16Type(FloatType):
    """16-bit float subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = True
        self.supertype = FloatType
        self.subtypes = frozenset()
        self.atomic_type = np.float16
        self.numpy_type = np.dtype(np.float16)
        self.pandas_type = None
        self.slug = "float16"
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**11
        self.max = 2**11


cdef class Float32Type(FloatType):
    """32-bit float subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = True
        self.supertype = FloatType
        self.subtypes = frozenset()
        self.atomic_type = np.float32
        self.numpy_type = np.dtype(np.float32)
        self.pandas_type = None
        self.slug = "float32"
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**24
        self.max = 2**24


cdef class Float64Type(FloatType):
    """64-bit float subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = True
        self.supertype = FloatType
        self.subtypes = frozenset()
        self.atomic_type = np.float64
        self.numpy_type = np.dtype(np.float64)
        self.pandas_type = None
        self.slug = "float64"
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**53
        self.max = 2**53


cdef class LongDoubleType(FloatType):
    """Long double float subtype (platform-dependent)"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = True
        self.supertype = FloatType
        self.subtypes = frozenset()
        self.atomic_type = np.longdouble
        self.numpy_type = np.dtype(np.longdouble)
        self.pandas_type = None
        self.slug = "longdouble"
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**64
        self.max = 2**64
