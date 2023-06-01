"""This module contains all the prepackaged float types for the ``pdcast``
type system.
"""
import decimal
import sys

import numpy as np
cimport numpy as np

from pdcast.util.type_hints import numeric

from .base cimport AtomicType, CompositeType
from .base import generic, register, supertype
import pdcast.types.complex as complex_types


##################################
####    MIXINS & CONSTANTS    ####
##################################


# NOTE: x86 extended precision floating point (long double) is
# platform-specific and may not be exposed depending on hardware configuration.
cdef bint has_longdouble = (np.dtype(np.longdouble).itemsize > 8)


class FloatMixin:

    is_numeric = True

    ############################
    ####    TYPE METHODS    ####
    ############################

    @property
    def equiv_complex(self) -> AtomicType:
        c_root = complex_types.ComplexType.instance()
        candidates = [x for y in c_root.backends.values() for x in y.subtypes]
        for x in candidates:
            if type(x).__name__ == self._equiv_complex:
                return x
        raise TypeError(f"{repr(self)} has no equivalent complex type")

    @property
    def smaller(self) -> list:
        # get candidates
        root = self.root
        result = [x for x in root.subtypes if x not in root.backends.values()]

        # filter off any that are larger than self
        if not self.is_root:
            result = [
                x for x in result if (
                    (x.itemsize or np.inf) < (self.itemsize or np.inf)
                )
            ]

        # sort by itemsize
        result.sort(key=lambda x: x.itemsize)
        return result


#######################
####    GENERIC    ####
#######################


@register
@generic
@supertype
class FloatType(FloatMixin, AtomicType):
    """Generic float supertype"""

    name = "float"
    aliases = {"float", "floating", "f"}
    dtype = np.dtype(np.float64)
    itemsize = 8
    na_value = np.nan
    type_def = float
    max = 2**53
    min = -2**53
    _equiv_complex = "ComplexType"


@register
@FloatType.subtype
@generic
class Float16Type(FloatMixin, AtomicType):

    name = "float16"
    aliases = {"float16", "half", "f2", "e"}
    dtype = np.dtype(np.float16)
    itemsize = 2
    na_value = np.nan
    type_def = np.float16
    max = 2**11
    min = -2**11
    _equiv_complex = "Complex64Type"


@register
@FloatType.subtype
@generic
class Float32Type(FloatMixin, AtomicType):

    name = "float32"
    aliases = {"float32", "single", "f4"}
    dtype = np.dtype(np.float32)
    itemsize = 4
    na_value = np.nan
    type_def = np.float32
    max = 2**24
    min = -2**24
    _equiv_complex = "Complex64Type"


@register
@FloatType.subtype
@generic
class Float64Type(FloatMixin, AtomicType):

    name = "float64"
    aliases = {"float64", "double", "float_", "f8", "d"}
    dtype = np.dtype(np.float64)
    itemsize = 8
    na_value = np.nan
    type_def = np.float64
    max = 2**53
    min = -2**53
    _equiv_complex = "Complex128Type"


@register(cond=has_longdouble)
@FloatType.subtype
@generic
class Float80Type(FloatMixin, AtomicType):

    name = "float80"
    aliases = {
        "float80", "longdouble", "longfloat", "long double", "long float",
        "f10", "g"
    }
    dtype = np.dtype(np.longdouble)
    itemsize = np.dtype(np.longdouble).itemsize
    na_value = np.nan
    type_def = np.longdouble
    max = 2**64
    min = -2**64
    _equiv_complex = "Complex160Type"


#####################
####    NUMPY    ####
#####################


@register
@FloatType.implementation("numpy")
@supertype
class NumpyFloatType(FloatMixin, AtomicType):

    aliases = {np.floating}
    dtype = np.dtype(np.float64)
    itemsize = 8
    na_value = np.nan
    type_def = np.float64
    max = 2**53
    min = -2**53
    _equiv_complex = "NumpyComplexType"


@register
@NumpyFloatType.subtype
@Float16Type.implementation("numpy")
class NumpyFloat16Type(FloatMixin, AtomicType):

    aliases = {np.float16, np.dtype(np.float16)}
    dtype = np.dtype(np.float16)
    itemsize = 2
    na_value = np.nan
    type_def = np.float16
    max = 2**11
    min = -2**11
    _equiv_complex = "NumpyComplex64Type"


@register
@NumpyFloatType.subtype
@Float32Type.implementation("numpy")
class NumpyFloat32Type(FloatMixin, AtomicType):

    aliases = {np.float32, np.dtype(np.float32)}
    dtype = np.dtype(np.float32)
    itemsize = 4
    na_value = np.nan
    type_def = np.float32
    max = 2**24
    min = -2**24
    _equiv_complex = "NumpyComplex64Type"


@register
@NumpyFloatType.subtype
@Float64Type.implementation("numpy")
class NumpyFloat64Type(FloatMixin, AtomicType):

    aliases = {np.float64, np.dtype(np.float64)}
    dtype = np.dtype(np.float64)
    itemsize = 8
    na_value = np.nan
    type_def = np.float64
    max = 2**53
    min = -2**53
    _equiv_complex = "NumpyComplex128Type"


@register(cond=has_longdouble)
@NumpyFloatType.subtype
@Float80Type.implementation("numpy")
class NumpyFloat80Type(FloatMixin, AtomicType):

    aliases = {np.longdouble, np.dtype(np.longdouble)}
    dtype = np.dtype(np.longdouble)
    itemsize = np.dtype(np.longdouble).itemsize
    na_value = np.nan
    type_def = np.longdouble
    max = 2**64
    min = -2**64
    _equiv_complex = "NumpyComplex160Type"


######################
####    PYTHON    ####
######################


@register
@FloatType.implementation("python")
@Float64Type.implementation("python")
class PythonFloatType(FloatMixin, AtomicType):

    aliases = {float}
    itemsize = sys.getsizeof(0.0)
    na_value = np.nan
    type_def = float
    max = 2**53
    min = -2**53
    _equiv_complex = "PythonComplexType"
