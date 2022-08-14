from __future__ import annotations
import decimal

import numpy as np
import pandas as pd

from ..check import (
    check_dtype, extension_type, get_dtype, is_dtype, resolve_dtype
)
from ..error import ConversionError, error_trace, shorten_list
from ..util.array import vectorize
from ..util.type_hints import array_like, dtype_like

from .helpers import (
    integral_range, _validate_dtype, _validate_errors, _validate_rounding,
    _validate_tolerance
)


# TODO: because these classes use rectify(), I might be able to add consistent
# type annotations to cython loops.
# -> if overloading works as expected, I can do this for all possible c types


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
        rounded.index = val.index  # match index
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
    # support the `out` parameter.  If `rule='half_even'` and `val` is a
    # Series, convert to numpy array and use the numpy implementation instead.
    bypass_pandas = (rule == "half_even" and isinstance(val, pd.Series))
    if bypass_pandas:
        index = val.index  # note original index
        val = val.to_numpy()

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

    # do rounding in-place
    out = val if is_array_like else None  # `out` must be array-like
    val = round_func(val, out=out)

    # undo scaling
    val /= scale_factor

    # return
    if bypass_pandas:  # restore Series
        val = pd.Series(val, copy=False)
        val.index = index  # replace index
    return val


class FloatSeries:
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

        self.series = series
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

        # hasinfs must be computed
        self._hasinfs = self.infs.any()
        return self._hasinfs

    def rectify(self, copy: bool = True) -> pd.Series:
        """Standardize element types of a float series."""
        # rectification is only needed for improperly formatted object series
        if pd.api.types.is_object_dtype(self.series):
            # get largest element type in series
            element_types = get_dtype(self.series)
            common = max(np.dtype(t) for t in vectorize(element_types))
            return self.series.astype(common, copy=copy)

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
        _validate_tolerance(tol)
        _validate_rounding(rounding)
        _validate_errors(errors)

        # rectify object series
        series = self.rectify(copy=True)

        # apply tolerance and rounding rules, if applicable
        nearest = ("half_floor", "half_ceiling", "half_down", "half_up",
                   "half_even")
        if tol and rounding not in nearest:
            series = apply_tolerance(series, tol=tol, copy=False)
        if rounding:
            series = round_float(series, rule=rounding, copy=False)

        # check for precision loss
        if ((series != 0) & (series != 1)).any():
            if errors != "coerce":
                bad_vals = series[(series != 0) & (series != 1)]
                err_msg = (f"non-boolean value encountered at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)
            series = np.ceil(series.abs().clip(0, 1))  # coerce to [0, 1]

        # return
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
        _validate_tolerance(tol)
        _validate_rounding(rounding)
        _validate_errors(errors)

        # rectify object series
        series = self.rectify(copy=True)
        coerced = False  # NAs may be introduced by coercion

        # reject any series that contains infinity
        if self.hasinfs:
            if errors != "coerce":
                bad_vals = series[self.infs]
                err_msg = (f"no integer equivalent for infinity at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)
            series[self.infs] = np.nan  # coerce
            coerced = True  # remember to convert to extension type later

        # apply tolerance and rounding rules, if applicable
        nearest = ("half_floor", "half_ceiling", "half_down", "half_up",
                   "half_even")
        if tol and rounding not in nearest:
            series = apply_tolerance(series, tol=tol, copy=False)
        if rounding:
            series = round_float(series, rule=rounding, copy=False)

        # check for precision loss
        if not (rounding or series.equals(round_float(series, "half_even"))):
            if errors != "coerce":
                bad_vals = series[(series != round_float(series)) ^ self.infs]
                err_msg = (f"precision loss detected at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)
            round_float(series, "down", copy=False)  # coerce toward zero

        # get min/max to evaluate range - longdouble maintains integer
        # precision for entire 64-bit range, prevents inconsistent comparison
        min_val = np.longdouble(series.min())
        max_val = np.longdouble(series.max())

        # built-in integer special case - can be arbitrarily large
        if is_dtype(dtype, int, exact=True):
            if min_val < -2**63 or max_val > 2**63 - 1:  # >int64
                if min_val >= 0 and max_val <= np.uint64(2**64 - 1):  # <uint64
                    dtype = pd.UInt64Dtype() if coerced else np.uint64
                    return series.astype(dtype, copy=False)
                # series is >int64 and >uint64, return as built-in python ints
                return np.frompyfunc(int, 1, 1)(series)  # as fast as cython
            # extended range isn't needed, demote to int64
            dtype = np.int64

        # check whether result fits within specified dtype
        min_poss, max_poss = integral_range(dtype)
        if min_val < min_poss or max_val > max_poss:
            if errors != "coerce":
                bad_vals = series[(series < min_poss) | (series > max_poss)]
                err_msg = (f"values exceed {dtype} range at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)
            series[(series < min_poss) | (series > max_poss)] = np.nan
            min_val = np.longdouble(series.min())
            max_val = np.longdouble(series.max())
            coerced = True  # remember to convert to extension type later

        # attempt to downcast if applicable
        if downcast:
            # get possible integer types
            if is_dtype(dtype, "unsigned"):
                int_types = [np.uint8, np.uint16, np.uint32, np.uint64]
            else:
                int_types = [np.int8, np.int16, np.int32, np.int64]
            # search for smaller dtypes that cover observed range
            for downcast_type in int_types[:int_types.index(dtype)]:
                min_poss, max_poss = integral_range(downcast_type)
                if min_val >= min_poss and max_val <= max_poss:
                    dtype = downcast_type
                    break  # stop at smallest

        # convert and return
        if coerced:  # convert to extension type early
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
        _validate_dtype(dtype, float)
        _validate_errors(errors)

        # rectify object series
        series = self.rectify(copy=True)

        # TODO: downcast should go up here to prevent unnecessary casting
        # operations

        # do naive conversion and check for precision loss/overflow afterwards
        if is_dtype(dtype, float, exact=True):  # preserve precision
            dtype = resolve_dtype(series.dtype)
        else:
            series = series.astype(dtype, copy=False)  # naive conversion
            if (series - self.series).any():  # precision loss detected
                if errors != "coerce":
                    bad_vals = series[(series != self.series)]
                    err_msg = (f"precision loss detected at index "
                               f"{shorten_list(bad_vals.index.values)}")
                    raise ConversionError(err_msg, bad_vals)
                # coerce infs to nans and ignore precision loss
                series[np.isinf(series) ^ self.infs] = np.nan

        # downcast if applicable
        if downcast:
            float_types = [np.float16, np.float32, np.float64, np.longdouble]
            for downcast_type in float_types[:float_types.index(dtype)]:
                attempt = series.astype(downcast_type, copy=False)
                if not (attempt - series).any():
                    return attempt

        # return
        return series

    def to_complex(
        self,
        dtype: dtype_like = complex,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        _validate_dtype(dtype, complex)
        _validate_errors(errors)

        # rectify object series
        series = self.rectify(copy=True)

        # TODO: downcast should go up here to prevent unnecessary casting
        # operations

        # do naive conversion and check for precision loss/overflow afterwards
        if is_dtype(dtype, complex, exact=True):  # preserve precision
            equiv_complex = {
                np.dtype(np.float16): np.complex64,
                np.dtype(np.float32): np.complex64,
                np.dtype(np.float64): np.complex128,
                np.dtype(np.longdouble): np.clongdouble
            }
            dtype = equiv_complex[series.dtype]
            series = series.astype(dtype, copy=False)
        else:
            series = series.astype(dtype, copy=False)  # naive conversion
            if (series - self.series).any():
                if errors != "coerce":
                    bad_vals = series[(series != self.series)]
                    err_msg = (f"precision loss detected at index "
                               f"{shorten_list(bad_vals.index.values)}")
                    raise ConversionError(err_msg, bad_vals)
                # coerce infs to nans and ignore precision loss
                series[np.isinf(series) ^ self.infs] += complex(np.nan, np.nan)

        # attempt to downcast, if applicable
        if downcast:
            complex_types = [np.complex64, np.complex128, np.clongdouble]
            for downcast_type in complex_types[:complex_types.index(dtype)]:
                attempt = series.astype(downcast_type, copy=False)
                if not (attempt - series).any():
                    return attempt

        # return
        return series

    def to_decimal(self) -> pd.Series:
        """test"""
        # decimal.Decimal can't parse np.longdouble by default
        series = self.rectify(copy=True)
        if is_dtype(series.dtype, np.longdouble):
            conv = lambda x: decimal.Decimal(str(x))
        else:
            conv = decimal.Decimal
        return np.frompyfunc(conv, 1, 1)(series)

    def to_string(self, dtype: dtype_like = pd.StringDtype()) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)  # TODO: erases extension type
        _validate_dtype(dtype, str)

        # TODO: consider using pyarrow string dtype to save memory

        # TODO: make this less janky
        if is_dtype(dtype, str, exact=True):
            dtype = pd.StringDtype()

        # do conversion
        return self.series.astype(dtype, copy=True)
