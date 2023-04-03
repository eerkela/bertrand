import decimal
import sys

import numpy as np
cimport numpy as np
import pandas as pd
import pytz

cimport pdcast.convert as convert
import pdcast.convert as convert
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve

from pdcast.util.error import shorten_list
from pdcast.util.round cimport Tolerance
from pdcast.util.round import round_float
from pdcast.util.time cimport Epoch
from pdcast.util.type_hints import numeric

from .base cimport AtomicType, CompositeType
from .base import dispatch, generic, register, subtype
import pdcast.types.complex as complex_types


# TODO: Add pyarrow float types?


##################################
####    MIXINS & CONSTANTS    ####
##################################


# NOTE: x86 extended precision floating point (long double) is
# platform-specific and may not be exposed depending on hardware configuration.
cdef bint has_longdouble = (np.dtype(np.longdouble).itemsize > 8)


class FloatMixin:

    ############################
    ####    TYPE METHODS    ####
    ############################

    def downcast(
        self,
        series: convert.SeriesWrapper,
        tol: Tolerance,
        smallest: CompositeType = None
    ) -> convert.SeriesWrapper:
        """Reduce the itemsize of a float type to fit the observed range."""
        # get downcast candidates
        smaller = self.smaller
        if smallest is not None:
            filtered = []
            for t in reversed(smaller):
                filtered.append(t)
                if t in smallest:
                    break  # stop at largest type contained in `smallest`
            smaller = reversed(filtered)

        for s in smaller:
            try:
                attempt = super().to_float(
                    series,
                    dtype=s,
                    tol=tol,
                    downcast=None,
                    errors="raise"
                )
            except Exception:
                # NOTE: if this method mysteriously fails to downcast for some
                # reason, check that it does not enter this block
                continue
            if attempt.within_tol(series, tol=tol.real).all():
                return attempt
        return series

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
        """Round a floating point series to the given number of decimal places
        using the specified rounding rule.
        """
        rule = convert.validate_rounding(rule)
        return convert.SeriesWrapper(
            round_float(series.rectify().series, rule=rule, decimals=decimals),
            hasnans=series.hasnans,
            element_type=series.element_type
        )

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

        rounded = self.round(series, rule="half_even")
        return convert.SeriesWrapper(
            series.series.where((
                (series.series - rounded).abs() > tol.real),
                rounded.series
            ),
            hasnans=series.hasnans,
            element_type=series.element_type
        )

    def snap_round(
        self,
        series: convert.SeriesWrapper,
        tol: numeric,
        rule: str,
        errors: str
    ) -> convert.SeriesWrapper:
        """Snap a series to the nearest integer within `tol`, and then round
        any remaining results according to the given rule.  Rejects any outputs
        that are not integer-like by the end of this process.
        """
        # apply tolerance, then check for non-integers if not rounding
        if tol or rule is None:
            rounded = self.round(series, rule="half_even")  # compute once
            outside = ~series.within_tol(rounded, tol=tol)
            if tol:
                element_type = series.element_type
                series = series.where(outside.series, rounded.series)
                series.element_type = element_type

            # check for non-integer (ignore if rounding)
            if rule is None and outside.any():
                if errors == "coerce":
                    series = self.round(series, "down")
                else:
                    raise ValueError(
                        f"precision loss exceeds tolerance {float(tol):g} at "
                        f"index {shorten_list(outside[outside].index.values)}"
                    )

        # round according to specified rule
        if rule:
            series = self.round(series, rule=rule)

        return series

    def to_boolean(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        rounding: str,
        tol: Tolerance,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert floating point data to a boolean data type."""
        series = self.snap_round(
            series,
            tol=tol.real,
            rule=rounding,
            errors=errors
        )
        series, dtype = series.boundscheck(dtype, errors=errors)
        return super().to_boolean(series, dtype=dtype, errors=errors)

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
        """Convert floating point data to an integer data type."""
        series = self.snap_round(
            series,
            tol=tol.real,
            rule=rounding,
            errors=errors
        )
        series, dtype = series.boundscheck(dtype, errors=errors)
        return super().to_integer(
            series,
            dtype=dtype,
            downcast=downcast,
            errors=errors
        )


class LongDoubleSpecialCase:
    """Special cases of the above conversions for longdouble types."""

    def to_decimal(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """A special case of FloatMixin.to_decimal() that bypasses `TypeError:
        conversion from numpy.float128 to Decimal is not supported`.
        """
        # convert longdouble to integer ratio and then to decimal
        def call(x):
            n, d = x.as_integer_ratio()
            return dtype.type_def(n) / d

        return series.apply_with_errors(
            call=call,
            errors=errors,
            element_type=dtype
        )


#######################
####    GENERIC    ####
#######################


@register
@generic
class FloatType(FloatMixin, AtomicType):
    """Generic float supertype"""

    # internal root fields - all subtypes/backends inherit these
    conversion_func = convert.to_float
    is_numeric = True

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
@generic
@subtype(FloatType)
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
@generic
@subtype(FloatType)
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
@generic
@subtype(FloatType)
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
@generic
@subtype(FloatType)
class Float80Type(LongDoubleSpecialCase, AtomicType):

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
@FloatType.register_backend("numpy")
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
@subtype(NumpyFloatType)
@Float16Type.register_backend("numpy")
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
@subtype(NumpyFloatType)
@Float32Type.register_backend("numpy")
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
@subtype(NumpyFloatType)
@Float64Type.register_backend("numpy")
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
@subtype(NumpyFloatType)
@Float80Type.register_backend("numpy")
class NumpyFloat80Type(LongDoubleSpecialCase, AtomicType):

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
@FloatType.register_backend("python")
@Float64Type.register_backend("python")
class PythonFloatType(FloatMixin, AtomicType):

    aliases = {float}
    itemsize = sys.getsizeof(0.0)
    na_value = np.nan
    type_def = float
    max = 2**53
    min = -2**53
    _equiv_complex = "PythonComplexType"
