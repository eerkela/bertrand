from types import MappingProxyType

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AdapterType, AtomicType
from .base import generic, subtype
import pdtypes.types.atomic.complex as complex_types

from pdtypes.error import shorten_list
cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
from pdtypes.util.round import round_float, Tolerance


######################
####    MIXINS    ####
######################


class FloatMixin:

    conversion_func = cast.to_float

    ##############################
    ####    CUSTOMIZATIONS    ####
    ##############################

    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        rounding: str,
        tol: Tolerance,
        errors: str,
        **unused
    ) -> pd.Series:
        """Convert floating point data to an integer data type."""
        # rectify object series to prevent rounding errors
        series.series = series.rectify()

        # snap round, checking for precision loss
        if tol or rounding is None:
            rounded = series.round("half_even")
            index = ~cast.within_tolerance(series, rounded, tol=tol.real)
            if tol:
                series.series = series.where(index, rounded)
            if rounding is None and index.any():
                if errors == "coerce":
                    series.series = series.round("down")
                else:
                    raise ValueError(
                        f"precision loss detected at index "
                        f"{shorten_list(series[index].index.values)}"
                    )
        if rounding:
            series.series = series.round(rounding)

        # check for overflow
        min_val = int(series.min())
        max_val = int(series.max())
        if min_val < dtype.min or max_val > dtype.max:
            dtype = dtype.upcast(series)  # attempt to upcast
            if min_val < dtype.min or max_val > dtype.max:
                index = (series < dtype.min) | (series > dtype.max)
                if errors == "coerce":
                    series.series = series[~index]
                    series.hasnans = True
                else:
                    raise OverflowError(
                        f"values exceed {dtype} range at index "
                        f"{shorten_list(series[index].index.values)}"
                    )

        # pass to AtomicType.to_integer()
        return super().to_integer(
            series=series,
            dtype=dtype,
            errors=errors,
            **unused
        )

    ######################
    ####    EXTRAS    ####
    ######################

    def downcast(
        self,
        series: pd.Series,
        tol = 0
    ) -> pd.Series:
        """Reduce the itemsize of a float type to fit the observed range."""
        for s in self.smaller:
            attempt = series.astype(s.dtype)
            if cast.within_tolerance(attempt, series, tol=tol).all():
                return attempt
        return series

    @property
    def equiv_complex(self) -> AtomicType:
        candidates = complex_types.ComplexType.instance().subtypes
        for x in candidates:
            if type(x).__name__ == self._equiv_complex:
                return x
        raise TypeError(f"{repr(self)} has no equivalent complex type")

    def round(
        self,
        series: cast.SeriesWrapper,
        rule: str = "half_even",
        decimals: int = 0
    ) -> pd.Series:
        """Round a floating point series to the given number of decimal places
        using the specified rounding rule.
        """
        return round_float(series.rectify(), rule=rule, decimals=decimals)

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


###############################
####    FLOAT SUPERTYPE    ####
###############################


@generic
class FloatType(FloatMixin, AtomicType):

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


#######################
####    FLOAT16    ####
#######################


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


#######################
####    FLOAT32    ####
#######################


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


#######################
####    FLOAT64    ####
#######################


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


############################################
####    x86 EXTENDED PRECISION FLOAT    ####
############################################


# NOTE: this type is platform-specific and may not be exposed depending on
# hardware configuration.
cdef bint no_longdouble = (np.dtype(np.longdouble).itemsize <= 8)


@generic
@subtype(FloatType)
class Float80Type(FloatMixin, AtomicType, ignore=no_longdouble):

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


@subtype(NumpyFloatType)
@Float80Type.register_backend("numpy")
class NumpyFloat80Type(FloatMixin, AtomicType, ignore=no_longdouble):

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
