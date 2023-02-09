from types import MappingProxyType

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AdapterType, AtomicType, BaseType
from .base import dispatch, generic, subtype
import pdtypes.types.atomic.complex as complex_types

from pdtypes.error import shorten_list
from pdtypes.type_hints import numeric
cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
from pdtypes.util.round import round_float, Tolerance


##################################
####    MIXINS & CONSTANTS    ####
##################################


# NOTE: x86 extended precision float type (long double) is platform-specific
# and may not be exposed depending on hardware configuration.
cdef bint no_longdouble = (np.dtype(np.longdouble).itemsize <= 8)


class FloatMixin:

    ############################
    ####    TYPE METHODS    ####
    ############################

    @property
    def equiv_complex(self) -> AtomicType:
        candidates = complex_types.ComplexType.instance().subtypes
        for x in candidates:
            if type(x).__name__ == self._equiv_complex:
                return x
        raise TypeError(f"{repr(self)} has no equivalent complex type")

    @property
    def smaller(self) -> list:
        result = [
            x for x in self.root.subtypes if (
                x.backend == self.backend and
                x not in FloatType.backends.values()
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

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    def downcast(
        self,
        series: cast.SeriesWrapper,
        tol: Tolerance = cast.defaults.tol,
        smallest: BaseType = None
    ) -> cast.SeriesWrapper:
        """Reduce the itemsize of a float type to fit the observed range."""
        # get downcast candidates
        smaller = self.smaller
        if smallest:
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
                    downcast=False,
                    errors="raise"
                )
            except Exception:
                # NOTE: if this method mysteriously fails to downcast for some
                # reason, check that it does not enter this block
                continue
            if attempt.within_tol(series, tol=tol.real).all():
                return attempt
        return series

    @dispatch
    def round(
        self,
        series: cast.SeriesWrapper,
        rule: str = "half_even",
        decimals: int = 0
    ) -> cast.SeriesWrapper:
        """Round a floating point series to the given number of decimal places
        using the specified rounding rule.
        """
        return cast.SeriesWrapper(
            round_float(series.rectify().series, rule=rule, decimals=decimals),
            hasnans=series.hasnans,
            element_type=series.element_type
        )

    @dispatch
    def to_boolean(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        rounding: str,
        tol: Tolerance,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert floating point data to a boolean data type."""
        series = series.snap_round(tol.real, rounding, errors=errors)
        series, dtype = series.boundscheck(dtype, errors=errors)
        if series.hasnans:
            dtype = dtype.force_nullable()
        return super().to_boolean(series, dtype, errors=errors)

    @dispatch
    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        rounding: str,
        tol: Tolerance,
        downcast: bool | BaseType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert floating point data to an integer data type."""
        series = series.snap_round(tol.real, rounding, errors)
        series, dtype = series.boundscheck(dtype, errors=errors)
        return super().to_integer(
            series,
            dtype,
            downcast=downcast,
            errors=errors
        )


class LongDoubleSpecialCase:
    """Special cases of the above conversions for longdouble types."""

    @dispatch
    def to_decimal(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """A special case of FloatMixin.to_decimal() that bypasses `TypeError:
        conversion from numpy.float128 to Decimal is not supported`.
        """
        # convert to integer ratio and then to decimal
        def call(x):
            n, d = x.as_integer_ratio()
            return dtype.type_def(n) / d

        result = series.apply_with_errors(call=call, errors=errors)
        result.element_type=dtype
        return result


#######################
####    GENERIC    ####
#######################


@generic
class FloatType(FloatMixin, AtomicType):

    conversion_func = cast.to_float  # all subtypes/backend inherit this
    name = "float"
    aliases = {float, "float", "floating", "f"}
    _equiv_complex = "ComplexType"
    min = -2**53
    max = 2**53

    def __init__(self):
        super().__init__(
            type_def=float,
            dtype=np.dtype(np.float64),
            na_value=np.nan,
            itemsize=8
        )


@generic
@subtype(FloatType)
class Float16Type(FloatMixin, AtomicType):

    name = "float16"
    aliases = {"float16", "half", "f2", "e"}
    _equiv_complex = "Complex64Type"
    min = -2**11
    max = 2**11

    def __init__(self):
        super().__init__(
            type_def=np.float16,
            dtype=np.dtype(np.float16),
            na_value=np.nan,
            itemsize=2
        )


@generic
@subtype(FloatType)
class Float32Type(FloatMixin, AtomicType):

    name = "float32"
    aliases = {"float32", "single", "f4"}
    _equiv_complex = "Complex64Type"
    min = -2**24
    max = 2**24

    def __init__(self):
        super().__init__(
            type_def=np.float32,
            dtype=np.dtype(np.float32),
            na_value=np.nan,
            itemsize=4
        )


@generic
@subtype(FloatType)
class Float64Type(FloatMixin, AtomicType):

    name = "float64"
    aliases = {"float64", "double", "float_", "f8", "d"}
    _equiv_complex = "Complex128Type"
    min = -2**53
    max = 2**53

    def __init__(self):
        super().__init__(
            type_def=float,
            dtype=np.dtype(np.float64),
            na_value=np.nan,
            itemsize=8
        )


@generic
@subtype(FloatType)
class Float80Type(LongDoubleSpecialCase, AtomicType, ignore=no_longdouble):

    name = "float80"
    aliases = {
        "float80", "longdouble", "longfloat", "long double", "long float",
        "f10", "g"
    }
    _equiv_complex = "Complex160Type"
    min = -2**64
    max = 2**64

    def __init__(self):
        super().__init__(
            type_def=np.longdouble,
            dtype=np.dtype(np.longdouble),
            na_value=np.nan,
            itemsize=np.dtype(np.longdouble).itemsize
        )


#####################
####    NUMPY    ####
#####################


@FloatType.register_backend("numpy")
class NumpyFloatType(FloatMixin, AtomicType):

    aliases = {np.floating}
    _equiv_complex = "NumpyComplexType"
    min = -2**53
    max = 2**53

    def __init__(self):
        super().__init__(
            type_def=np.float64,
            dtype=np.dtype(np.float64),
            na_value=np.nan,
            itemsize=8
        )

@subtype(NumpyFloatType)
@Float16Type.register_backend("numpy")
class NumpyFloat16Type(FloatMixin, AtomicType):

    aliases = {np.float16, np.dtype(np.float16)}
    _equiv_complex = "NumpyComplex64Type"
    min = -2**11
    max = 2**11

    def __init__(self):
        super().__init__(
            type_def=np.float16,
            dtype=np.dtype(np.float16),
            na_value=np.nan,
            itemsize=2
        )


@subtype(NumpyFloatType)
@Float32Type.register_backend("numpy")
class NumpyFloat32Type(FloatMixin, AtomicType):

    aliases = {np.float32, np.dtype(np.float32)}
    _equiv_complex = "NumpyComplex64Type"
    min = -2**24
    max = 2**24

    def __init__(self):
        super().__init__(
            type_def=np.float32,
            dtype=np.dtype(np.float32),
            na_value=np.nan,
            itemsize=4
        )


@subtype(NumpyFloatType)
@Float64Type.register_backend("numpy")
class NumpyFloat64Type(FloatMixin, AtomicType):

    aliases = {np.float64, np.dtype(np.float64)}
    _equiv_complex = "NumpyComplex128Type"
    min = -2**53
    max = 2**53

    def __init__(self):
        super().__init__(
            type_def=np.float64,
            dtype=np.dtype(np.float64),
            na_value=np.nan,
            itemsize=8
        )


@subtype(NumpyFloatType)
@Float80Type.register_backend("numpy")
class NumpyFloat80Type(LongDoubleSpecialCase, AtomicType, ignore=no_longdouble):

    aliases = {np.longdouble, np.dtype(np.longdouble)}
    _equiv_complex = "NumpyComplex160Type"
    min = -2**64
    max = 2**64

    def __init__(self):
        super().__init__(
            type_def=np.longdouble,
            dtype=np.dtype(np.longdouble),
            na_value=np.nan,
            itemsize=np.dtype(np.longdouble).itemsize
        )


######################
####    PYTHON    ####
######################


@FloatType.register_backend("python")
@Float64Type.register_backend("python")
class PythonFloatType(FloatMixin, AtomicType):

    aliases = set()
    _equiv_complex = "PythonComplexType"
    min = -2**53
    max = 2**53

    def __init__(self):
        super().__init__(
            type_def=float,
            dtype=np.dtype("O"),
            na_value=np.nan,
            itemsize=8
        )
