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
        self.atomic_type = complex
        self.numpy_type = np.dtype(np.complex128)
        self.pandas_type = None
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # generate slug
        self.slug = "complex"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))
        self.subtypes |= {
            t.instance(sparse=sparse, categorical=categorical)
            for t in (Complex64Type, Complex128Type, CLongDoubleType)
        }

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
        self.atomic_type = np.complex64
        self.numpy_type = np.dtype(np.complex64)
        self.pandas_type = None
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # generate slug
        self.slug = "complex64"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))

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
        self.atomic_type = np.complex128
        self.numpy_type = np.dtype(np.complex128)
        self.pandas_type = None
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # generate slug
        self.slug = "complex128"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))

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
        self.atomic_type = np.clongdouble
        self.numpy_type = np.dtype(np.clongdouble)
        self.pandas_type = None
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # generate slug
        self.slug = "clongdouble"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))

        # min/max representable integer (determined by size of significand)
        self.min = -2**64
        self.max = 2**64
