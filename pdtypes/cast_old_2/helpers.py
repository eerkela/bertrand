from __future__ import annotations
import datetime
import decimal
import zoneinfo

import numpy as np
import pandas as pd
import pytz
import tzlocal

from pdtypes.check import check_dtype, get_dtype, is_dtype
from pdtypes.error import error_trace
from pdtypes.util.time import _to_ns
from pdtypes.util.type_hints import (
    array_like, datetime_like, dtype_like, timedelta_like
)


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
                   f"{_shorten_indices(bad)} not recognized: must be in "
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


def _shorten_indices(indices: array_like, max_length: int = 5) -> str:
    if len(indices) <= max_length:
        return str(list(indices))
    shortened = ", ".join(str(i) for i in indices[:max_length])
    return f"[{shortened}, ...] ({len(indices)})"


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
