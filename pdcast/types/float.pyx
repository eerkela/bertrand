"""This module contains all the prepackaged float types for the ``pdcast``
type system.
"""
import decimal
import sys

import numpy as np

from pdcast.util.type_hints import numeric

from .base cimport ScalarType, AbstractType, CompositeType
from .base import register


##########################
####    ROOT FLOAT    ####
##########################


@register
class FloatType(AbstractType):
    """Generic float supertype"""

    name = "float"
    aliases = {"float", "floating", "f"}

    @property
    def equiv_complex(self) -> ScalarType:
        """An equivalent complex type."""
        from .complex import ComplexType

        return self.registry[ComplexType]


@register
@FloatType.implementation("numpy")
class NumpyFloatType(AbstractType):

    aliases = {np.floating}

    @property
    def equiv_complex(self) -> ScalarType:
        """An equivalent complex type."""
        from .complex import NumpyComplexType

        return self.registry[NumpyComplexType]


#######################
####    FLOAT16    ####
#######################


@register
@FloatType.subtype
class Float16Type(AbstractType):

    name = "float16"
    aliases = {"float16", "half", "f2", "e"}

    @property
    def equiv_complex(self) -> ScalarType:
        """An equivalent complex type."""
        from .complex import Complex64Type

        return self.registry[Complex64Type]


@register
@NumpyFloatType.subtype
@Float16Type.default
@Float16Type.implementation("numpy")
class NumpyFloat16Type(ScalarType):

    aliases = {np.float16, np.dtype(np.float16)}
    dtype = np.dtype(np.float16)
    itemsize = 2
    na_value = np.nan
    type_def = np.float16
    is_numeric = True
    max = 2**11
    min = -2**11

    @property
    def equiv_complex(self) -> ScalarType:
        """An equivalent complex type."""
        from .complex import NumpyComplex64Type

        return self.registry[NumpyComplex64Type]


#######################
####    FLOAT32    ####
#######################


@register
@FloatType.subtype
class Float32Type(AbstractType):

    name = "float32"
    aliases = {"float32", "single", "f4"}

    @property
    def equiv_complex(self) -> ScalarType:
        """An equivalent complex type."""
        from .complex import Complex64Type

        return self.registry[Complex64Type]


@register
@NumpyFloatType.subtype
@Float32Type.default
@Float32Type.implementation("numpy")
class NumpyFloat32Type(ScalarType):

    aliases = {np.float32, np.dtype(np.float32)}
    dtype = np.dtype(np.float32)
    itemsize = 4
    na_value = np.nan
    type_def = np.float32
    is_numeric = True
    max = 2**24
    min = -2**24

    @property
    def equiv_complex(self) -> ScalarType:
        """An equivalent complex type."""
        from .complex import NumpyComplex64Type

        return self.registry[NumpyComplex64Type]


#######################
####    FLOAT64    ####
#######################


@register
@FloatType.default
@FloatType.subtype
class Float64Type(AbstractType):

    name = "float64"
    aliases = {"float64", "double", "float_", "f8", "d"}

    @property
    def equiv_complex(self) -> ScalarType:
        """An equivalent complex type."""
        from .complex import Complex128Type

        return self.registry[Complex128Type]


@register
@NumpyFloatType.default
@NumpyFloatType.subtype
@Float64Type.default
@Float64Type.implementation("numpy")
class NumpyFloat64Type(ScalarType):

    aliases = {np.float64, np.dtype(np.float64)}
    dtype = np.dtype(np.float64)
    itemsize = 8
    na_value = np.nan
    type_def = np.float64
    is_numeric = True
    max = 2**53
    min = -2**53

    @property
    def equiv_complex(self) -> ScalarType:
        """An equivalent complex type."""
        from .complex import NumpyComplex128Type

        return self.registry[NumpyComplex128Type]


@register
@FloatType.implementation("python")
@Float64Type.implementation("python")
class PythonFloatType(ScalarType):

    aliases = {float}
    itemsize = sys.getsizeof(0.0)
    na_value = np.nan
    type_def = float
    is_numeric = True
    max = 2**53
    min = -2**53

    @property
    def equiv_complex(self) -> ScalarType:
        """An equivalent complex type."""
        from .complex import PythonComplexType

        return self.registry[PythonComplexType]


#################################
####    LONG DOUBLE (x86)    ####
#################################


# NOTE: long doubles are platform-specific and may not be valid depending on
# hardware configuration.
cdef bint has_longdouble = (np.dtype(np.longdouble).itemsize > 8)


@register(cond=has_longdouble)
@FloatType.subtype
class Float80Type(AbstractType):

    name = "float80"
    aliases = {
        "float80", "longdouble", "longfloat", "long double", "long float",
        "f10", "g"
    }

    @property
    def equiv_complex(self) -> ScalarType:
        """An equivalent complex type."""
        from .complex import Complex160Type

        return self.registry[Complex160Type]


@register(cond=has_longdouble)
@NumpyFloatType.subtype
@Float80Type.default
@Float80Type.implementation("numpy")
class NumpyFloat80Type(ScalarType):

    aliases = {np.longdouble, np.dtype(np.longdouble)}
    dtype = np.dtype(np.longdouble)
    itemsize = np.dtype(np.longdouble).itemsize
    na_value = np.nan
    type_def = np.longdouble
    is_numeric = True
    max = 2**64
    min = -2**64

    @property
    def equiv_complex(self) -> ScalarType:
        """An equivalent complex type."""
        from .complex import NumpyComplex160Type

        return self.registry[NumpyComplex160Type]
