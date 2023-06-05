"""This module contains all the prepackaged complex types for the ``pdcast``
type system.
"""
import sys

import numpy as np
cimport numpy as np

from pdcast.util.type_hints import numeric

from .base cimport ScalarType, AbstractType, CompositeType
from .base import register
import pdcast.types.float as float_types


##################################
####    MIXINS & CONSTANTS    ####
##################################


# NOTE: x86 extended precision float type (long double) is platform-specific
# and may not be exposed depending on hardware configuration.
cdef bint has_clongdouble = (np.dtype(np.clongdouble).itemsize > 16)


class ComplexMixin:

    is_numeric = True

    ############################
    ####    TYPE METHODS    ####
    ############################

    @property
    def equiv_float(self) -> ScalarType:
        f_root = float_types.FloatType()
        candidates = [x for y in f_root.backends.values() for x in y.subtypes]
        for x in candidates:
            if type(x).__name__ == self._equiv_float:
                return x
        raise TypeError(f"{repr(self)} has no equivalent float type")


############################
####    ROOT COMPLEX    ####
############################


@register
class ComplexType(AbstractType):

    name = "complex"
    aliases = {
        "complex", "cfloat", "complex float", "complex floating", "c"
    }
    _equiv_float = "FloatType"


@register
@ComplexType.implementation("numpy")
class NumpyComplexType(AbstractType):

    aliases = {np.complexfloating}
    _equiv_float = "NumpyFloatType"


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
    _equiv_float = "Float32Type"


@register
@NumpyComplexType.subtype
@Complex64Type.default
@Complex64Type.implementation("numpy")
class NumpyComplex64Type(ComplexMixin, ScalarType):

    aliases = {np.complex64, np.dtype(np.complex64)}
    dtype = np.dtype(np.complex64)
    itemsize = 8
    na_value = np.complex64("nan+nanj")
    type_def = np.complex64
    max = 2**24
    min = -2**24
    _equiv_float = "NumpyFloat32Type"


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
    _equiv_float = "Float64Type"


@register
@NumpyComplexType.default
@NumpyComplexType.subtype
@Complex128Type.default
@Complex128Type.implementation("numpy")
class NumpyComplex128Type(ComplexMixin, ScalarType):

    aliases = {np.complex128, np.dtype(np.complex128)}
    dtype = np.dtype(np.complex128)
    itemsize = 16
    na_value = np.complex128("nan+nanj")
    type_def = np.complex128
    max = 2**53
    min = -2**53
    _equiv_float = "NumpyFloat64Type"


@register
@ComplexType.implementation("python")
@Complex128Type.implementation("python")
class PythonComplexType(ComplexMixin, ScalarType):

    aliases = {complex}
    itemsize = sys.getsizeof(0+0j)
    na_value = complex("nan+nanj")
    type_def = complex
    max = 2**53
    min = -2**53
    _equiv_float = "PythonFloatType"


#########################################
####    COMPLEX LONG DOUBLE (x86)    ####
#########################################


@register(cond=has_clongdouble)
@ComplexType.subtype
class Complex160Type(AbstractType):

    name = "complex160"
    aliases = {
        "complex160", "clongdouble", "clongfloat", "complex longdouble",
        "complex longfloat", "complex long double", "complex long float",
        "longcomplex", "long complex", "c20", "G"
    }
    _equiv_float = "Float80Type"


@register(cond=has_clongdouble)
@NumpyComplexType.subtype
@Complex160Type.default
@Complex160Type.implementation("numpy")
class NumpyComplex160Type(ComplexMixin, ScalarType):

    aliases = {np.clongdouble, np.dtype(np.clongdouble)}
    dtype = np.dtype(np.clongdouble)
    itemsize = np.dtype(np.clongdouble).itemsize
    na_value = np.clongdouble("nan+nanj")
    type_def = np.clongdouble
    max = 2**64
    min = -2**64
    _equiv_float = "NumpyFloat80Type"
