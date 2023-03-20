import sys

import numpy as np
cimport numpy as np
import pandas as pd

import pytz

cimport pdcast.convert as convert
import pdcast.convert as convert

import pdcast.util.array as array
from pdcast.util.error import shorten_list
from pdcast.util.round cimport Tolerance
from pdcast.util.time cimport Epoch
from pdcast.util.type_hints import numeric

from .base cimport AtomicType, CompositeType
from .base import dispatch, generic, register, subtype
import pdcast.types.float as float_types


# TODO: complex160 needs to be conditional using @register syntax


##################################
####    MIXINS & CONSTANTS    ####
##################################


# NOTE: x86 extended precision float type (long double) is platform-specific
# and may not be exposed depending on hardware configuration.
cdef bint no_clongdouble = (np.dtype(np.clongdouble).itemsize <= 16)


class ComplexMixin:

    ############################
    ####    TYPE METHODS    ####
    ############################

    def downcast(
        self,
        series: convert.SeriesWrapper,
        tol: Tolerance,
        smallest: CompositeType = None
    ) -> convert.SeriesWrapper:
        """Reduce the itemsize of a complex type to fit the observed range."""
        equiv_float = self.equiv_float
        real = equiv_float.downcast(
            series.real,
            smallest=smallest,
            tol=tol
        )
        imag = equiv_float.downcast(
            series.imag,
            smallest=smallest,
            tol=Tolerance(tol.imag)
        )
        return combine_real_imag(real, imag)

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

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    @dispatch
    def round(
        self,
        series: convert.SeriesWrapper,
        decimals: int = 0,
        rule: str = "half_even"
    ) -> convert.SeriesWrapper:
        """Round a complex series to the given number of decimal places using
        the specified rounding rule.
        """
        equiv_float = self.equiv_float
        real = equiv_float.round(series.real, decimals=decimals, rule=rule)
        imag = equiv_float.round(series.imag, decimals=decimals, rule=rule)
        return combine_real_imag(real, imag)

    @dispatch
    def snap(
        self,
        series: convert.SeriesWrapper,
        tol: numeric = 1e-6
    ) -> convert.SeriesWrapper:
        """Snap each element of the series to the nearest integer if it is
        within the specified tolerance.
        """
        tol = Tolerance(tol)
        if not tol:  # trivial case, tol=0
            return series.copy()

        equiv_float = self.equiv_float
        real = equiv_float.snap(series.real, tol=tol.real)
        imag = equiv_float.snap(series.imag, tol=tol.imag)
        return combine_real_imag(real, imag)

    def to_boolean(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        rounding: str,
        tol: Tolerance,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert complex data to a boolean data type."""
        # 2-step conversion: complex -> float, float -> bool
        transfer_type = self.equiv_float
        series = self.to_float(
            series,
            dtype=transfer_type,
            tol=tol,
            downcast=None,
            errors=errors
        )
        return transfer_type.to_boolean(
            series,
            dtype=dtype,
            rounding=rounding,
            tol=tol,
            errors=errors,
            **unused
        )

    def to_integer(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        rounding: str,
        tol: Tolerance,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert complex data to an integer data type."""
        # 2-step conversion: complex -> float, float -> int
        transfer_type = self.equiv_float
        series = self.to_float(
            series,
            dtype=transfer_type,
            tol=tol,
            downcast=None,
            errors=errors
        )
        return transfer_type.to_integer(
            series,
            dtype=dtype,
            rounding=rounding,
            tol=tol,
            downcast=downcast,
            errors=errors,
            **unused
        )

    def to_float(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert complex data to a float data type."""
        transfer_type = self.equiv_float
        real = series.real

        # check for nonzero imag
        if errors != "coerce":  # ignore if coercing
            bad = ~series.imag.within_tol(0, tol=tol.imag)
            if bad.any():
                raise ValueError(
                    f"imaginary component exceeds tolerance "
                    f"{float(tol.imag):g} at index "
                    f"{shorten_list(bad[bad].index.values)}"
                )

        return transfer_type.to_float(
            real,
            dtype=dtype,
            tol=tol,
            downcast=downcast,
            errors=errors,
            **unused
        )

    def to_decimal(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert complex data to a decimal data type."""
        # 2-step conversion: complex -> float, float -> decimal
        transfer_type = self.equiv_float
        series = self.to_float(
            series,
            dtype=transfer_type,
            tol=tol,
            downcast=None,
            errors=errors
        )
        return transfer_type.to_decimal(
            series,
            dtype=dtype,
            tol=tol,
            errors=errors,
            **unused
        )

    def to_datetime(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        unit: str,
        step_size: int,
        rounding: str,
        tz: pytz.BaseTzInfo,
        since: Epoch,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert complex data to a datetime data type."""
        # 2-step conversion: complex -> float, float -> datetime
        transfer_type = self.equiv_float
        series = self.to_float(
            series,
            dtype=transfer_type,
            tol=tol,
            downcast=None,
            errors=errors
        )
        return transfer_type.to_datetime(
            series,
            dtype=dtype,
            tol=tol,
            unit=unit,
            step_size=step_size,
            rounding=rounding,
            tz=tz,
            since=since,
            errors=errors,
            **unused
        )

    def to_timedelta(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        unit: str,
        step_size: int,
        rounding: str,
        since: Epoch,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert complex data to a timedelta data type."""
        # 2-step conversion: complex -> float, float -> timedelta
        transfer_type = self.equiv_float
        series = self.to_float(
            series,
            dtype=transfer_type,
            tol=tol,
            downcast=None,
            errors=errors
        )
        return transfer_type.to_timedelta(
            series,
            dtype=dtype,
            tol=tol,
            unit=unit,
            step_size=step_size,
            rounding=rounding,
            since=since,
            errors=errors,
            **unused
        )


#######################
####    GENERIC    ####
#######################


@register
@generic
class ComplexType(ComplexMixin, AtomicType):

    # internal root fields - all subtypes/backends inherit these
    conversion_func = convert.to_complex
    _is_numeric = True

    name = "complex"
    aliases = {
        complex, "complex", "cfloat", "complex float", "complex floating", "c"
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


@register
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


@register
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

    aliases = set()
    itemsize = sys.getsizeof(0+0j)
    na_value = complex("nan+nanj")
    type_def = complex
    max = 2**53
    min = -2**53
    _equiv_float = "PythonFloatType"


#######################
####    PRIVATE    ####
#######################


cdef convert.SeriesWrapper combine_real_imag(
    convert.SeriesWrapper real,
    convert.SeriesWrapper imag
):
    """Merge separate real, imaginary components into a complex series."""
    largest = max(
        [real.element_type, imag.element_type],
        key=lambda x: x.itemsize or np.inf
    )
    target = largest.equiv_complex
    result = real + imag * 1j
    result.series = result.series.astype(target.dtype, copy=False)
    result.element_type = target
    result.hasnans = real.hasnans or imag.hasnans
    return result
