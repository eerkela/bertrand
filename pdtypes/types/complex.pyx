import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport compute_hash, ElementType, shared_registry


##########################
####    SUPERTYPES    ####
##########################


cdef class ComplexType(ElementType):
    """Complex supertype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        self.sparse = sparse,
        self.categorical = categorical
        self.nullable = True
        self.supertype = None
        self.subtypes = frozenset(
            t.instance(sparse=sparse, categorical=categorical)
            for t in (Complex64Type, Complex128Type, CLongDoubleType)
        )
        self.atomic_type = complex
        self.numpy_type = None
        self.pandas_type = None
        self.slug = "complex"
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


cdef class Complex64Type(ComplexType):
    """64-bit complex subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        self.sparse = sparse,
        self.categorical = categorical
        self.nullable = True
        self.supertype = ComplexType
        self.subtypes = frozenset()
        self.atomic_type = np.complex64
        self.numpy_type = np.dtype(np.complex64)
        self.pandas_type = None
        self.slug = "complex64"
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**24
        self.max = 2**24


cdef class Complex128Type(ComplexType):
    """128-bit complex subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        self.sparse = sparse,
        self.categorical = categorical
        self.nullable = True
        self.supertype = ComplexType
        self.subtypes = frozenset()
        self.atomic_type = np.complex128
        self.numpy_type = np.dtype(np.complex128)
        self.pandas_type = None
        self.slug = "complex128"
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**53
        self.max = 2**53


cdef class CLongDoubleType(ComplexType):
    """complex long double subtype (platform-dependent)"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        self.sparse = sparse,
        self.categorical = categorical
        self.nullable = True
        self.supertype = ComplexType
        self.subtypes = frozenset()
        self.atomic_type = np.clongdouble
        self.numpy_type = np.dtype(np.clongdouble)
        self.pandas_type = None
        self.slug = "clongdouble"
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**64
        self.max = 2**64
