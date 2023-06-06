"""This module contains all the prepackaged complex types for the ``pdcast``
type system.
"""
import sys

import numpy as np
cimport numpy as np

from pdcast.util.type_hints import numeric

from .base cimport ScalarType, AbstractType, CompositeType
from .base import register


############################
####    ROOT COMPLEX    ####
############################


@register
class ComplexType(AbstractType):

    name = "complex"
    aliases = {
        "complex", "cfloat", "complex float", "complex floating", "c"
    }

    @property
    def equiv_float(self) -> ScalarType:
        """An equivalent floating point type."""
        from .float import FloatType

        return self.registry[FloatType]


@register
@ComplexType.implementation("numpy")
class NumpyComplexType(AbstractType):

    aliases = {np.complexfloating}

    @property
    def equiv_float(self) -> ScalarType:
        """An equivalent floating point type."""
        from .float import NumpyFloatType

        return self.registry[NumpyFloatType]


#########################
####    COMPLEX64    ####
#########################


@register
@ComplexType.subtype
class Complex64Type(AbstractType):

    name = "complex64"
    aliases = {
        "complex64", "csingle", "complex single", "singlecomplex", "c8", "F"
    }

    @property
    def equiv_float(self) -> ScalarType:
        """An equivalent floating point type."""
        from .float import Float32Type

        return self.registry[Float32Type]


@register
@NumpyComplexType.subtype
@Complex64Type.default
@Complex64Type.implementation("numpy")
class NumpyComplex64Type(ScalarType):

    aliases = {np.complex64, np.dtype(np.complex64)}
    dtype = np.dtype(np.complex64)
    itemsize = 8
    na_value = np.complex64("nan+nanj")
    type_def = np.complex64
    is_numeric = True
    max = 2**24
    min = -2**24

    @property
    def equiv_float(self) -> ScalarType:
        """An equivalent floating point type."""
        from .float import NumpyFloat32Type

        return self.registry[NumpyFloat32Type]


##########################
####    COMPLEX128    ####
##########################


@register
@ComplexType.default
@ComplexType.subtype
class Complex128Type(AbstractType):

    name = "complex128"
    aliases = {
        "complex128", "cdouble", "complex double", "complex_", "c16", "D"
    }

    @property
    def equiv_float(self) -> ScalarType:
        """An equivalent floating point type."""
        from .float import Float64Type

        return self.registry[Float64Type]


@register
@NumpyComplexType.default
@NumpyComplexType.subtype
@Complex128Type.default
@Complex128Type.implementation("numpy")
class NumpyComplex128Type(ScalarType):

    aliases = {np.complex128, np.dtype(np.complex128)}
    dtype = np.dtype(np.complex128)
    itemsize = 16
    na_value = np.complex128("nan+nanj")
    type_def = np.complex128
    is_numeric = True
    max = 2**53
    min = -2**53

    @property
    def equiv_float(self) -> ScalarType:
        """An equivalent floating point type."""
        from .float import NumpyFloat64Type

        return self.registry[NumpyFloat64Type]


@register
@ComplexType.implementation("python")
@Complex128Type.implementation("python")
class PythonComplexType(ScalarType):

    aliases = {complex}
    itemsize = sys.getsizeof(0+0j)
    na_value = complex("nan+nanj")
    type_def = complex
    is_numeric = True
    max = 2**53
    min = -2**53

    @property
    def equiv_float(self) -> ScalarType:
        """An equivalent floating point type."""
        from .float import PythonFloatType

        return self.registry[PythonFloatType]


#########################################
####    COMPLEX LONG DOUBLE (x86)    ####
#########################################


# NOTE: long doubles are platform-specific and may not be valid depending on
# hardware configuration.
cdef bint has_clongdouble = (np.dtype(np.clongdouble).itemsize > 16)


@register(cond=has_clongdouble)
@ComplexType.subtype
class Complex160Type(AbstractType):

    name = "complex160"
    aliases = {
        "complex160", "clongdouble", "clongfloat", "complex longdouble",
        "complex longfloat", "complex long double", "complex long float",
        "longcomplex", "long complex", "c20", "G"
    }

    @property
    def equiv_float(self) -> ScalarType:
        """An equivalent floating point type."""
        from .float import Float80Type

        return self.registry[Float80Type]


@register(cond=has_clongdouble)
@NumpyComplexType.subtype
@Complex160Type.default
@Complex160Type.implementation("numpy")
class NumpyComplex160Type(ScalarType):

    aliases = {np.clongdouble, np.dtype(np.clongdouble)}
    dtype = np.dtype(np.clongdouble)
    itemsize = np.dtype(np.clongdouble).itemsize
    na_value = np.clongdouble("nan+nanj")
    type_def = np.clongdouble
    is_numeric = True
    max = 2**64
    min = -2**64

    @property
    def equiv_float(self) -> ScalarType:
        """An equivalent floating point type."""
        from .float import NumpyFloat80Type

        return self.registry[NumpyFloat80Type]
