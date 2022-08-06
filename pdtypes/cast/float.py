from __future__ import annotations
import decimal

import numpy as np
import pandas as pd

from pdtypes.cast.helpers import (
    downcast_int_dtype, int_dtype_range, SeriesWrapper
)
from pdtypes.check import (
    check_dtype, extension_type, get_dtype, is_dtype, resolve_dtype
)
from pdtypes.error import error_trace, shorten_list
from pdtypes.util.array import vectorize
from pdtypes.util.type_hints import array_like, dtype_like


def round_float(
    val: float | array_like,
    rounding: str
) -> float | np.ndarray | pd.Series:
    """test"""
    switch = {  # C-style switch statement with lazy evaluation
        "floor": lambda: np.floor(val),
        "ceiling": lambda: np.ceil(val),
        "down": lambda: np.trunc(val),
        "up": lambda: np.sign(val) * np.ceil(np.abs(val)),
        "half_floor": lambda: np.ceil(val - 0.5),
        "half_ceiling": lambda: np.floor(val + 0.5),
        "half_down": lambda: np.sign(val) * np.ceil(np.abs(val) - 0.5),
        "half_up": lambda: np.sign(val) * np.floor(np.abs(val) + 0.5),
        "half_even": lambda: np.round(val)
    }
    return switch[rounding]()


def round_within_tol(
    series: pd.Series,
    rounding: str | None,
    tol: int | float | decimal.Decimal
) -> pd.Series:
    """test"""
    # if rounding to nearest, don't bother computing tolerance
    if rounding in ("half_floor", "half_ceiling", "half_down", "half_up",
                    "half_even"):
        return round_float(series, rounding=rounding)

    # apply tolerance if given
    if tol:
        series = series.copy()  # prevent in-place modification
        rounded = round_float(series, rounding="half_even")
        within_tol = np.abs(series - rounded) <= tol
        series[within_tol] = rounded[within_tol]

    # apply rounding rule if given and skip checking precision loss
    if rounding:
        return round_float(series, rounding=rounding)

    # return
    return series


class FloatSeries(SeriesWrapper):
    """test"""

    def __init__(
        self,
        series: float | array_like,
        validate: bool = True
    ) -> FloatSeries:
        if validate and not check_dtype(series, float):
            err_msg = (f"[{error_trace()}] `series` must contain float data, "
                       f"not {get_dtype(series)}")
            raise TypeError(err_msg)

        super().__init__(series)
        self.series = FloatSeries.rectify(self.series, copy=True)
        self._infs = None
        self._hasinfs = None

    @property
    def infs(self) -> pd.Series:
        """test"""
        if self._infs is not None:
            return self._infs
        self._infs = np.isinf(self.series)
        self._hasinfs = self._infs.any()
        return self._infs

    @property
    def hasinfs(self) -> bool:
        """test"""
        # _hasinfs is already cached
        if self._hasinfs is not None:
            return self._hasinfs

        # _infs is cached, but _hasinfs has not been computed (shouldn't happen)
        if self._infs is not None:
            self._hasinfs = self._infs.any()
            return self._hasinfs

        # _infs and _hasinfs must be computed
        self.infs
        return self._hasinfs

    @staticmethod
    def rectify(
        series: pd.Series | np.ndarray,
        copy: bool = True
    ) -> pd.Series | np.ndarray:
        """Standardize element types of a float series."""
        if pd.api.types.is_object_dtype(series):
            # standardize as float series with dtype = maximum element size
            standard = max(np.dtype(t) for t in vectorize(get_dtype(series)))
            return series.astype(standard, copy=copy)
        return series

    def to_boolean(
        self,
        tol: int | float | decimal.Decimal = 1e-6,
        rounding: None | str = None,
        dtype: dtype_like = bool,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_tolerance(tol)
        SeriesWrapper._validate_rounding(rounding)
        SeriesWrapper._validate_errors(errors)

        # rectify object series, apply tolerance, and round if applicable
        # series = FloatSeries.rectify(self.series, copy=True)
        series = round_within_tol(self.series, tol=tol, rounding=rounding)

        # check for precision loss
        if not series.dropna().isin((0, 1)).all():
            if errors == "raise":
                bad = series[~series.isin((0, 1)) ^ ~self.not_na].index.values
                err_msg = (f"[{error_trace()}] non-boolean value encountered "
                           f"at index {shorten_list(bad)}")
                raise ValueError(err_msg)
            if errors == "ignore":
                return series
            return np.ceil(series.abs().clip(0, 1))  # coerce to [0, 1, np.nan]

        # return
        if self.hasnans:
            dtype = pd.BooleanDtype()
        return series.astype(dtype, copy=False)

    def to_integer(
        self,
        tol: int | float | decimal.Decimal = 1e-6,
        rounding: None | str = None,
        dtype: dtype_like = int,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_tolerance(tol)
        SeriesWrapper._validate_rounding(rounding)
        SeriesWrapper._validate_errors(errors)

        # rectify object series
        # series = FloatSeries.rectify(self.series, copy=True)
        series = self.series.copy()
        hasnans = self.hasnans

        # reject any series that contains infinity
        if self.hasinfs:
            if errors == "raise":
                bad = series[self.infs].index.values
                err_msg = (f"[{error_trace()}] no integer equivalent for "
                           f"infinity at index {shorten_list(bad)}")
                raise OverflowError(err_msg)
            if errors == "ignore":
                return self.series
            series[self.infs] = np.nan  # coerce
            hasnans = True

        # apply tolerance and round, if applicable
        series = round_within_tol(series, tol=tol, rounding=rounding)

        # check for precision loss
        if not (rounding or series.equals(round_float(series, "half_even"))):
            if errors == "raise":
                rounded = round_float(series, "half_even")
                bad = series[(series != rounded) ^ ~self.not_na].index.values
                err_msg = (f"[{error_trace()}] precision loss detected at "
                           f"index {shorten_list(bad)}")
                raise ValueError(err_msg)
            if errors == "ignore":
                return series
            return round_float(series, rounding="down")  # coerce toward zero

        # get min/max to evaluate range - longdouble maintains integer
        # precision for entire 64-bit range, prevents inconsistent comparison
        min_val = np.longdouble(series.min())
        max_val = np.longdouble(series.max())

        # built-in integer special case - can be arbitrarily large
        if is_dtype(dtype, int, exact=True):

            # check if series exceeds int64 range
            if min_val < -2**63 or max_val > 2**63 - 1:

                # check if series fits within uint64 range
                if min_val >= 0 and max_val <= np.uint64(2**64 - 1):
                    dtype = pd.UInt64Dtype() if hasnans else np.uint64
                    return series.astype(dtype, copy=False)

                # series is >int64 and >uint64, return as built-in python ints
                with self.exclude_na(pd.NA) as ctx:
                    ctx.subset = np.frompyfunc(int, 1, 1)(ctx.subset)
                return ctx.result

            # python extended integer range isn't needed, reduce to int64
            dtype = np.int64

        # check whether result fits within specified dtype
        min_poss, max_poss = int_dtype_range(dtype)
        if min_val < min_poss or max_val > max_poss:
            if errors == "ignore":
                return self.series
            indices = (series < min_poss) | (series > max_poss)
            if errors == "raise":
                bad = series[indices].index.values
                err_msg = (f"[{error_trace()}] values exceed {dtype} range at "
                           f"index {shorten_list(bad)}")
                raise OverflowError(err_msg)
            if indices.any():
                series[indices] = np.nan
                min_val = np.longdouble(series.min())
                max_val = np.longdouble(series.max())
                hasnans = True

        # attempt to downcast if applicable
        if downcast:
            dtype = downcast_int_dtype(dtype, min_val, max_val)

        # convert and return
        if hasnans:
            dtype = extension_type(dtype)
        return series.astype(dtype)

    def to_float(
        self,
        dtype: dtype_like = float,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, float)
        SeriesWrapper._validate_errors(errors)

        # do conversion
        if is_dtype(dtype, float, exact=True):  # preserve precision
            # series = FloatSeries.rectify(self.series, copy=True)
            series = self.series.copy()
            dtype = resolve_dtype(series.dtype)
        else:
            series = self.series.astype(dtype, copy=True)
            if (series - self.series).any():  # precision loss detected
                if errors == "ignore":
                    return self.series
                if errors == "raise":
                    indices = (series != self.series) ^ ~self.not_na
                    bad = series[indices].index.values
                    err_msg = (f"[{error_trace()}] precision loss detected at "
                               f"index {shorten_list(bad)}")
                    raise OverflowError(err_msg)
                # coerce infs introduced by coercion into nans
                series[np.isinf(series) ^ self.infs] = np.nan

        # downcast if applicable
        if downcast:
            float_types = [np.float16, np.float32, np.float64, np.longdouble]
            smaller = float_types[:float_types.index(dtype)]
            for dtype in smaller:
                try:
                    return self.to_float(dtype=dtype)
                except OverflowError:
                    pass
        return series

    def to_complex(
        self,
        dtype: dtype_like = complex,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, complex)
        SeriesWrapper._validate_errors(errors)

        if is_dtype(dtype, complex, exact=True):  # preserve precision
            # series = FloatSeries.rectify(self.series, copy=True)
            series = self.series.copy()
            equiv_complex = {
                np.dtype(np.float16): np.complex64,
                np.dtype(np.float32): np.complex64,
                np.dtype(np.float64): np.complex128,
                np.dtype(np.longdouble): np.clongdouble
            }
            series = series.astype(equiv_complex[series.dtype], copy=False)
        else:
            series = self.series.astype(dtype, copy=True)
            if (series - self.series).any():
                if errors == "ignore":
                    return self.series
                if errors == "raise":
                    indices = (series != self.series) ^ ~self.not_na
                    bad = series[indices].index.values
                    err_msg = (f"[{error_trace()}] precision loss detected at "
                               f"index {shorten_list(bad)}")
                    raise OverflowError(err_msg)
                # coerce infs introduced by coercion into nans
                series[np.isinf(series) ^ self.infs] = np.nan

        # return
        if downcast:
            return _downcast_complex_series(series)
        return series

    def to_decimal(self) -> pd.Series:
        """test"""
        with self.exclude_na(pd.NA) as ctx:
            # decimal.Decimal can't parse numpy ints in object array
            # ctx.subset = FloatSeries.rectify(ctx.subset, copy=False)
            ctx.subset = np.frompyfunc(decimal.Decimal, 1, 1)(ctx.subset)
        return ctx.result
