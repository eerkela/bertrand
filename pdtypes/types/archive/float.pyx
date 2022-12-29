import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType, generate_slug, shared_registry
from .complex cimport (
    ComplexType, Complex64Type, Complex128Type, CLongDoubleType
)


# LongDoubleType -> Float80Type, aliases are platform-specific, and Float80Type
# may not be available.


##########################
####    SUPERTYPES    ####
##########################


cdef class FloatType(ElementType):
    """Float supertype"""

    _base_slug = "float"
    aliases = {
        # type
        float: {},
        np.floating: {"backend": "numpy"},

        # string
        "float": {},
        "floating": {},
        "f": {}
    }

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        super(FloatType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=float,
            numpy_type=np.dtype(np.float64),
            pandas_type=None,
            na_value=np.nan,
            itemsize=8,
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
    def equiv_complex(self) -> ComplexType:
        """Add an imaginary component to this ElementType."""
        if self._equiv_complex is not None:
            return self._equiv_complex

        self._equiv_complex = ComplexType.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._equiv_complex

    @property
    def subtypes(self) -> frozenset:
        if self._subtypes is None:
            self._subtypes = frozenset({self})
            self._subtypes |= {
                t.instance(sparse=self.sparse, categorical=self.categorical)
                for t in (
                    Float16Type, Float32Type, Float64Type, LongDoubleType
                )
            }
        return self._subtypes


########################
####    SUBTYPES    ####
########################


cdef class Float16Type(FloatType):
    """16-bit float subtype"""

    _base_slug = "float16"
    aliases = {
        # type
        np.float16: {"backend": "numpy"},

        # dtype
        np.dtype(np.float16): {"backend": "numpy"},

        # string
        "float16": {},
        "f2": {},
        "half": {},
        "e": {},
    }

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        super(FloatType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=np.float16,
            numpy_type=np.dtype(np.float16),
            pandas_type=None,
            na_value=np.nan,
            itemsize=2,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical
            ),
        )

        # min/max representable integer (determined by size of significand)
        self.min = -2**11
        self.max = 2**11

    @property
    def equiv_complex(self) -> Complex64Type:
        """Add an imaginary component to this ElementType."""
        if self._equiv_complex is not None:
            return self._equiv_complex

        self._equiv_complex = Complex64Type.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._equiv_complex

    @property
    def subtypes(self) -> frozenset:
        return super(FloatType, self).subtypes

    @property
    def supertype(self) -> FloatType:
        if self._supertype is None:
            self._supertype = FloatType.instance(
                sparse=self.sparse,
                categorical=self.categorical
            )
        return self._supertype


cdef class Float32Type(FloatType):
    """32-bit float subtype"""

    _base_slug = "float32"
    aliases = {
        # type
        np.float32: {"backend": "numpy"},

        # dtype
        np.dtype(np.float32): {"backend": "numpy"},

        # string
        "float32": {},
        "f4": {},
        "single": {}
    }

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        super(FloatType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=np.float32,
            numpy_type=np.dtype(np.float32),
            pandas_type=None,
            na_value=np.nan,
            itemsize=4,
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
    def equiv_complex(self) -> Complex64Type:
        """Add an imaginary component to this ElementType."""
        if self._equiv_complex is not None:
            return self._equiv_complex

        self._equiv_complex = Complex64Type.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._equiv_complex

    @property
    def subtypes(self) -> frozenset:
        return super(FloatType, self).subtypes

    @property
    def supertype(self) -> FloatType:
        if self._supertype is None:
            self._supertype = FloatType.instance(
                sparse=self.sparse,
                categorical=self.categorical
            )
        return self._supertype


cdef class Float64Type(FloatType):
    """64-bit float subtype"""

    _base_slug = "float64"
    aliases = {
        # type
        np.float64: {"backend": "numpy"},

        # dtype
        np.dtype(np.float64): {"backend": "numpy"},

        # string
        "float64": {},
        "f8": {},
        "float_": {},
        "double": {},
        "d": {},
    }

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        super(FloatType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=np.float64,
            numpy_type=np.dtype(np.float64),
            pandas_type=None,
            na_value=np.nan,
            itemsize=8,
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
    def equiv_complex(self) -> Complex128Type:
        """Add an imaginary component to this ElementType."""
        if self._equiv_complex is not None:
            return self._equiv_complex

        self._equiv_complex = Complex128Type.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._equiv_complex

    @property
    def subtypes(self) -> frozenset:
        return super(FloatType, self).subtypes

    @property
    def supertype(self) -> FloatType:
        if self._supertype is None:
            self._supertype = FloatType.instance(
                sparse=self.sparse,
                categorical=self.categorical
            )
        return self._supertype


cdef class LongDoubleType(FloatType):
    """Long double float subtype (platform-dependent)"""

    _base_slug = "float80"
    aliases = {} if np.dtype(np.longdouble) == np.dtype(np.float64) else {
        # type
        np.longdouble: {"backend": "numpy"},

        # dtype
        np.dtype(np.longdouble): {"backend": "numpy"},

        # string
        "float80": {},
        "long double": {},
        "long float": {},
        "longdouble": {},
        "longfloat": {},
        "f10": {},
        "g": {}
    }

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        super(FloatType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=np.longdouble,
            numpy_type=np.dtype(np.longdouble),
            pandas_type=None,
            na_value=np.nan,
            itemsize=np.dtype(np.longdouble).itemsize,  # platform-specific
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
    def equiv_complex(self) -> CLongDoubleType:
        """Add an imaginary component to this ElementType."""
        if self._equiv_complex is not None:
            return self._equiv_complex

        self._equiv_complex = CLongDoubleType.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._equiv_complex

    @property
    def subtypes(self) -> frozenset:
        return super(FloatType, self).subtypes

    @property
    def supertype(self) -> FloatType:
        if self._supertype is None:
            self._supertype = FloatType.instance(
                sparse=self.sparse,
                categorical=self.categorical
            )
        return self._supertype
