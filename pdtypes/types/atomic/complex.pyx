import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AdapterType, AtomicType
from .base import generic, subtype
import pdtypes.types.atomic.float as float_types

from pdtypes.error import shorten_list
cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast


# TODO: consider ftol in downcast()?
# -> have to consider real/imag separately


# TODO: complex snapping needs to consider real, imag separately


######################
####    MIXINS    ####
######################


class ComplexMixin:

    def downcast(
        self,
        series: pd.Series,
        tol = 0
    ) -> AtomicType:
        """Reduce the itemsize of a complex type to fit the observed range."""
        for s in self.smaller:
            attempt = series.astype(s.dtype)
            if cast.within_tolerance(attempt, series, tol=tol):
                return s
        return self

    @property
    def equiv_float(self) -> AtomicType:
        candidates = float_types.FloatType.instance().subtypes
        for x in candidates:
            if type(x).__name__ == self._equiv_float:
                return x
        raise TypeError(f"{repr(self)} has no equivalent float type")

    def round(
        self,
        series: cast.SeriesWrapper,
        rule: str = "half_even",
        decimals: int = 0
    ) -> pd.Series:
        """Round a complex series to the given number of decimal places using
        the specified rounding rule.

        NOTE: this method rounds real and imaginary components separately.
        """
        real = series.real.round(rule=rule, decimals=decimals)
        imag = series.imag.round(rule=rule, decimals=decimals)
        return real + imag * 1j

    @property
    def smaller(self) -> list:
        result = [
            x for x in self.root.subtypes if (
                x.backend == self.backend and
                x not in ComplexType.backends.values()
            )
        ]
        if not self.is_root:
            result = [
                x for x in result if (
                    (x.itemsize or np.inf) < (self.itemsize or np.inf)
                )
            ]
        result.sort(key=lambda x: x.itemsize)
        return result


#################################
####    COMPLEX SUPERTYPE    ####
#################################


@generic
class ComplexType(ComplexMixin, AtomicType):

    name = "complex"
    aliases = {
        complex, "complex", "cfloat", "complex float", "complex floating", "c"
    }
    _equiv_float = "FloatType"

    def __init__(self):
        type_def = complex
        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.complex128),
            na_value=type_def("nan+nanj"),
            itemsize=16
        )


@ComplexType.register_backend("numpy")
class NumpyComplexType(ComplexMixin, AtomicType):

    aliases = {np.complexfloating}
    _equiv_float = "NumpyFloatType"

    def __init__(self):
        type_def = np.complex128
        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.complex128),
            na_value=type_def("nan+nanj"),
            itemsize=16
        )


#########################
####    COMPLEX64    ####
#########################


@generic
@subtype(ComplexType)
class Complex64Type(ComplexMixin, AtomicType):

    name = "complex64"
    aliases = {
        "complex64", "csingle", "complex single", "singlecomplex", "c8", "F"
    }
    _equiv_float = "Float32Type"

    def __init__(self):
        type_def = np.complex64
        self.min = type_def(-2**24)
        self.max = type_def(2**24)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.complex64),
            na_value=type_def("nan+nanj"),
            itemsize=8
        )


@subtype(NumpyComplexType)
@Complex64Type.register_backend("numpy")
class NumpyComplex64Type(ComplexMixin, AtomicType):

    aliases = {np.complex64, np.dtype(np.complex64)}
    _equiv_float = "NumpyFloat32Type"

    def __init__(self):
        type_def = np.complex64
        self.min = type_def(-2**24)
        self.max = type_def(2**24)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.complex64),
            na_value=type_def("nan+nanj"),
            itemsize=8
        )


##########################
####    COMPLEX128    ####
##########################


@generic
@subtype(ComplexType)
class Complex128Type(ComplexMixin, AtomicType):

    name = "complex128"
    aliases = {
        "complex128", "cdouble", "complex double", "complex_", "c16", "D"
    }
    _equiv_float = "Float64Type"

    def __init__(self):
        type_def = complex
        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.complex128),
            na_value=type_def("nan+nanj"),
            itemsize=16
        )


@subtype(NumpyComplexType)
@Complex128Type.register_backend("numpy")
class NumpyComplex128Type(ComplexMixin, AtomicType):

    aliases = {np.complex128, np.dtype(np.complex128)}
    _equiv_float = "NumpyFloat64Type"

    def __init__(self):
        type_def = np.complex128
        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.complex128),
            na_value=type_def("nan+nanj"),
            itemsize=16
        )


@ComplexType.register_backend("python")
@Complex128Type.register_backend("python")
class PythonComplexType(ComplexMixin, AtomicType):

    aliases = set()
    _equiv_float = "PythonFloatType"

    def __init__(self):
        type_def = complex
        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype("O"),
            na_value=type_def("nan+nanj"),
            itemsize=16
        )


##############################################
####    x86 EXTENDED PRECISION COMPLEX    ####
##############################################


cdef bint no_clongdouble = (np.dtype(np.clongdouble).itemsize <= 16)


@generic
@subtype(ComplexType)
class Complex160Type(ComplexMixin, AtomicType, ignore=no_clongdouble):

    name = "complex160"
    aliases = {
        "complex160", "clongdouble", "clongfloat", "complex longdouble",
        "complex longfloat", "complex long double", "complex long float",
        "longcomplex", "long complex", "c20", "G"
    }
    _equiv_float = "Float80Type"

    def __init__(self):
        type_def = np.clongdouble
        self.min = type_def(-2**64)
        self.max = type_def(2**64)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.clongdouble),
            na_value=type_def("nan+nanj"),
            itemsize=np.dtype(np.clongdouble).itemsize
        )



@subtype(NumpyComplexType)
@Complex160Type.register_backend("numpy")
class NumpyComplex160Type(ComplexMixin, AtomicType, ignore=no_clongdouble):

    aliases = {np.clongdouble, np.dtype(np.clongdouble)}
    _equiv_float = "NumpyFloat80Type"

    def __init__(self):
        type_def = np.clongdouble
        self.min = type_def(-2**64)
        self.max = type_def(2**64)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.clongdouble),
            na_value=type_def("nan+nanj"),
            itemsize=np.dtype(np.clongdouble).itemsize
        )
