import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport CompositeType, compute_hash, ElementType, shared_registry
from .float cimport FloatType, Float32Type, Float64Type, LongDoubleType


# TODO: cache equiv_complex in a private field


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
        super(ComplexType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=complex,
            numpy_type=np.dtype(np.complex128),
            pandas_type=None,
            slug="complex",
            supertype=None,
            subtypes=None  # lazy-loaded
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**53
        self.max = 2**53

    @property
    def equiv_float(self) -> FloatType:
        """Remove the imaginary component from this ElementType."""
        return FloatType.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )

    @property
    def subtypes(self) -> CompositeType:
        # cached
        if self._subtypes is not None:
            return self._subtypes

        # uncached
        subtypes = {self} | {
            t.instance(sparse=self.sparse, categorical=self.categorical)
            for t in (Complex64Type, Complex128Type, CLongDoubleType)
        }
        self._subtypes = CompositeType(subtypes, immutable=True)
        return self._subtypes


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
        super(ComplexType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=np.complex64,
            numpy_type=np.dtype(np.complex64),
            pandas_type=None,
            slug = "complex64",
            supertype=None,  # lazy-loaded
            subtypes=CompositeType({self}, immutable=True)
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**24
        self.max = 2**24

    @property
    def equiv_float(self) -> Float32Type:
        """Remove the imaginary component from this ElementType."""
        return Float32Type.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )

    @property
    def supertype(self) -> ComplexType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = ComplexType.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._supertype


cdef class Complex128Type(ComplexType):
    """128-bit complex subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        super(ComplexType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=np.complex128,
            numpy_type=np.dtype(np.complex128),
            pandas_type=None,
            slug = "complex128",
            supertype=None,  # lazy-loaded
            subtypes=CompositeType({self}, immutable=True)
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**53
        self.max = 2**53

    @property
    def equiv_float(self) -> Float64Type:
        """Remove the imaginary component from this ElementType."""
        return Float64Type.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )

    @property
    def supertype(self) -> ComplexType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = ComplexType.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._supertype


cdef class CLongDoubleType(ComplexType):
    """complex long double subtype (platform-dependent)"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        super(ComplexType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=np.clongdouble,
            numpy_type=np.dtype(np.clongdouble),
            pandas_type=None,
            slug = "clongdouble",
            supertype=None,  # lazy-loaded
            subtypes=CompositeType({self}, immutable=True)
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**64
        self.max = 2**64

    @property
    def equiv_float(self) -> LongDoubleType:
        """Remove the imaginary component from this ElementType."""
        return LongDoubleType.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )

    @property
    def supertype(self) -> ComplexType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = ComplexType.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._supertype
