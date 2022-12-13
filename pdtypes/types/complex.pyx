import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType, generate_slug, shared_registry
from .float cimport FloatType, Float32Type, Float64Type, LongDoubleType


##########################
####    SUPERTYPES    ####
##########################


cdef class ComplexType(ElementType):
    """Complex supertype"""

    _base_slug = "complex"
    aliases = {
        # type
        complex: {},
        np.complexfloating: {"backend": "numpy"},

        # string
        "complex": {},
        "complex float": {},
        "complex floating": {},
        "cfloat": {},
        "c": {},
    }

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
            na_value=complex("nan+nanj"),
            itemsize=16,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical
            )
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**53
        self.max = 2**53

    @property
    def equiv_float(self) -> FloatType:
        """Remove the imaginary component from this ElementType."""
        if self._equiv_float is not None:
            return self._equiv_float

        self._equiv_float = FloatType.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._equiv_float

    @property
    def subtypes(self) -> frozenset:
        if self._subtypes is None:
            self._subtypes = frozenset({self})
            self._subtypes |= {
                t.instance(sparse=self.sparse, categorical=self.categorical)
                for t in (Complex64Type, Complex128Type, CLongDoubleType)
            }
        return self._subtypes


########################
####    SUBTYPES    ####
########################


cdef class Complex64Type(ComplexType):
    """64-bit complex subtype"""

    _base_slug = "complex64"
    aliases = {
        # type
        np.complex64: {"backend": "numpy"},

        # dtype
        np.dtype(np.complex64): {"backend": "numpy"},

        # string
        "complex64": {},
        "complex single": {},
        "singlecomplex": {},
        "csingle": {},
        "c8": {},
        "F": {},
    }

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
            na_value=complex("nan+nanj"),
            itemsize=8,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical
            )
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**24
        self.max = 2**24

    @property
    def equiv_float(self) -> Float32Type:
        """Remove the imaginary component from this ElementType."""
        if self._equiv_float is not None:
            return self._equiv_float

        self._equiv_float = Float32Type.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._equiv_float

    @property
    def subtypes(self) -> frozenset:
        return super(ComplexType, self).subtypes

    @property
    def supertype(self) -> ComplexType:
        if self._supertype is None:
            self._supertype = ComplexType.instance(
                sparse=self.sparse,
                categorical=self.categorical
            )
        return self._supertype


cdef class Complex128Type(ComplexType):
    """128-bit complex subtype"""

    _base_slug = "complex128"
    aliases = {
        # type
        np.complex128: {"backend": "numpy"},

        # dtype
        np.dtype(np.complex128): {"backend": "numpy"},

        # string
        "complex128": {},
        "complex double": {},
        "complex_": {},
        "cdouble": {},
        "c16": {},
        "D": {},
    }

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
            na_value=complex("nan+nanj"),
            itemsize=16,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical
            )
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**53
        self.max = 2**53

    @property
    def equiv_float(self) -> Float64Type:
        """Remove the imaginary component from this ElementType."""
        if self._equiv_float is not None:
            return self._equiv_float

        self._equiv_float = Float64Type.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._equiv_float

    @property
    def subtypes(self) -> frozenset:
        return super(ComplexType, self).subtypes

    @property
    def supertype(self) -> ComplexType:
        if self._supertype is None:
            self._supertype = ComplexType.instance(
                sparse=self.sparse,
                categorical=self.categorical
            )
        return self._supertype


cdef class ComplexLongDoubleType(ComplexType):
    """complex long double subtype (platform-dependent)"""

    _base_slug = "complex160"
    aliases = {} if np.dtype(np.clongdouble) == np.dtype(np.complex128) else {
        # type
        np.clongdouble: {"backend": "numpy"},

        # dtype
        np.dtype(np.clongdouble): {"backend": "numpy"},

        # string
        "complex160": {},
        "complex long double": {},
        "complex long float": {},
        "complex longdouble": {},
        "complex longfloat": {},
        "clongdouble": {},
        "clongfloat": {},
        "long complex": {},
        "longcomplex": {},
        "c20": {},
        "G": {}
    }

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
            na_value=complex("nan+nanj"),
            itemsize=np.dtype(np.clongdouble).itemsize,  # platform-specific
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical
            )
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**64
        self.max = 2**64

    @property
    def equiv_float(self) -> LongDoubleType:
        """Remove the imaginary component from this ElementType."""
        if self._equiv_float is not None:
            return self._equiv_float

        self._equiv_float = LongDoubleType.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._equiv_float

    @property
    def subtypes(self) -> frozenset:
        return super(ComplexType, self).subtypes

    @property
    def supertype(self) -> ComplexType:
        if self._supertype is None:
            self._supertype = ComplexType.instance(
                sparse=self.sparse,
                categorical=self.categorical
            )
        return self._supertype
