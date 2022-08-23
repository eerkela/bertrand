from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd
import pytz
import tzlocal

from pdtypes.check import is_dtype, resolve_dtype, supertype
from pdtypes.error import error_trace, shorten_list
from pdtypes.util.time import _to_ns
from pdtypes.util.type_hints import dtype_like


####################################
####    Validation Functions    ####
####################################


def _validate_datetime_format(
    format: None | str,
    day_first: bool,
    year_first: bool
) -> None:
    # check format is a string or None
    if format is not None:
        if day_first or year_first:
            err_msg = (f"[{error_trace()}] `day_first` and `year_first` "
                       f"should not be used when `format` is given.")
            raise RuntimeError(err_msg)
        if not isinstance(format, str):
            err_msg = (f"[{error_trace()}] `format` must be a datetime "
                       f"format string or None, not {type(format)}")
            raise TypeError(err_msg)


def _validate_dtype(
    dtype: dtype_like,
    category: dtype_like
) -> None:
    if not is_dtype(dtype, category):
        sup_type = supertype(category)
        if isinstance(sup_type, type):
            sup_type = sup_type.__name__
        err_msg = (f"[{error_trace()}] `dtype` must be {sup_type}-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


def _validate_errors(errors: str) -> None:
    """Raise  a TypeError if `errors` isn't a string, and a ValueError if
    it is not one of the accepted error-handling rules ('raise', 'coerce',
    'ignore').
    """
    valid_errors = ("raise", "coerce", "ignore")
    if not isinstance(errors, str):
        err_msg = (f"[{error_trace()}] `errors` must be a string "
                   f"{valid_errors}, not {type(errors)}")
        raise TypeError(err_msg)
    if errors not in valid_errors:
        err_msg = (f"[{error_trace()}] `errors` must be one of "
                   f"{valid_errors}, not {repr(errors)}")
        raise ValueError(err_msg)


def _validate_rounding(rounding: None | str) -> None:
    """Raise a TypeError if `rounding` isn't None or a string, and a
    ValueError if it is not one of the accepted rounding rules ('floor',
    'ceiling', 'down', 'up', 'half_floor', 'half_ceiling', 'half_down',
    'half_up', 'half_even').
    """
    if rounding is None:
        return None

    valid_rules = ("floor", "ceiling", "down", "up", "half_floor",
                   "half_ceiling", "half_down", "half_up", "half_even")
    if not isinstance(rounding, str):
        err_msg = (f"[{error_trace()}] `rounding` must be a string in "
                   f"{valid_rules}, not {type(rounding)}")
        raise TypeError(err_msg)
    if rounding not in valid_rules:
        err_msg = (f"[{error_trace()}] `rounding` must be one of "
                   f"{valid_rules}, not {repr(rounding)}")
        raise ValueError(err_msg)

    return None


def _validate_unit(unit: str | np.ndarray | pd.Series) -> None:
    """Efficiently check whether an array of units is valid."""
    valid = list(_to_ns) + ["M", "Y"]
    if not np.isin(unit, valid).all():
        bad = list(np.unique(unit[~np.isin(unit, valid)]))
        err_msg = (f"[{error_trace()}] `unit` {shorten_list(bad)} not "
                   f"recognized: must be in {valid}")
        raise ValueError(err_msg)


####################
####    Misc    ####
####################


def integral_range(dtype: dtype_like) -> tuple[int, int]:
    """Get the integral range of a given integer, float, or complex dtype."""
    dtype = resolve_dtype(dtype)

    # integer case
    if is_dtype(dtype, int):
        # convert to pandas dtype to expose .itemsize attribute
        dtype = pd.api.types.pandas_dtype(dtype)
        bit_size = 8 * dtype.itemsize
        if pd.api.types.is_unsigned_integer_dtype(dtype):
            return (0, 2**bit_size - 1)
        return (-2**(bit_size - 1), 2**(bit_size - 1) - 1)

    # float case
    if is_dtype(dtype, float):
        significand_bits = {
            np.float16: 11,
            np.float32: 24,
            float: 53,
            np.float64: 53,
            np.longdouble: 64
        }
        extreme = 2**significand_bits[dtype]
        return (-extreme, extreme)

    # complex case
    if is_dtype(dtype, complex):
        significand_bits = {
            np.complex64: 24,
            complex: 53,
            np.complex128: 53,
            np.clongdouble: 64
        }
        extreme = 2**significand_bits[dtype]
        return (-extreme, extreme)

    # unrecognized
    err_msg = (f"[{error_trace()}] `dtype` must be int, float, or "
               f"complex-like, not {dtype}")
    raise TypeError(err_msg)


def localize_pydatetime(
    dt: datetime.datetime,
    tz: None | datetime.tzinfo
) -> datetime.datetime:
    """test"""
    # TODO: this is duplicated in cython.loops
    if not tz:  # return naive
        if dt.tzinfo:  # datetime is not naive
            dt = dt.astimezone(datetime.timezone.utc)
            return dt.replace(tzinfo=None)
        return dt

    # return aware
    if not dt.tzinfo:  # datetime is naive
        dt = dt.replace(tzinfo=datetime.timezone.utc)
    return dt.astimezone(tz)


def parse_timezone(tz: None | str | datetime.tzinfo) -> None | datetime.tzinfo:
    """test"""
    # validate timezone and convert to datetime.tzinfo object

    # TODO: localize step uses LMT (local mean time) for dates prior
    # to 1902 for some reason.  This appears to be a known pytz limitation.
    # https://stackoverflow.com/questions/24188060/in-pandas-why-does-tz-convert-change-the-timezone-used-from-est-to-lmt
    # https://github.com/pandas-dev/pandas/issues/41834
    # solution: use zoneinfo.ZoneInfo instead once pandas supports it
    # https://github.com/pandas-dev/pandas/pull/46425
    if isinstance(tz, str):
        if tz == "local":
            return pytz.timezone(tzlocal.get_localzone_name())
        return pytz.timezone(tz)
    if tz and not isinstance(tz, pytz.BaseTzInfo):
        err_msg = (f"[{error_trace(stack_index=2)}] `tz` must be a "
                   f"datetime.tzinfo object or an IANA-recognized timezone "
                   f"string, not {type(tz)}")
        raise TypeError(err_msg)
    return tz


def tolerance(
    tol: int | float | complex | decimal.Decimal
) -> tuple[int | float | decimal.Decimal, int | float | decimal.Decimal]:
    """test"""
    valid_types = (int, np.integer, float, np.floating, complex,
                   np.complexfloating, decimal.Decimal)
    if not isinstance(tol, valid_types):
        err_msg = (f"[{error_trace()}] `tol` must be a numeric >= 0, not "
                   f"{type(tol)}")
        raise TypeError(err_msg)

    # split into real and imaginary components
    if isinstance(tol, (complex, np.complexfloating)):
        real = np.real(tol)
        imag = np.imag(tol)
    else:
        real, imag = tol, tol

    # check both components are positive
    if real < 0 or imag < 0:
        err_msg = (f"[{error_trace()}] `tol` must be a numeric >= 0, not "
                   f"{tol}")
        raise ValueError(err_msg)

    return real, imag


