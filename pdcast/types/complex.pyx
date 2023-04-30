"""This module contains all the prepackaged complex types for the ``pdcast``
type system.
"""
import sys

import numpy as np
cimport numpy as np

from pdcast.util.type_hints import numeric

from .base cimport AtomicType, CompositeType
from .base import generic, subtype, register
import pdcast.types.float as float_types


##################################
####    MIXINS & CONSTANTS    ####
##################################


# NOTE: x86 extended precision float type (long double) is platform-specific
# and may not be exposed depending on hardware configuration.
cdef bint has_clongdouble = (np.dtype(np.clongdouble).itemsize > 16)


class ComplexMixin:

    ############################
    ####    TYPE METHODS    ####
    ############################

    @property
    def equiv_float(self) -> AtomicType:
        f_root = float_types.FloatType.instance()
        candidates = [x for y in f_root.backends.values() for x in y.subtypes]
        for x in candidates:
            if type(x).__name__ == self._equiv_float:
                return x
        raise TypeError(f"{repr(self)} has no equivalent float type")

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
class ComplexType(ComplexMixin, AtomicType):

    # internal root fields - all subtypes/backends inherit these
    _is_numeric = True

    name = "complex"
    aliases = {
        "complex", "cfloat", "complex float", "complex floating", "c"
    }
    dtype = np.dtype(np.complex128)
    itemsize = 16
    na_value = complex("nan+nanj")
    type_def = complex
    max = 2**53
    min = -2**53
    _equiv_float = "FloatType"


@register
@generic
@subtype(ComplexType)
class Complex64Type(ComplexMixin, AtomicType):

    name = "complex64"
    aliases = {
        "complex64", "csingle", "complex single", "singlecomplex", "c8", "F"
    }
    dtype = np.dtype(np.complex64)
    itemsize = 8
    na_value = np.complex64("nan+nanj")
    type_def = np.complex64
    max = 2**24
    min = -2**24
    _equiv_float = "Float32Type"


@register
@generic
@subtype(ComplexType)
class Complex128Type(ComplexMixin, AtomicType):

    name = "complex128"
    aliases = {
        "complex128", "cdouble", "complex double", "complex_", "c16", "D"
    }
    dtype = np.dtype(np.complex128)
    itemsize = 16
    na_value = np.complex128("nan+nanj")
    type_def = np.complex128
    max = 2**53
    min = -2**53
    _equiv_float = "Float64Type"


@register(cond=has_clongdouble)
@generic
@subtype(ComplexType)
class Complex160Type(ComplexMixin, AtomicType):

    name = "complex160"
    aliases = {
        "complex160", "clongdouble", "clongfloat", "complex longdouble",
        "complex longfloat", "complex long double", "complex long float",
        "longcomplex", "long complex", "c20", "G"
    }
    dtype = np.dtype(np.clongdouble)
    itemsize = np.dtype(np.clongdouble).itemsize
    na_value = np.clongdouble("nan+nanj")
    type_def = np.clongdouble
    max = 2**64
    min = -2**64
    _equiv_float = "Float80Type"


#####################
####    NUMPY    ####
#####################


@register
@ComplexType.register_backend("numpy")
class NumpyComplexType(ComplexMixin, AtomicType):

    aliases = {np.complexfloating}
    dtype = np.dtype(np.complex128)
    itemsize = 16
    na_value = np.complex128("nan+nanj")
    type_def = np.complex128
    max = 2**53
    min = -2**53
    _equiv_float = "NumpyFloatType"


@register
@subtype(NumpyComplexType)
@Complex64Type.register_backend("numpy")
class NumpyComplex64Type(ComplexMixin, AtomicType):

    aliases = {np.complex64, np.dtype(np.complex64)}
    dtype = np.dtype(np.complex64)
    itemsize = 8
    na_value = np.complex64("nan+nanj")
    type_def = np.complex64
    max = 2**24
    min = -2**24
    _equiv_float = "NumpyFloat32Type"


@register
@subtype(NumpyComplexType)
@Complex128Type.register_backend("numpy")
class NumpyComplex128Type(ComplexMixin, AtomicType):

    aliases = {np.complex128, np.dtype(np.complex128)}
    dtype = np.dtype(np.complex128)
    itemsize = 16
    na_value = np.complex128("nan+nanj")
    type_def = np.complex128
    max = 2**53
    min = -2**53
    _equiv_float = "NumpyFloat64Type"


@register(cond=has_clongdouble)
@subtype(NumpyComplexType)
@Complex160Type.register_backend("numpy")
class NumpyComplex160Type(ComplexMixin, AtomicType):

    aliases = {np.clongdouble, np.dtype(np.clongdouble)}
    dtype = np.dtype(np.clongdouble)
    itemsize = np.dtype(np.clongdouble).itemsize
    na_value = np.clongdouble("nan+nanj")
    type_def = np.clongdouble
    max = 2**64
    min = -2**64
    _equiv_float = "NumpyFloat80Type"


######################
####    PYTHON    ####
######################


@register
@ComplexType.register_backend("python")
@Complex128Type.register_backend("python")
class PythonComplexType(ComplexMixin, AtomicType):

    aliases = {complex}
    itemsize = sys.getsizeof(0+0j)
    na_value = complex("nan+nanj")
    type_def = complex
    max = 2**53
    min = -2**53
    _equiv_float = "PythonFloatType"
