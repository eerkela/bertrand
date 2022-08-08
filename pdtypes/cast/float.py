from __future__ import annotations
import decimal

import numpy as np
import pandas as pd

import pdtypes.cast.complex  # absolute path prevents circular ImportError
from pdtypes.cast.helpers import (
    downcast_int_dtype, integral_range, SeriesWrapper
)
from pdtypes.check import (
    check_dtype, extension_type, get_dtype, is_dtype, resolve_dtype
)
from pdtypes.error import error_trace, shorten_list
from pdtypes.util.array import vectorize
from pdtypes.util.type_hints import array_like, dtype_like


def apply_tolerance(
    val: float | np.ndarray | pd.Series,
    tol: int | float | decimal.Decimal,
    copy: bool = True
) -> pd.Series:
    """test"""
    rounded = round_float(val, "half_even", copy=True)

    # numpy array, using np.where
    if isinstance(val, np.ndarray):
        if copy:
            return np.where(np.abs(val - rounded) > tol, val, rounded)
        val[:] = np.where(np.abs(val - rounded) > tol, val, rounded)
        return val

    # pandas series, using Series.where
    if isinstance(val, pd.Series):
        if copy:
            return val.where(np.abs(val - rounded) > tol, rounded)
        val.where(np.abs(val - rounded) > tol, rounded, inplace=True)
        return val

    # scalar
    if np.abs(val - rounded) > tol:
        return val
    return rounded


def _round_up(
    val: float | np.ndarray | pd.Series,
    out: None | np.ndarray | pd.Series = None
) -> float | np.ndarray | pd.Series:
    sign = np.sign(val)
    result = np.ceil(np.abs(val, out=out), out=out)
    result *= sign
    return result


def _round_half_down(
    val: float | np.ndarray | pd.Series,
    out: None | np.ndarray | pd.Series = None
) -> float | np.ndarray | pd.Series:
    sign = np.sign(val)
    result = np.ceil(np.abs(val, out=out) - 0.5, out=out)
    result *= sign
    return result


def _round_half_floor(
    val: float | np.ndarray | pd.Series,
    out: None | np.ndarray | pd.Series = None
) -> float | np.ndarray | pd.Series:
    return np.ceil(val - 0.5, out=out)


def _round_half_ceiling(
    val: float | np.ndarray | pd.Series,
    out: None | np.ndarray | pd.Series = None
) -> float | np.ndarray | pd.Series:
    return np.floor(val + 0.5, out=out)


def _round_half_up(
    val: float | np.ndarray | pd.Series,
    out: None | np.ndarray | pd.Series = None
) -> float | np.ndarray | pd.Series:
    sign = np.sign(val)
    result = np.floor(np.abs(val, out=out) + 0.5, out=out)
    result *= sign
    return result


def round_float(
    val: float | np.ndarray | pd.Series,
    rule: str = "half_even",
    decimals: int = 0,
    copy: bool = True
) -> float | np.ndarray | pd.Series:
    """test"""
    is_array_like = isinstance(val, (np.ndarray, pd.Series))

    # optimization: hidden mutability.  A copy is (or is not) generated in this
    # first scaling step.  All other operations are done in-place.
    scale_factor = 10**decimals
    if not copy and is_array_like:
        val *= scale_factor  # no copy
    else:
        val = val * scale_factor  # implicit copy

    # special case: pandas implementation of round() (=="half_even") does not
    # support `out` parameter.  If `rule='half_even'` and `val` is a Series,
    # convert to numpy array and use the numpy implementation instead.
    bypass_pandas = (rule == "half_even" and isinstance(val, pd.Series))
    if bypass_pandas:
        val = np.array(val, copy=False)

    # select rounding strategy
    switch = {  # C-style switch statement
        "floor": np.floor,
        "ceiling": np.ceil,
        "down": np.trunc,
        "up": _round_up,
        "half_floor": _round_half_floor,
        "half_ceiling": _round_half_ceiling,
        "half_down": _round_half_down,
        "half_up": _round_half_up,
        "half_even": np.round
    }
    round_func = switch[rule]

    # do rounding in-place, undo scaling, and return
    out = val if is_array_like else None  # `out` must be array-like
    val = round_func(val, out=out)
    val /= scale_factor
    if bypass_pandas:  # restore Series
        return pd.Series(val, copy=False)
    return val


class FloatSeries(SeriesWrapper):
    """test"""

    def __init__(
        self,
        series: float | array_like,
        nans: None | bool | array_like = None,
        validate: bool = True
    ) -> FloatSeries:
        if validate and not check_dtype(series, float):
            err_msg = (f"[{error_trace()}] `series` must contain float data, "
                       f"not {get_dtype(series)}")
            raise TypeError(err_msg)

        super().__init__(series, nans)
        self._infs = None
        self._hasinfs = None

    @property
    def infs(self) -> pd.Series:
        """test"""
        if self._infs is not None:  # infs is cached
            return self._infs

        # infs must be computed
        self._infs = np.isinf(self.rectify(copy=True))
        self._hasinfs = self._infs.any()
        return self._infs

    @property
    def hasinfs(self) -> bool:
        """test"""
        if self._hasinfs is not None:  # hasinfs is cached
            return self._hasinfs

        if self._infs is not None:  # infs is cached, but hasinfs isn't
            self._hasinfs = self._infs.any()
            return self._hasinfs

        # infs and hasinfs must be computed
        self._infs = np.isinf(self.rectify(copy=True))
        self._hasinfs = self._infs.any()
        return self._hasinfs

    def rectify(self, copy: bool = True) -> pd.Series:
        """Standardize element types of a float series."""
        # rectification is only needed for improperly formatted object series
        if pd.api.types.is_object_dtype(self.series):
            # get largest element type in series
            element_types = get_dtype(self.series)
            common = max(np.dtype(t) for t in vectorize(element_types))

            # if returning a copy, use series.where(..., inplace=False)
            if copy:
                series = self.series.where(~self.is_na, np.nan)
                return series.astype(common, copy=False)

            # modify in-place and return result
            self.series.where(~self.is_na, np.nan, inplace=True)
            self.series = self.series.astype(common, copy=False)
            return self.series

        # series is already rectified, return a copy or direct reference
        return self.series.copy() if copy else self.series

    def round(
        self,
        rule: str = "half_even",
        decimals: int = 0,
        copy: bool = True
    ) -> pd.Series:
        """test"""
        # TODO: this can be attached directly to pd.Series
        series = self.rectify(copy=copy)
        return round_float(series, rule=rule, decimals=decimals, copy=copy)

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
        series = self.rectify(copy=True)
        if rounding in {"half_floor", "half_ceiling", "half_down", "half_up",
                        "half_even"}:
            series = round_float(series, rule=rounding, copy=False)
        else:
            if tol:
                series = apply_tolerance(series, tol=tol, copy=False)
            if rounding:
                series = round_float(series, rule=rounding, copy=False)

        # check for precision loss
        if not series.isin((0, 1, np.nan)).all():
            if errors == "raise":
                bad = series[~series.isin((0, 1, np.nan))].index.values
                err_msg = (f"[{error_trace()}] non-boolean value encountered "
                           f"at index {shorten_list(bad)}")
                raise ValueError(err_msg)
            if errors == "ignore":
                return self.series
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
        series = self.rectify(copy=True)
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

        # apply tolerance and round if applicable
        if rounding in {"half_floor", "half_ceiling", "half_down", "half_up",
                        "half_even"}:
            series = round_float(series, rule=rounding, copy=False)
        else:
            if tol:
                series = apply_tolerance(series, tol=tol, copy=False)
            if rounding:
                series = round_float(series, rule=rounding, copy=False)

        # check for precision loss
        if not (rounding or series.equals(round_float(series, "half_even"))):
            if errors == "raise":
                rounded = round_float(series, "half_even")
                bad = series[(series != rounded) ^ self.is_na].index.values
                err_msg = (f"[{error_trace()}] precision loss detected at "
                           f"index {shorten_list(bad)}")
                raise ValueError(err_msg)
            if errors == "ignore":
                return self.series
            round_float(series, "down", copy=False)  # coerce toward zero

        # get min/max to evaluate range - longdouble maintains integer
        # precision for entire 64-bit range, prevents inconsistent comparison
        min_val = np.longdouble(series.min())
        max_val = np.longdouble(series.max())

        # built-in integer special case - can be arbitrarily large
        if is_dtype(dtype, int, exact=True):
            if min_val < -2**63 or max_val > 2**63 - 1:  # >int64
                if min_val >= 0 and max_val <= np.uint64(2**64 - 1):  # <uint64
                    dtype = pd.UInt64Dtype() if hasnans else np.uint64
                    return series.astype(dtype, copy=False)
                # series is >int64 and >uint64, return as built-in python ints
                with self.exclude_na(pd.NA) as ctx:
                    ctx.subset = np.frompyfunc(int, 1, 1)(ctx.subset)
                return ctx.result
            dtype = np.int64  # extended range isn't needed, demote to int64

        # check whether result fits within specified dtype
        min_poss, max_poss = integral_range(dtype)
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
        return series.astype(dtype, copy=False)

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
        series = self.rectify(copy=True)
        if is_dtype(dtype, float, exact=True):  # preserve precision
            dtype = resolve_dtype(series.dtype)
        else:
            series = series.astype(dtype, copy=False)
            if (series - self.series).any():  # precision loss detected
                if errors == "ignore":
                    return self.series
                if errors == "raise":
                    bad = series[(series != self.series) ^
                                 self.is_na].index.values
                    err_msg = (f"[{error_trace()}] precision loss detected at "
                               f"index {shorten_list(bad)}")
                    raise OverflowError(err_msg)
                # coerce infs introduced by coercion into nans
                series[np.isinf(series) ^ self.infs] = np.nan

        # downcast if applicable
        if downcast:
            float_types = [np.float16, np.float32, np.float64, np.longdouble]
            smaller = float_types[:float_types.index(dtype)]
            for downcast_type in smaller:
                try:
                    return self.to_float(dtype=downcast_type)
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

        # TODO: nans are not properly cast to nan+nanj

        series = self.rectify(copy=True)
        if is_dtype(dtype, complex, exact=True):  # preserve precision
            equiv_complex = {
                np.dtype(np.float16): np.complex64,
                np.dtype(np.float32): np.complex64,
                np.dtype(np.float64): np.complex128,
                np.dtype(np.longdouble): np.clongdouble
            }
            series = series.astype(equiv_complex[series.dtype], copy=False)
        else:
            series = series.astype(dtype, copy=False)
            if (series - self.series).any():
                if errors == "ignore":
                    return self.series
                if errors == "raise":
                    bad = series[(series != self.series) ^
                                 self.is_na].index.values
                    err_msg = (f"[{error_trace()}] precision loss detected at "
                               f"index {shorten_list(bad)}")
                    raise OverflowError(err_msg)
                # coerce infs introduced by coercion into nans
                complex_nan = dtype("nan+nanj")
                series[np.isinf(series) ^ self.infs] = complex_nan

        # return
        if downcast:
            # TODO: pass nans to ComplexSeries
            series = pdtypes.cast.complex.ComplexSeries(series, validate=False)
            return series.to_complex(downcast=True)
        return series

    def to_decimal(self) -> pd.Series:
        """test"""
        with self.exclude_na(pd.NA) as ctx:
            # decimal.Decimal can't parse numpy floats by default
            conv = lambda x: decimal.Decimal(str(x))
            ctx.subset = np.frompyfunc(conv, 1, 1)(ctx.subset)
        return ctx.result

    def to_string(self, dtype: dtype_like = pd.StringDtype()) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, str)

        # TODO: replace extension type support to resolve_dtype.
        # TODO: consider using pyarrow string dtype to save memory.

        if self.hasnans:
            dtype = pd.StringDtype()
        return self.series.astype(dtype, copy=True)
