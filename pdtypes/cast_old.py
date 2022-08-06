from __future__ import annotations
import datetime
import decimal
import functools
import sys
from types import TracebackType
from typing import Any
import warnings
import zoneinfo

import dateutil
import numba
import numpy as np
import pandas as pd
import pytz
import tzlocal

from pdtypes.error import error_trace
from pdtypes.check import check_dtype, extension_type, get_dtype, is_dtype, resolve_dtype
from pdtypes.util.array import vectorize, broadcast_args
from pdtypes.util.time import _to_ns
from pdtypes.util.type_hints import (
    array_like, dtype_like, datetime_like, timedelta_like, scalar
)

# TODO: memory management in pandas/numpy
# https://pythonspeed.com/datascience/#memory
# -> make every conversion in-place with hidden mutability

# TODO: look into replacing np.frompyfunc loops with numba njit/vectorize
# https://towardsdatascience.com/supercharging-numpy-with-numba-77ed5b169240

# TODO: rename .coerce_dtypes() to .transmute()?

# TODO: when dispatching, use sparse and categorical boolean flags.  If the
# provided dtype is SparseDtype(), set sparse=True and convert to base type.
# If dtype is pd.CategoricalDtype() with or without `categories`, `ordered`,
# then set categorical=True and convert to base type.  If none is provided,
# use current element type.

# TODO: change all rounding rules to accept [floor, ceiling, down, up,
# half_floor, half_ceiling, half_down, half_up]

# TODO: support pyarrow string dtype if pyarrow is installed
# https://pythonspeed.com/articles/pandas-string-dtype-memory/


#############################################
####    `series` Validation Functions    ####
#############################################


def _validate_boolean_series(series: bool | array_like) -> None:
    """Raise a TypeError if `series` does not contain boolean data."""
    if not check_dtype(series, bool):
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"boolean data, not {get_dtype(series)}")
        raise TypeError(err_msg)


def _validate_integer_series(series: int | array_like) -> None:
    """Raise a TypeError if `series` does not contain integer data."""
    if not check_dtype(series, int):
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"integer data, not {get_dtype(series)}")
        raise TypeError(err_msg)


def _validate_float_series(series: float | array_like) -> None:
    """Raise a TypeError if `series` does not contain float data."""
    if not check_dtype(series, float):
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"float data, not {get_dtype(series)}")
        raise TypeError(err_msg)


def _validate_complex_series(series: complex | array_like) -> None:
    """Raise a TypeError if `series` does not contain complex data."""
    if not check_dtype(series, complex):
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"complex data, not {get_dtype(series)}")
        raise TypeError(err_msg)


def _validate_decimal_series(series: decimal.Decimal | array_like) -> None:
    """Raise a TypeError if `series` does not contain decimal data."""
    if not check_dtype(series, "decimal"):
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"decimal data, not {get_dtype(series)}")
        raise TypeError(err_msg)


def _validate_datetime_series(series: datetime_like | array_like) -> None:
    """Raise a TypeError if `series` does not contain datetime data."""
    if not check_dtype(series, "datetime"):
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"datetime data, not {get_dtype(series)}")
        raise TypeError(err_msg)


def _validate_timedelta_series(series: timedelta_like | array_like) -> None:
    """Raise a TypeError if `series` does not contain timedelta data."""
    if not check_dtype(series, "timedelta"):
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"timedelta data, not {get_dtype(series)}")
        raise TypeError(err_msg)


def _validate_string_series(series: str | array_like) -> None:
    if not check_dtype(series, str):
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"string data, not {get_dtype(series)}")
        raise TypeError(err_msg)


############################################
####    `dtype` Validation Functions    ####
############################################


def _validate_dtype_is_scalar(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is a sequence of any length."""
    if np.array(dtype).shape:
        err_msg = (f"[{error_trace()}] `dtype` must be a single atomic type, "
                   f"not a sequence {repr(dtype)}")
        raise TypeError(err_msg)


def _validate_boolean_dtype(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is not a valid boolean dtype."""
    if not is_dtype(dtype, bool):
        err_msg = (f"[{error_trace()}] `dtype` must be bool-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


def _validate_integer_dtype(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is not a valid integer dtype."""
    if not is_dtype(dtype, int):
        err_msg = (f"[{error_trace()}] `dtype` must be int-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


def _validate_float_dtype(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is not a valid float dtype."""
    if not is_dtype(dtype, float):
        err_msg = (f"[{error_trace()}] `dtype` must be float-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


def _validate_complex_dtype(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is not a valid complex dtype."""
    if not is_dtype(dtype, complex):
        err_msg = (f"[{error_trace()}] `dtype` must be complex-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


def _validate_decimal_dtype(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is not a valid decimal dtype."""
    if not is_dtype(dtype, "decimal"):
        err_msg = (f"[{error_trace()}] `dtype` must be decimal-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


def _validate_datetime_dtype(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is not a valid datetime dtype."""
    if not is_dtype(dtype, "datetime"):
        err_msg = (f"[{error_trace()}] `dtype` must be datetime-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


def _validate_timedelta_dtype(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is not a valid timedelta dtype."""
    if not is_dtype(dtype, "timedelta"):
        err_msg = (f"[{error_trace()}] `dtype` must be timedelta-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


def _validate_string_dtype(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is not a valid string dtype."""
    if not is_dtype(dtype, str):
        err_msg = (f"[{error_trace()}] `dtype` must be string-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


##################################################
####    Miscellaneous Validation Functions    ####
##################################################


def _validate_format(
    format: None | str,
    day_first: bool,
    year_first: bool
) -> None:
    """Raise a TypeError if `format` is not None or a string, and a
    RuntimeError if it is given and `day_first` or `year_first` are not False.
    """
    # check format is a string or None
    if format is not None:
        if day_first or year_first:
            err_msg = (f"[{error_trace(stack_index=2)}] `day_first` and "
                       f"`year_first` should not be used when `format` is "
                       f"given.")
            raise RuntimeError(err_msg)
        if not isinstance(format, str):
            err_msg = (f"[{error_trace(stack_index=2)}] `format` must be a "
                       f"datetime format string or None, not {type(format)}")
            raise TypeError(err_msg)


def _validate_unit(unit: str | np.ndarray | pd.Series) -> None:
    """Efficiently check whether an array of units is valid."""
    valid = list(_to_ns) + ["M", "Y"]
    if not np.isin(unit, valid).all():
        bad = list(np.unique(unit[~np.isin(unit, valid)]))
        err_msg = (f"[{error_trace(stack_index=2)}] `unit` "
                   f"{_shorten_list(bad)} not recognized: must be in "
                   f"{valid}")
        raise ValueError(err_msg)


def _validate_tolerance(tol: int | float | decimal.Decimal) -> None:
    """Raise a TypeError if `tol` isn't a real numeric, and a ValueError if it
    is not between 0 and 0.5.
    """
    if not isinstance(tol, (int, float, decimal.Decimal)):
        err_msg = (f"[{error_trace(stack_index=2)}] `tol` must be a real "
                   f"numeric between 0 and 0.5, not {type(tol)}")
        raise TypeError(err_msg)
    if not 0 <= tol <= 0.5:
        err_msg = (f"[{error_trace(stack_index=2)}] `tol` must be a real "
                   f"numeric between 0 and 0.5, not {tol}")
        raise ValueError(err_msg)


def _validate_rounding(rounding: str | None) -> None:
    """Raise a TypeError if `tol` isn't None or a string, and a ValueError if
    it is not one of the accepted rounding rules ('floor', 'ceiling',
    'truncate', 'infinity', 'nearest')
    """
    if rounding is None:
        return None
    valid_rules = ("floor", "ceiling", "down", "up", "half_floor",
                   "half_ceiling", "half_down", "half_up", "half_even")
    if not isinstance(rounding, str):
        err_msg = (f"[{error_trace(stack_index=2)}] `rounding` must be a "
                   f"string {valid_rules}, not {type(rounding)}")
        raise TypeError(err_msg)
    if rounding not in valid_rules:
        err_msg = (f"[{error_trace(stack_index=2)}] `rounding` must be one of "
                   f"{valid_rules}, not {repr(rounding)}")
        raise ValueError(err_msg)
    return None


def _validate_errors(errors: str) -> None:
    """Raise  a TypeError if `errors` isn't a string, and a ValueError if it is
    not one of the accepted error-handling rules ('raise', 'coerce', 'ignore')
    """
    valid_errors = ("raise", "coerce", "ignore")
    if not isinstance(errors, str):
        err_msg = (f"[{error_trace(stack_index=2)}] `errors` must be a string "
                   f"{valid_errors}, not {type(errors)}")
        raise TypeError(err_msg)
    if errors not in valid_errors:
        err_msg = (f"[{error_trace(stack_index=2)}] `errors` must be one of "
                   f"{valid_errors}, not {repr(errors)}")
        raise ValueError(err_msg)


#################################
####    Utility Functions    ####
#################################


def _downcast_complex_series(
    series: pd.Series,
    real: None | np.ndarray = None,
    imag: None | np.ndarray = None
) -> pd.Series:
    """Attempt to downcast a complex series to the smallest possible complex
    dtype that can fully represent its values, without losing any precision.

    Note: this implementation is fully vectorized (and therefore fast), but
    may exceed memory requirements for very large data.
    """
    real = _downcast_float_series(np.real(series) if real is None else real)
    imag = _downcast_float_series(np.imag(series) if imag is None else imag)
    equivalent_complex_dtype = {
        np.dtype(np.float16): np.complex64,
        np.dtype(np.float32): np.complex64,
        np.dtype(np.float64): np.complex128,
        np.dtype(np.longdouble): np.clongdouble
    }
    most_general = equivalent_complex_dtype[max(real.dtype, imag.dtype)]
    return series.astype(most_general)


def _downcast_float_series(series: pd.Series) -> pd.Series:
    """Attempt to downcast a float series to the smallest possible float dtype
    that can fully represent its values, without losing any precision.

    Note: this implementation is fully vectorized (and therefore fast), but
    may exceed memory requirements for very large data (~10**8 rows).
    """
    dtype = series.dtype
    if np.issubdtype(dtype, np.float16):  # can't downcast past float16
        return series

    # get array elements as raw bits (exluding zeros, nans, infs)
    exclude = np.isnan(series) | np.isin(series, (0.0, -np.inf, np.inf))
    bits = np.array(series[~exclude])
    bits = bits.view(np.uint8).reshape(-1, dtype.itemsize)
    if sys.byteorder == "little":  # ensure big-endian byte order
        bits = bits[:,::-1]
    bits = np.unpackbits(bits, axis=1)

    # decompose bits into unbiased exponent + mantissa
    exp_length = {4: 8, 8: 11, 12: 15, 16: 15}[dtype.itemsize]
    exp_coefs = 2**np.arange(exp_length - 1, -1, -1)
    exp_bias = 2**(exp_length - 1) - 1
    if bits.shape[1] > 64:  # longdouble: only the last 80 bits are relevant
        # longdouble includes an explicit integer bit not present in other
        # IEEE 754 floating point standards, which we exclude from mantissa
        # construction.  This bit can be 0, but only in extreme edge cases
        # involving the Intel 8087/80287 platform or an intentional denormal
        # representation.  This should never occur in practice.
        start = bits.shape[1] - 79
        exponent = np.dot(bits[:,start:start+exp_length], exp_coefs) - exp_bias
        mantissa = bits[:, start+exp_length+1:]
    else:  # np.float32, np.float64
        exponent = np.dot(bits[:,1:1+exp_length], exp_coefs) - exp_bias
        mantissa = bits[:,1+exp_length:]

    # define search types and associated limits
    widths = {  # dtype: (min exp, max exp, mantissa width)
        np.dtype(np.float16): (-2**4 + 2, 2**4 - 1, 10),
        np.dtype(np.float32): (-2**7 + 2, 2**7 - 1, 23),
        np.dtype(np.float64): (-2**10 + 2, 2**10 - 1, 52),
        np.dtype(np.longdouble): (-2**14 + 2, 2**14 - 1, 63),
    }
    # filter `widths` to only include dtypes that are smaller than original
    widths = {k: widths[k] for k in list(widths)[:list(widths).index(dtype)]}

    # search for smaller dtypes that can fully represent series without
    # incurring precision loss
    for float_dtype, (min_exp, max_exp, man_width) in widths.items():
        if (((min_exp <= exponent) & (exponent <= max_exp)).all() and
            not np.sum(mantissa[:,man_width:])):
            return series.astype(float_dtype)
    return series


def _downcast_int_dtype(
    dtype: np.dtype | pd.api.extensions.ExtensionDtype,
    min_val: int | float | complex | decimal.Decimal,
    max_val: int | float | complex | decimal.Decimal
) -> np.dtype | pd.api.extensions.ExtensionDtype:
    dtype = pd.api.types.pandas_dtype(dtype)
    if dtype.itemsize == 1:
        return dtype

    # get type hierarchy
    if pd.api.types.is_extension_array_dtype(dtype):
        if pd.api.types.is_unsigned_integer_dtype(dtype):
            type_hierarchy = {
                8: pd.UInt64Dtype(),
                4: pd.UInt32Dtype(),
                2: pd.UInt16Dtype(),
                1: pd.UInt8Dtype()
            }
        else:
            type_hierarchy = {
                8: pd.Int64Dtype(),
                4: pd.Int32Dtype(),
                2: pd.Int16Dtype(),
                1: pd.Int8Dtype()
            }
    else:
        if pd.api.types.is_unsigned_integer_dtype(dtype):
            type_hierarchy = {
                8: np.dtype(np.uint64),
                4: np.dtype(np.uint32),
                2: np.dtype(np.uint16),
                1: np.dtype(np.uint8)
            }
        else:
            type_hierarchy = {
                8: np.dtype(np.int64),
                4: np.dtype(np.int32),
                2: np.dtype(np.int16),
                1: np.dtype(np.int8)
            }

    # check for smaller dtypes that fit given range
    size = dtype.itemsize
    selected = dtype
    while size > 1:
        test = type_hierarchy[size // 2]
        size = test.itemsize
        if pd.api.types.is_unsigned_integer_dtype(test):
            min_poss = 0
            max_poss = 2**(8 * test.itemsize) - 1
        else:
            min_poss = -2**(8 * test.itemsize - 1)
            max_poss = 2**(8 * test.itemsize - 1) - 1
        if min_val >= min_poss and max_val <= max_poss:
            selected = test
    return selected


def _int_dtype_range(dtype: dtype_like) -> tuple[int, int]:
    # convert to pandas dtype to expose .itemsize attribute
    dtype = pd.api.types.pandas_dtype(dtype)
    bit_size = 8 * dtype.itemsize
    if pd.api.types.is_unsigned_integer_dtype(dtype):
        return (0, 2**bit_size - 1)
    return (-2**(bit_size - 1), 2**(bit_size - 1) - 1)


def _parse_timezone(tz: str | datetime.tzinfo | None) -> datetime.tzinfo | None:
    # validate timezone and convert to datetime.tzinfo object
    if isinstance(tz, str):
        if tz == "local":
            return pytz.timezone(tzlocal.get_localzone_name())
        return pytz.timezone(tz)
    if tz and not isinstance(tz, pytz.BaseTzInfo):
        err_msg = (f"[{error_trace(stack_index=2)}] `tz` must be a "
                   f"pytz.timezone object or an IANA-recognized timezone "
                   f"string, not {type(tz)}")
        raise TypeError(err_msg)
    return tz


def _round_float(
    num: float | np.ndarray | pd.Series,
    rounding: None | str
) -> float | np.ndarray | pd.Series:
    switch = {  # C-style switch statement with lazy evaluation
        "floor": lambda: np.floor(num),
        "ceiling": lambda: np.ceil(num),
        "down": lambda: np.trunc(num),
        "up": lambda: np.sign(num) * np.ceil(np.abs(num)),
        "half_floor": lambda: np.ceil(num - 0.5),
        "half_ceiling": lambda: np.floor(num + 0.5),
        "half_down": lambda: np.sign(num) * np.ceil(np.abs(num) - 0.5),
        "half_up": lambda: np.sign(num) * np.floor(np.abs(num) + 0.5),
        "half_even": lambda: np.round(num)
    }
    return switch[rounding]()


def _round_decimal(
    dec: decimal.Decimal | np.ndarray | pd.Series,
    rounding: str
) -> decimal.Decimal | np.ndarray | pd.Series:
    if rounding == "down":
        return dec // 1
    switch = {  # C-style switch statement
        "floor": lambda x: x.quantize(1, decimal.ROUND_FLOOR),
        "ceiling": lambda x: x.quantize(1, decimal.ROUND_CEILING),
        "up": lambda x: x.quantize(1, decimal.ROUND_UP),
        "half_floor": (lambda x: x.quantize(1, decimal.ROUND_HALF_UP)
                                 if x < 0 else
                                 x.quantize(1, decimal.ROUND_HALF_DOWN)),
        "half_ceiling": (lambda x: x.quantize(1, decimal.ROUND_HALF_DOWN)
                                   if x < 0 else
                                   x.quantize(1, decimal.ROUND_HALF_UP)),
        "half_down": lambda x: x.quantize(1, decimal.ROUND_HALF_DOWN),
        "half_up": lambda x: x.quantize(1, decimal.ROUND_HALF_UP),
        "half_even": lambda x: x.quantize(1, decimal.ROUND_HALF_EVEN)
    }
    return np.frompyfunc(switch[rounding], 1, 1)(dec)


def _round_within_tol(
    series: pd.Series,
    tol: int | float | decimal.Decimal,
    rounding: None | str,
    errors: None | str,
    is_decimal: bool,
    to_boolean: bool
) -> pd.Series:
    round_func = _round_decimal if is_decimal else _round_float

    # if rounding to nearest, don't bother computing tolerance
    if rounding in ("half_floor", "half_ceiling", "half_down", "half_up",
                    "half_even"):
        return round_func(series, rounding=rounding)

    # apply tolerance if given
    if tol:
        rounded = round_func(series, rounding="half_even")
        within_tol = (series - rounded).abs() <= tol
        series[within_tol] = rounded[within_tol]

    # apply rounding rule if given and skip checking precision loss
    if rounding:
        return round_func(series, rounding=rounding)

    # check for precision loss (target = boolean)
    if to_boolean:
        if not series.dropna().isin((0, 1)).all():
            if errors == "raise":
                bad = series[~series.isin((0, 1)) ^ series.isna()].index.values
                err_msg = (f"[{error_trace(stack_index=2)}] non-boolean value "
                           f"encountered at index {_shorten_list(bad)}")
                raise ValueError(err_msg)
            if errors == "ignore":
                return series
            return np.ceil(series.abs().clip(0, 1))  # coerce to [0, 1, np.nan]

    # check for precision loss (target = float/decimal)
    else:
        if "rounded" not in locals():  # might be defined from `tol`
            rounded = round_func(series, rounding="half_even")
        if not series.equals(rounded):
            if errors == "raise":
                bad = series[(series != rounded) ^ series.isna()].index.values
                err_msg = (f"[{error_trace(stack_index=2)}] precision loss "
                           f"detected at index {_shorten_list(bad)}")
                raise ValueError(err_msg)
            if errors == "ignore":
                return series
            return round_func(series, rounding="down")  # coerce toward zero

    # return
    return series


def _shorten_list(list_like: array_like, max_length: int = 5) -> str:
    if len(list_like) <= max_length:
        return str(list(list_like))
    shortened = ", ".join(str(i) for i in list_like[:max_length])
    return f"[{shortened}, ...] ({len(list_like)})"


class ExcludeMissing:
    """Context manager that subsets an input series to exclude missing values.
    Operations can then be performed on the subset, which are written back to
    the input series when the context manager falls out of scope.  Missing
    values are replaced with the given fill_value.
    """

    def __init__(
        self,
        series: pd.Series,
        fill_value: Any,
        dtype: dtype_like = "O"
    ) -> ExcludeMissing:
        self.not_na = series.notna()
        self.subset = series[self.not_na]
        self.fill_value = fill_value
        self.dtype = dtype
        self.hasnans = (~self.not_na).any()
        self.result = None

    def __enter__(self) -> ExcludeMissing:
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        exc_tb: TracebackType | None
    ) -> None:
        if exc_value:  # exception occurred in body of with statement
            return False  # propagate exception without evaluating result

        self.result = np.full(self.not_na.shape, self.fill_value, self.dtype)
        self.result = pd.Series(self.result)
        self.result[self.not_na] = self.subset
        return False


#######################
####    Boolean    ####
#######################


def boolean_to_boolean(
    series: bool | array_like,
    dtype: dtype_like = bool,
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a boolean series to another boolean dtype."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_boolean_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_boolean_dtype(dtype)

    # do conversion
    if series.hasnans:
        return series.astype(pd.BooleanDtype())
    return series.astype(dtype)


def boolean_to_integer(
    series: bool | array_like,
    dtype: dtype_like = int,
    downcast: bool = False,
    *,
    validate: bool = True
) -> pd.Series:
    """Convert boolean series to integer"""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_boolean_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_integer_dtype(dtype)

    # if series has missing values, convert to extension types
    hasnans = series.hasnans
    if hasnans and pd.api.types.is_object_dtype(series):
        series = series.astype(pd.BooleanDtype())

    # if `downcast=True`, return as 8-bit integer dtype
    if downcast:
        if pd.api.types.is_unsigned_integer_dtype(dtype):
            if hasnans:
                return series.astype(extension_type(np.uint8))
            return series.astype(np.uint8)
        if hasnans:
            return series.astype(extension_type(np.int8))
        return series.astype(np.int8)

    # no extension type for built-in python integer
    if is_dtype(dtype, int, exact=True):
        dtype = np.int64
    if hasnans:
        dtype = extension_type(dtype)
    return series.astype(dtype)


def boolean_to_float(
    series: bool | array_like,
    dtype: dtype_like = float,
    downcast: bool = False,
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a boolean series to a float series."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_boolean_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_float_dtype(dtype)

    # if `downcast=True`, return as float16
    if downcast:
        return series.astype(np.float16)

    # return as specified dtype
    return series.astype(dtype)


def boolean_to_complex(
    series: bool | array_like,
    dtype: dtype_like = complex,
    downcast: bool = False,
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a boolean series to a complex series."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_boolean_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_complex_dtype(dtype)

    # astype() is unstable when converting missing values to complex types
    dtype = np.complex64 if downcast else dtype
    with ExcludeMissing(series, None, dtype) as ctx:
        ctx.subset = ctx.subset.astype(dtype, copy=False)
    return ctx.result


def boolean_to_decimal(
    series: bool | array_like,
    dtype: dtype_like = decimal.Decimal,
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a boolean series to a decimal series."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_boolean_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_decimal_dtype(dtype)

    # subsetting is marginally faster than backfilling with pd.NA
    with ExcludeMissing(series, pd.NA) as ctx:
        ctx.subset += decimal.Decimal(0)
    return ctx.result


# def boolean_to_datetime(
#     series: bool | array_like,
#     unit: str | array_like = "ns",
#     since: str | datetime_like | array_like = "1970-01-01 00:00:00+0000",
#     tz: str | datetime.tzinfo = "local",
#     dtype: dtype_like = "datetime"
# ) -> pd.Series:
#     """Convert a boolean series to datetimes, where True represents one `unit`
#     after `since`.  This is mostly provided for completeness and reversability
#     reasons.
#     """
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_boolean_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_datetime_dtype(dtype)

#     raise NotImplementedError()


# def boolean_to_timedelta(
#     series: bool | array_like,
#     unit: str = "ns",
#     since: str | timedelta_like = "00:00:00",
#     dtype: dtype_like = "timedelta"
# ) -> pd.Series:
#     """Convert a boolean series to timedeltas, where True represents one `unit`
#     after `since`.  This is mostly provided for completeness and reversability
#     reasons.
#     """
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_boolean_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_timedelta_dtype(dtype)

#     raise NotImplementedError()


def boolean_to_string(
    series: bool | array_like,
    dtype: dtype_like = pd.StringDtype(),
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a boolean series into strings `('True', 'False')`.  This is
    mostly provided for completeness and reversability reasons.
    """
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_boolean_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_string_dtype(dtype)

    # use pandas string extension type, if applicable
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)


#######################
####    Integer    ####
#######################


def integer_to_boolean(
    series: int | array_like,
    dtype: dtype_like = bool,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert an integer series to their boolean equivalents."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_integer_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_boolean_dtype(dtype)
    _validate_errors(errors)

    # check for information loss and apply error handling rule
    if series.min() < 0 or series.max() > 1:
        if errors == "raise":
            bad = series[(series < 0) | (series > 1)].index.values
            err_msg = (f"[{error_trace()}] non-boolean value encountered at "
                       f"index {_shorten_list(bad)}")
            raise ValueError(err_msg)
        if errors == "ignore":
            return series
        series = series.fillna(pd.NA).abs().clip(0, 1)

    # convert to final result
    if series.hasnans:
        return series.astype(pd.BooleanDtype())
    return series.astype(dtype)


def integer_to_integer(
    series: int | array_like,
    dtype: dtype_like = int,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert an integer series to another integer dtype."""
    # vectorize input
    series = pd.Series(vectorize(series), copy=True)

    # validate input
    if validate:
        _validate_integer_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_integer_dtype(dtype)
    _validate_errors(errors)

    # get min/max to evaluate range
    min_val = series.min()
    max_val = series.max()

    # built-in integer special case - can be arbitrarily large
    if ((min_val < -2**63 or max_val > 2**63 - 1) and
        is_dtype(dtype, int, exact=True)):
        # > int64 but < uint64
        if min_val >= 0 and max_val <= 2**64 - 1:
            dtype = pd.UInt64Dtype() if series.hasnans else np.uint64
            return series.astype(dtype, copy=False)
        # > int64 and > uint64, return as built-in python ints
        return series.astype("O", copy=False).fillna(pd.NA, inplace=True)

    # check whether result fits within available range for specified `dtype`
    min_poss, max_poss = _int_dtype_range(dtype)
    if min_val < min_poss or max_val > max_poss:
        if errors == "ignore":
            return series
        indices = (series < min_poss) | (series > max_poss)
        if errors == "raise":
            bad = series[indices].index.values
            err_msg = (f"[{error_trace()}] values exceed {dtype} range at "
                       f"index {_shorten_list(bad)}")
            raise OverflowError(err_msg)
        series[indices] = pd.NA  # TODO: modifying in-place
        min_val = series.min()
        max_val = series.max()

    # attempt to downcast if applicable
    if downcast:
        dtype = _downcast_int_dtype(dtype, min_val, max_val)

    # convert and return
    if series.hasnans:
        dtype = extension_type(dtype)
    return series.astype(dtype, copy=False)


def integer_to_float(
    series: int | array_like,
    dtype: dtype_like = float,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert an integer series into floats of the given dtype.

    By default, this function does not check for precision loss, only
    infinities that are introduced during coercion, which signal overflow.
    If exact equality between the floats returned by this function and the
    original integer input is desired, pair this with a `float_to_integer`
    conversion and compare.
    """
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_integer_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_float_dtype(dtype)
    _validate_errors(errors)

    # do conversion
    result = series.astype(dtype)

    # check for infinities introduced by coercion
    overflow = np.isinf(result)
    if overflow.any():
        if errors == "raise":
            bad = result[overflow].index.values
            err_msg = (f"[{error_trace()}] values exceed {dtype} range at "
                       f"index {_shorten_list(bad)}")
            raise OverflowError(err_msg)
        if errors == "ignore":
            return series
        result[overflow] = np.nan

    # return
    if downcast:
        return _downcast_float_series(result)
    return result


def integer_to_complex(
    series: int | array_like,
    dtype: dtype_like = complex,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert an integer series into complex numbers of the given dtype."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_integer_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_complex_dtype(dtype)
    _validate_errors(errors)

    # do conversion
    result = series.astype(dtype)
    real = np.real(result)
    imag = np.imag(result)

    # check for infinities introduced by coercion
    overflow = np.isinf(real) | np.isinf(imag)
    if overflow.any():
        if errors == "raise":
            bad = result[overflow].index.values
            err_msg = (f"[{error_trace()}] values exceed {dtype} range at "
                       f"index {_shorten_list(bad)}")
            raise OverflowError(err_msg)
        if errors == "ignore":
            return series
        result[overflow] = complex(np.nan, np.nan)

    # return
    if downcast:
        return _downcast_complex_series(result, real, imag)
    return result


def integer_to_decimal(
    series: int | array_like,
    dtype: dtype_like = decimal.Decimal,
    *,
    validate: bool = True
) -> pd.Series:
    """Convert an integer series into decimal."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_integer_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_decimal_dtype(dtype)

    # decimal.Decimal can't parse numpy integers stored in an object array
    if (pd.api.types.is_object_dtype(series) and
        not check_dtype(series, int, exact=True)):
        # cast to a string transfer format before converting to decimal
        series = integer_to_string(series, dtype=str)

    # subset to avoid missing values
    with ExcludeMissing(series, pd.NA) as ctx:
        ctx.subset = np.frompyfunc(decimal.Decimal, 1, 1)(ctx.subset)
    return ctx.result


# def integer_to_datetime(
#     series: int | array_like,
#     unit: str | array_like = "ns",
#     since: str | datetime_like | array_like = "1970-01-01 00:00:00+0000",
#     tz: str | datetime.tzinfo = "local",
#     dtype: dtype_like = "datetime"
# ) -> pd.Series:
#     """Convert an integer series into datetime."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_integer_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_datetime_dtype(dtype)

#     raise NotImplementedError()


# def integer_to_timedelta(
#     series: int | array_like,
#     unit: str | array_like = "ns",
#     since: str | timedelta_like | array_like = "00:00:00",
#     dtype: dtype_like = "timedelta"
# ) -> pd.Series:
#     """Convert an integer series to timedelta."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_integer_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_timedelta_dtype(dtype)

#     raise NotImplementedError()


def integer_to_string(
    series: int | array_like,
    dtype: dtype_like = pd.StringDtype(),
    *,
    validate: bool = True
) -> pd.Series:
    """Convert an integer series to strings."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_integer_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_string_dtype(dtype)

    # do conversion
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)


#####################
####    Float    ####
#####################


def float_to_boolean(
    series: float | array_like,
    tol: int | float | decimal.Decimal = 1e-6,
    rounding: str | None = None,
    dtype: dtype_like = bool,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a float series to booleans, using the specified rounding rule
    and floating point tolerance.
    """
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_float_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_boolean_dtype(dtype)
    _validate_tolerance(tol)
    _validate_rounding(rounding)
    _validate_errors(errors)

    # python floats have no callable rint method, but are identical to float64
    if pd.api.types.is_object_dtype(series):
        result = series.astype(np.float64)
    else:
        result = series.copy()

    # round if applicable
    result = _round_within_tol(result, tol=tol, rounding=rounding,
                               errors=errors, is_decimal=False, to_boolean=True)

    # return
    if result.hasnans:
        return result.astype(pd.BooleanDtype(), copy=False)
    return result.astype(dtype, copy=False)


def float_to_integer(
    series: float | array_like,
    tol: int | float | decimal.Decimal = 1e-6,
    rounding: str | None = None,
    dtype: dtype_like = int,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a float series to integers, using the specified rounding rule
    and floating point tolerance.
    """
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_float_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_integer_dtype(dtype)
    _validate_tolerance(tol)
    _validate_rounding(rounding)
    _validate_errors(errors)

    # python floats have no callable rint method, but are identical to float64
    if pd.api.types.is_object_dtype(series):
        result = series.astype(np.float64)
    else:
        result = series.copy()

    # reject any series that contains infinity
    if np.isinf(result).any():
        if errors == "raise":
            bad = result[np.isinf(result)].index.values
            err_msg = (f"[{error_trace()}] no integer equivalent for infinity "
                       f"at index {_shorten_list(bad)}")
            raise OverflowError(err_msg)
        if errors == "ignore":
            return series
        result[np.isinf(result)] = np.nan  # coerce

    # apply floating point tolerance + rounding rule
    result = _round_within_tol(result, tol=tol, rounding=rounding,
                               errors=errors, is_decimal=False)

    # get min/max to evaluate range - longdouble maintains integer precision
    # for entire 64-bit range, prevents inconsistent comparison
    min_val = np.longdouble(result.min())
    max_val = np.longdouble(result.max())

    # built-in integer special case - can be arbitrarily large
    if ((min_val < -2**63 or max_val > 2**63 - 1) and
        is_dtype(dtype, int, exact=True)):
        # longdouble can't be compared to extended python integer (> 2**63 - 1)
        if min_val >= 0 and max_val < np.uint64(2**64 - 1):  # > i8 but < u8
            dtype = pd.UInt64Dtype() if result.hasnans else np.uint64
            return result.astype(dtype)
        with ExcludeMissing(result, pd.NA) as ctx:
            ctx.subset = np.frompyfunc(int, 1, 1)(ctx.subset)
        return ctx.result

    # check whether result fits within specified dtype
    min_poss, max_poss = _int_dtype_range(dtype)
    if min_val < min_poss or max_val > max_poss:
        if errors == "ignore":
            return series
        indices = (result < min_poss) | (result > max_poss)
        if errors == "raise":
            bad = result[indices].index.values
            err_msg = (f"[{error_trace()}] values exceed {dtype} range at index "
                       f"{_shorten_list(bad)}")
            raise OverflowError(err_msg)
        if indices.any():
            result[indices] = np.nan
            min_val = result.min()
            max_val = result.max()

    # attempt to downcast if applicable
    if downcast:
        dtype = _downcast_int_dtype(dtype, min_val, max_val)

    # convert and return
    if result.hasnans:
        dtype = extension_type(dtype)
    return result.astype(dtype)


def float_to_float(
    series: float | array_like,
    dtype: dtype_like = float,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a float series into another float dtype."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_float_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_float_dtype(dtype)
    _validate_errors(errors)

    # do conversion
    if is_dtype(dtype, float, exact=True):  # preserve precision
        # if no .dtype field, get dtype of largest element in series
        if pd.api.types.is_object_dtype(series):
            dtype = max(np.dtype(d) for d in vectorize(get_dtype(series)))
            result = series.astype(dtype)
        else:
            result = series
    else:  # convert and check for precision loss
        result = series.astype(dtype)
        if (result - series).any():
            if errors == "ignore":
                return series
            if errors == "raise":
                bad = result[(result != series) ^ result.isna()].index.values
                err_msg = (f"[{error_trace()}] precision loss detected at "
                           f"index {_shorten_list(bad)}")
                raise OverflowError(err_msg)
            # coerce infs introduced by coercion into nans
            result[np.isinf(series) ^ np.isinf(result)] = np.nan

    # return
    if downcast:
        result = _downcast_float_series(result)
    return result


def float_to_complex(
    series: float | array_like,
    dtype: dtype_like = complex,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a float series into complex numbers."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_float_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_complex_dtype(dtype)
    _validate_errors(errors)

    # do conversion
    result = series.astype(dtype)

    # check for precision loss
    if (result - series).any():
        if errors == "raise":
            bad = result[(result - series) != 0].index.values
            err_msg = (f"[{error_trace()}] precision loss detected at index "
                       f"{_shorten_list(bad)}")
            raise OverflowError(err_msg)
        if errors == "ignore":
            return series
        result[np.isinf(result) ^ np.isinf(series)] = complex(np.nan, np.nan)

    # return
    if downcast:
        result = _downcast_complex_series(result)
    return result


def float_to_decimal(
    series: float | array_like,
    dtype: dtype_like = decimal.Decimal,
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a float series into decimals."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_float_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_decimal_dtype(dtype)

    # decimal.Decimal can't parse numpy floats in an object array.  Cast these
    # to a string transfer format before decimal conversion.
    if (pd.api.types.is_object_dtype(series) and
        not check_dtype(series, float, exact=True)):
        series = float_to_string(series, dtype=str).astype("O", copy=False)

    # convert non-missing values
    with ExcludeMissing(series, pd.NA) as ctx:
        ctx.subset = np.frompyfunc(decimal.Decimal, 1, 1)(ctx.subset)
    return ctx.result


# def float_to_datetime(
#     series: float | array_like,
#     unit: str | array_like = "ns",
#     since: str | datetime_like | array_like = "1970-01-01 00:00:00+0000",
#     tz: str | datetime.tzinfo = "local",
#     dtype: dtype_like = "datetime"
# ) -> pd.Series:
#     """Convert a float series to datetimes."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_float_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_boolean_dtype(dtype)

#     raise NotImplementedError()


# def float_to_timedelta(
#     series: float | array_like,
#     unit: str | array_like = "ns",
#     since: str | timedelta_like | array_like = "00:00:00",
#     dtype: dtype_like = "timedelta"
# ) -> pd.Series:
#     """Convert a float series to timedeltas."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_float_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_timedelta_dtype(dtype)

#     raise NotImplementedError()


def float_to_string(
    series: float | array_like,
    dtype: dtype_like = pd.StringDtype(),
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a float series to string."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_float_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_string_dtype(dtype)

    # do conversion
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)


#######################
####    Complex    ####
#######################


def complex_to_boolean(
    series: complex | array_like,
    tol: complex = 1e-6,
    rounding: str | None = None,
    dtype: dtype_like = bool,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a complex series to booleans, using the specified rounding rule
    and floating point tolerance.
    """
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_complex_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_boolean_dtype(dtype)
    _validate_tolerance(tol)
    _validate_rounding(rounding)
    _validate_errors(errors)

    # 2 steps: complex -> float, then float -> boolean
    series = complex_to_float(series, tol=tol, errors=errors, validate=False)
    return float_to_boolean(series, tol=tol, rounding=rounding, dtype=dtype,
                            errors=errors, validate=False)


def complex_to_integer(
    series: complex | array_like,
    tol: complex = 1e-6,
    rounding: str | None = None,
    dtype: dtype_like = int,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a complex series to integers, using the specified rounding rule
    and floating point tolerance.
    """
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_complex_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_integer_dtype(dtype)
    _validate_rounding(rounding)
    _validate_tolerance(tol)
    _validate_errors(errors)

    # 2 steps: complex -> float, then float -> integer
    series = complex_to_float(series, tol=tol, errors=errors, validate=False)
    return float_to_integer(series, tol=tol, rounding=rounding, dtype=dtype,
                            downcast=downcast, errors=errors, validate=False)


def complex_to_float(
    series: complex | array_like,
    tol: int | float | decimal.Decimal = 1e-6,
    dtype: dtype_like = float,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a complex series into floats."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_complex_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_float_dtype(dtype)
    _validate_tolerance(tol)
    _validate_errors(errors)

    # np.real & np.imag aren't stable for object-typed arrays
    if pd.api.types.is_object_dtype(series):
        # find the largest dtype present in array
        largest = np.max([np.dtype(x) for x in vectorize(get_dtype(series))])
        result = series.astype(largest)
    else:
        result = series.copy()

    # split result into real and imaginary components
    real = pd.Series(np.real(result))
    imag = np.imag(result)

    # check `imag` for information loss
    if (np.abs(imag) > tol).any():
        if errors == "raise":
            bad = result[np.abs(imag) > tol].index.values
            err_msg = (f"[{error_trace()}] imaginary component exceeds "
                       f"tolerance ({tol}) at index {_shorten_list(bad)}")
            raise ValueError(err_msg)
        if errors == "ignore":
            return series

    # return using float_to_float
    return float_to_float(real, dtype=dtype, downcast=downcast, errors=errors,
                          validate=False)


def complex_to_complex(
    series: complex | array_like,
    dtype: dtype_like = complex,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a complex series into another complex dtype."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_complex_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_complex_dtype(dtype)
    _validate_errors(errors)

    # do conversion
    if is_dtype(dtype, complex, exact=True):  # preserve precision
        # if no .dtype field, get dtype of largest element in series
        if pd.api.types.is_object_dtype(series):
            dtype = max(np.dtype(d) for d in vectorize(get_dtype(series)))
            result = series.astype(dtype)
        else:
            result = series
    else:  # convert and check for precision loss
        result = series.astype(dtype)
        if (result - series).any():
            if errors == "raise":
                bad = result[(result - series) != 0].index.values
                err_msg = (f"[{error_trace()}] precision loss detected at "
                           f"index {_shorten_list(bad)}")
                raise OverflowError(err_msg)
            if errors == "ignore":
                return series
            complex_na = complex(np.nan, np.nan)
            result[np.isinf(result) ^ np.isinf(series)] = complex_na

    # return
    if downcast:
        result = _downcast_complex_series(result)
    return result


def complex_to_decimal(
    series: complex | array_like,
    tol: int | float | decimal.Decimal = 1e-6,
    dtype: dtype_like = decimal.Decimal,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a complex series into decimals."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_complex_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_decimal_dtype(dtype)
    _validate_tolerance(tol)
    _validate_errors(errors)

    # 2 steps: complex -> float, then float -> decimal
    series = complex_to_float(series, tol=tol, errors=errors, validate=False)
    return float_to_decimal(series, dtype=dtype, validate=False)


# def complex_to_datetime(
#     series: complex | array_like,
#     unit: str | array_like = "ns",
#     since: str | datetime_like | array_like = "1970-01-01 00:00:00+0000",
#     tz: str | datetime.tzinfo = "local",
#     dtype: dtype_like = "datetime"
# ) -> pd.Series:
#     """Convert a complex series to datetimes."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_complex_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_boolean_dtype(dtype)

#     raise NotImplementedError()


# def complex_to_timedelta(
#     series: complex | array_like,
#     unit: str | array_like = "ns",
#     since: str | timedelta_like | array_like = "00:00:00",
#     dtype: dtype_like = "timedelta"
# ) -> pd.Series:
#     """Convert a complex series to timedeltas."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_complex_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_timedelta_dtype(dtype)

#     raise NotImplementedError()


def complex_to_string(
    series: complex | array_like,
    dtype: dtype_like = pd.StringDtype(),
    *,
    validate: bool
) -> pd.Series:
    """Convert a complex series to string."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_complex_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_string_dtype(dtype)

    # do conversion
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)


#######################
####    Decimal    ####
#######################


def decimal_to_boolean(
    series: decimal.Decimal | array_like,
    tol: int | float | decimal.Decimal = 0,
    rounding: None | str = None,
    dtype: dtype_like = bool,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a decimal series to booleans, using the specified rounding rule.
    """
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_decimal_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_boolean_dtype(dtype)
    _validate_tolerance(tol)
    _validate_rounding(rounding)
    _validate_errors(errors)

    # subset to avoid missing values
    with ExcludeMissing(series, pd.NA) as ctx:
        ctx.subset = _round_within_tol(ctx.subset, tol=tol, rounding=rounding,
                                       errors=errors, is_decimal=True,
                                       to_boolean=True)

        # check for information loss
        if not ctx.subset.isin((0, 1)).all():
            if errors == "raise":
                bad = ctx.subset[~ctx.subset.isin((0, 1))].index.values
                err_msg = (f"[{error_trace()}] non-boolean value encountered "
                           f"at index {_shorten_list(bad)}")
                raise ValueError(err_msg)
            if errors == "ignore" and not ctx.subset.isin((0, 1)).all():
                return series
            # coerce subset to [0, 1] bool flags
            ctx.subset = np.ceil(ctx.subset.abs().clip(0, 1))

        # convert subset to bool
        ctx.subset = ctx.subset.astype(bool, copy=False)

    # change dtype of result to match content
    if ctx.hasnans:
        return ctx.result.astype(pd.BooleanDtype(), copy=False)
    return ctx.result.astype(dtype, copy=False)


def decimal_to_integer(
    series: decimal.Decimal | array_like,
    tol: int | float | decimal.Decimal = 0,
    rounding: None | str = None,
    dtype: dtype_like = int,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a decimal series to integers, using the specified rounding rule.
    """
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_decimal_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_integer_dtype(dtype)
    _validate_tolerance(tol)
    _validate_rounding(rounding)
    _validate_errors(errors)

    # subset to avoid missing values
    with ExcludeMissing(series, pd.NA) as ctx:
        ctx.subset = _round_within_tol(ctx.subset, tol=tol, rounding=rounding,
                                       errors=errors, is_decimal=True)
        ctx.subset = np.frompyfunc(int, 1, 1)(ctx.subset)

    # pass through integer_to_integer to sort out dtype, downcast args
    return integer_to_integer(ctx.result, dtype=dtype, downcast=downcast,
                              errors=errors, validate=False)


def decimal_to_float(
    series: decimal.Decimal | array_like,
    dtype: dtype_like = float,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a decimal series into floats."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_decimal_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_float_dtype(dtype)
    _validate_errors(errors)

    # do conversion, then convert back to decimal to allow precision loss check
    result = series.astype(dtype)

    # check for precision loss
    new_infs = np.isinf(result) ^ np.isinf(series)
    if new_infs.any():
        if errors == "raise":
            bad = result[new_infs].index.values
            err_msg = (f"[{error_trace()}] values exceed {dtype} range at index "
                    f"{_shorten_list(bad)}")
            raise OverflowError(err_msg)
        if errors == "ignore" and new_infs.any():
            return series
        result[new_infs] = np.nan

    # return
    if downcast:
        result = _downcast_float_series(result)
    return result


def decimal_to_complex(
    series: decimal.Decimal | array_like,
    dtype: dtype_like = complex,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a decimal series into another complex dtype."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_decimal_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_complex_dtype(dtype)
    _validate_errors(errors)

    # do conversion, then convert back to decimal to allow precision loss check
    result = series.astype(dtype)

    # check for precision loss
    new_infs = np.isinf(result) ^ np.isinf(series)
    if new_infs.any():
        if errors == "raise":
            bad = result[new_infs].index.values
            err_msg = (f"[{error_trace()}] values exceed {dtype} range at "
                       f"index {_shorten_list(bad)}")
            raise OverflowError(err_msg)
        if errors == "ignore" and new_infs.any():
            return series
        result[new_infs] = complex(np.nan, np.nan)

    # return
    if downcast:
        result = _downcast_complex_series(result)
    return result


def decimal_to_decimal(
    series: decimal.Decimal | array_like,
    dtype: dtype_like = decimal.Decimal,
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a decimal series into decimals."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_decimal_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_decimal_dtype(dtype)

    # currently, decimal.Decimal is the only decimal subclass that is allowed
    return series.fillna(pd.NA)


# def decimal_to_datetime(
#     series: decimal.Decimal | array_like,
#     unit: str | array_like = "ns",
#     since: str | datetime_like | array_like = "1970-01-01 00:00:00+0000",
#     tz: str | datetime.tzinfo = "local",
#     dtype: dtype_like = "datetime"
# ) -> pd.Series:
#     """Convert a decimal series to datetimes."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_decimal_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_boolean_dtype(dtype)

#     raise NotImplementedError()


# def decimal_to_timedelta(
#     series: decimal.Decimal | array_like,
#     unit: str | array_like = "ns",
#     since: str | timedelta_like | array_like = "00:00:00",
#     dtype: dtype_like = "timedelta"
# ) -> pd.Series:
#     """Convert a decimal series to timedeltas."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_decimal_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_timedelta_dtype(dtype)

#     raise NotImplementedError()


def decimal_to_string(
    series: decimal.Decimal | array_like,
    dtype: dtype_like = pd.StringDtype(),
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a decimal series to string."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_decimal_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_string_dtype(dtype)

    # do conversion
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)


########################
####    Datetime    ####
########################




#########################
####    Timedelta    ####
#########################



######################
####    String    ####
######################


def string_to_integer(
    series: str | array_like,
    dtype: dtype_like = int,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a string series into integers."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_string_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_integer_dtype(dtype)
    _validate_errors(errors)

    # for each element, attempt integer coercion and note errors
    def transcribe(element: str) -> tuple[int, bool]:
        try:
            return (int(element.replace(" ", "")), False)
        except ValueError:
            return (pd.NA, True)

    # subset to avoid missing values
    with ExcludeMissing(series, pd.NA) as ctx:
        ctx.subset, invalid = np.frompyfunc(transcribe, 1, 2)(ctx.subset)
        if invalid.any():
            if errors == "raise":
                bad = ctx.subset[invalid].index.values
                err_msg = (f"[{error_trace()}] non-integer values detected at "
                           f"index {_shorten_list(bad)}")
                raise ValueError(err_msg)
            if errors == "ignore":
                return series

    # pass through integer_to_integer to sort out dtype, downcast args
    return integer_to_integer(ctx.result, dtype=dtype, downcast=downcast,
                              errors=errors, validate=False)


def string_to_float(
    series: str | array_like,
    dtype: dtype_like = float,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a string series into floats."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_string_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_float_dtype(dtype)
    _validate_errors(errors)

    # subset to avoid missing values
    with ExcludeMissing(series, np.nan, dtype) as ctx:
        ctx.subset = ctx.subset.str.replace(" ", "").str.lower()
        old_infs = ctx.subset.isin(("-inf", "-infinity", "inf", "infinity"))

        # attempt conversion
        try:  # all elements are valid
            ctx.subset = ctx.subset.astype(dtype, copy=False)
        except ValueError as err:  # parsing errors, apply error-handling rule
            if errors == "ignore":
                return series

            # find indices of elements that cannot be parsed
            def transcribe(element: str) -> bool:
                try:
                    float(element)
                    return False
                except ValueError:
                    return True

            invalid = np.frompyfunc(transcribe, 1, 1)(ctx.subset)
            if errors == "raise":
                bad = ctx.subset[invalid].index.values
                err_msg = (f"[{error_trace()}] non-numeric values detected at "
                           f"index {_shorten_list(bad)}")
                raise ValueError(err_msg) from err

            ctx.subset[invalid] = np.nan
            ctx.subset = ctx.subset.astype(dtype, copy=False)

        # scan for new infinities introduced by coercion
        new_infs = np.isinf(ctx.subset) ^ old_infs
        if new_infs.any():
            if errors == "raise":
                bad = ctx.subset[new_infs].index.values
                err_msg = (f"[{error_trace()}] values exceed {dtype} range at "
                           f"index {_shorten_list(bad)}")
                raise OverflowError(err_msg)
            if errors == "ignore":
                return series
            ctx.subset[new_infs] = np.nan

    # return
    if downcast:
        return _downcast_float_series(ctx.result)
    return ctx.result


def string_to_complex(
    series: str | array_like,
    dtype: dtype_like = complex,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a string series into complex numbers."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_string_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_complex_dtype(dtype)
    _validate_errors(errors)

    # subset to avoid missing values
    with ExcludeMissing(series, complex(np.nan, np.nan), dtype) as ctx:
        ctx.subset = ctx.subset.str.replace(" ", "").str.lower()
        old_infs = ctx.subset.str.match(r".*(inf|infinity).*")

        # attempt conversion
        try:  # all elements are valid
            ctx.subset = ctx.subset.astype(dtype, copy=False)
        except ValueError as err:  # parsing errors, apply error-handling rule
            if errors == "ignore":
                return series

            # find indices of elements that cannot be parsed
            def transcribe(element: str) -> bool:
                try:
                    complex(element)
                    return False
                except ValueError:
                    return True

            invalid = np.frompyfunc(transcribe, 1, 1)(ctx.subset)
            if errors == "raise":
                bad = ctx.subset[invalid].index.values
                err_msg = (f"[{error_trace()}] non-numeric values detected at "
                           f"index {_shorten_list(bad)}")
                raise ValueError(err_msg) from err

            # coerce
            ctx.subset[invalid] = complex(np.nan, np.nan)
            ctx.subset = ctx.subset.astype(dtype, copy=False)

        # scan for new infinities introduced by coercion
        real = np.real(ctx.subset)
        imag = np.imag(ctx.subset)
        new_infs = (np.isinf(real) | np.isinf(imag)) ^ old_infs
        if new_infs.any():
            if errors == "raise":
                bad = ctx.subset[new_infs].index.values
                err_msg = (f"[{error_trace()}] values exceed {dtype} range at "
                           f"index {_shorten_list(bad)}")
                raise OverflowError(err_msg)
            if errors == "ignore":
                return series
            ctx.subset[new_infs] = np.nan

    # TODO: python doesn't like the `real = real or x` construction when
    # downcast=True

    # return
    if downcast:
        return _downcast_complex_series(ctx.result, real, imag)
    return ctx.result


def string_to_decimal(
    series: str | array_like,
    dtype: dtype_like = decimal.Decimal,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """convert strings to arbitrary precision decimal objects."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_string_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_decimal_dtype(dtype)
    _validate_errors(errors)

    # for each element, attempt decimal coercion and note errors
    def transcribe(element: str) -> tuple[decimal.Decimal, bool]:
        try:
            return (decimal.Decimal(element.replace(" ", "")), False)
        except ValueError:
            return (pd.NA, True)

    # subset to avoid missing values
    with ExcludeMissing(series, pd.NA) as ctx:
        ctx.subset, invalid = np.frompyfunc(transcribe, 1, 2)(ctx.subset)
        if invalid.any():
            if errors == "raise":
                bad = ctx.subset[invalid].index.values
                err_msg = (f"[{error_trace()}] non-decimal values detected at "
                           f"index {_shorten_list(bad)}")
                raise ValueError(err_msg)
            if errors == "ignore":
                return series

    return ctx.result


def _string_to_pandas_timestamp(
    series: str | array_like,
    format: None | str = None,
    tz: None | str | datetime.tzinfo = "local",
    day_first: bool = False,
    year_first: bool = False,
    fuzzy: bool = False,
    errors: str = "raise"
) -> pd.Series:
    """Convert a datetime string series into `pandas.Timestamp` objects."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_string_series(series)
    _validate_format(format, day_first=day_first, year_first=year_first)
    tz = _parse_timezone(tz)
    _validate_errors(errors)

    # do conversion -> use pd.to_datetime with appropriate args
    if format:  # use specified format string
        result = pd.to_datetime(series, utc=True, format=format,
                                exact=not fuzzy, errors=errors)
    else:  # infer format
        result = pd.to_datetime(series, utc=True, dayfirst=day_first,
                                yearfirst=year_first,
                                infer_datetime_format=True, errors=errors)

    # catch immediate return from pd.to_datetime(..., errors="ignore")
    if errors == "ignore" and result.equals(series):
        return series

    # TODO: this last localize step uses LMT (local mean time) for dates prior
    # to 1902 for some reason.  This appears to be a known pytz limitation.
    # https://stackoverflow.com/questions/24188060/in-pandas-why-does-tz-convert-change-the-timezone-used-from-est-to-lmt
    # https://github.com/pandas-dev/pandas/issues/41834
    # solution: use zoneinfo.ZoneInfo instead once pandas supports it
    # https://github.com/pandas-dev/pandas/pull/46425
    return result.dt.tz_convert(tz)


def _string_to_pydatetime(
    series: str | array_like,
    format: None | str = None,
    tz: None | str | datetime.tzinfo = "local",
    day_first: bool = False,
    year_first: bool = False,
    fuzzy: bool = False,
    cache: bool = True,
    errors: str = "raise"
) -> pd.Series:
    """Convert a datetime string series into `datetime.datetime` objects."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_string_series(series)
    _validate_format(format, day_first=day_first, year_first=year_first)
    tz = _parse_timezone(tz)
    _validate_errors(errors)

    # subset to avoid missing values
    not_na = series.notna()
    subset = series[not_na]

    # gather resources for elementwise conversion
    def localize(date: datetime.datetime) -> datetime.datetime:
        if not tz and not date.tzinfo:
            return date
        if not tz:
            date = date.astimezone(datetime.timezone.utc)
            return date.replace(tzinfo=None)
        if not date.tzinfo:
            date = date.replace(tzinfo=datetime.timezone.utc)
        return date.astimezone(tz)
    # TODO: this last localize step uses LMT (local mean time) for dates prior
    # to 1902 for some reason.  This appears to be a known pytz limitation.
    # https://stackoverflow.com/questions/24188060/in-pandas-why-does-tz-convert-change-the-timezone-used-from-est-to-lmt
    # https://github.com/pandas-dev/pandas/issues/41834
    # solution: use zoneinfo.ZoneInfo instead once pandas supports it
    # https://github.com/pandas-dev/pandas/pull/46425

    if format is None:  # attempt to infer format
        infer = pd.core.tools.datetimes._guess_datetime_format_for_array
        format = infer(np.array(subset))
    parser_info = dateutil.parser.parserinfo(dayfirst=day_first,
                                             yearfirst=year_first)

    # TODO: this is actually almost exactly as fast as pd.to_datetime when
    # parsing is required, but significantly slower when it is not.

    # do conversion -> use an elementwise conversion func + dateutil
    if format:  # format is given or can be inferred
        def transcribe(date_string: str) -> datetime.datetime:
            date_string = date_string.strip()
            try:
                try:
                    result = datetime.datetime.strptime(date_string, format)
                    return localize(result)
                except ValueError:  # attempt flexible parse
                    result = dateutil.parser.parse(date_string, fuzzy=fuzzy,
                                                   parserinfo=parser_info)
                    return localize(result)
            except (dateutil.parser.ParserError, OverflowError) as err:
                if errors == "raise":
                    raise err
                if errors == "ignore":
                    raise RuntimeError() from err  # used as kill signal
                return pd.NaT
    else:  # format cannot be inferred -> skip strptime step
        def transcribe(date_string: str) -> datetime.datetime:
            date_string = date_string.strip()
            try:
                result = dateutil.parser.parse(date_string, fuzzy=fuzzy,
                                               parserinfo=parser_info)
                return localize(result)
            except (dateutil.parser.ParserError, OverflowError) as err:
                if errors == "raise":
                    raise err
                if errors == "ignore":
                    raise RuntimeError() from err  # used as kill signal
                return pd.NaT

    # apply caching if directed
    if cache:
        transcribe = functools.cache(transcribe)

    # attempt conversion
    try:
        subset = np.frompyfunc(transcribe, 1, 1)(subset)
    except RuntimeError:
        return series
    except (dateutil.parser.ParserError, OverflowError) as err:
        err_msg = (f"[{error_trace()}] unable to interpret string "
                   f"{repr(err.args[1])}")
        raise ValueError(err_msg) from err

    # reassign subset to series, accounting for missing values
    series = pd.Series(np.full(series.shape, pd.NaT, dtype="O"))
    series[not_na] = subset
    return series


def _string_to_numpy_datetime64(
    series: str | array_like,
    errors: str = "raise"
) -> pd.Series:
    """Convert a datetime string series into `numpy.datetime64` objects."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_string_series(series)
    _validate_errors(errors)

    # subset to avoid missing values
    not_na = series.notna()
    subset = series[not_na]

    # do conversion -> requires ISO format and does not carry tzinfo
    try:
        subset = pd.Series(list(subset.array.astype("M8")), dtype="O")
    except ValueError as err:
        # TODO: replicate "coerce" behavior
        if errors == "ignore":
            return series
        raise err

    series = pd.Series(np.full(series.shape, pd.NaT, dtype="O"))
    series[not_na] = subset
    return series


def string_to_datetime(
    series: str | array_like,
    format: None | str = None,
    tz: None | str | datetime.tzinfo = "local",
    day_first: bool = False,
    year_first: bool = False,
    fuzzy: bool = False,
    dtype: dtype_like = "datetime",
    errors: str = "raise"
) -> pd.Series:
    """Convert a string series to datetimes of the specified dtype."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_string_series(series)
    _validate_format(format, day_first, year_first)
    tz = _parse_timezone(tz)
    _validate_dtype_is_scalar(dtype)
    _validate_datetime_dtype(dtype)
    _validate_errors(errors)

    args = {
        "format": format,
        "tz": tz,
        "day_first": day_first,
        "year_first": year_first,
        "fuzzy": fuzzy,
        "errors": errors
    }

    # if dtype is a subtype of "datetime", return directly
    if is_dtype(dtype, pd.Timestamp):
        return _string_to_pandas_timestamp(series, **args)
    if is_dtype(dtype, datetime.datetime):
        return _string_to_pydatetime(series, **args)
    if is_dtype(dtype, np.datetime64):
        return _string_to_numpy_datetime64(series, errors=errors)

    # dtype is datetime superclass.  Try each and return most accurate.

    # try pd.Timestamp
    try:
        return _string_to_pandas_timestamp(series, **args)
    except (OverflowError, pd._libs.tslibs.np_datetime.OutOfBoundsDatetime,
            dateutil.parser.ParserError):
        pass

    # try datetime.datetime
    try:
        return _string_to_pydatetime(series, **args)
    except (ValueError, OverflowError, dateutil.parser.ParserError):
        pass

    # try np.datetime64
    if any((format, fuzzy, day_first, year_first)):
        err_msg = (f"[{error_trace()}] `numpy.datetime64` objects do not "
                   f"support arbitrary string parsing (string must be ISO "
                   f"8601-compliant)")
        raise TypeError(err_msg)
    if tz and tz not in ("UTC", datetime.timezone.utc, pytz.utc,
                         zoneinfo.ZoneInfo("UTC")):
        warn_msg = ("`numpy.datetime64` objects do not carry timezone "
                    "information - returned time is UTC")
        warnings.warn(warn_msg, RuntimeWarning)
    try:
        return _string_to_numpy_datetime64(series, errors=errors)
    except Exception as err:
        err_msg = (f"[{error_trace()}] could not convert string to any form "
                   f"of datetime object")
        raise ValueError(err_msg) from err


def _string_to_pandas_timedelta(
    series: str | array_like,
    errors: str = "raise"
) -> pd.Series:
    """Convert a string series into pd.Timedelta objects."""


def string_to_string(
    series: str | array_like,
    dtype: dtype_like = pd.StringDtype(),
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a string series to another string dtype."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_string_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_string_dtype(dtype)

    # do conversion
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)
