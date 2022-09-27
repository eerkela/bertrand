"""Contains a variety of functions to parse and normalize arguments given to
pdtypes conversion functions (`convert_dtypes()`, `to_integer()`, etc.)
"""
from __future__ import annotations
import decimal

import numpy as np
import pandas as pd

from pdtypes.check import is_dtype, resolve_dtype
from pdtypes.error import error_trace, shorten_list
from pdtypes.time import valid_units
from pdtypes.util.type_hints import dtype_like


def tolerance(
    tol: int | float | complex | decimal.Decimal
) -> tuple[int | float | decimal.Decimal, int | float | decimal.Decimal]:
    """Ensure a floating-point tolerance is valid and split it into real and
    imaginary components.  Real input returns a 2-tuple `(real, real)`.
    """
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


def validate_datetime_format(
    format: None | str,
    day_first: bool,
    year_first: bool
) -> None:
    """Ensure that a datetime format is a string or `None`, and that it does
    not conflict with `day_first` or `year_first`.
    """
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


def validate_dtype(
    dtype: dtype_like,
    expected: dtype_like
) -> None:
    """Ensure that a dtype specifier is a subtype of `expected`."""
    if not is_dtype(dtype, expected):
        try:
            expected = resolve_dtype(expected).__name__
        except ValueError:  # may be supertype only
            if isinstance(expected, str):
                expected = expected.lower()

        err_msg = (f"[{error_trace()}] `dtype` must be {expected}-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


def validate_errors(errors: str) -> None:
    """Ensure `errors` is one of the accepted error-handling rules
    ('raise', 'coerce', 'ignore').
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


def validate_rounding(rounding: None | str) -> None:
    """Ensure that `rounding` is one of the accepted rounding rules
    ('floor', 'ceiling', 'down', 'up', 'half_floor', 'half_ceiling',
    'half_down', 'half_up', 'half_even').
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


def validate_unit(unit: str | np.ndarray | pd.Series) -> None:
    """Ensure that all elements of `unit` are valid time units ('ns', 'us',
    'ms', 's', 'm', 'h', 'D', 'W', 'M', 'Y'), following the numpy convention.
    """
    if not np.isin(unit, valid_units).all():
        bad = list(np.unique(unit[~np.isin(unit, valid_units)]))
        err_msg = (f"[{error_trace()}] `unit` {shorten_list(bad)} not "
                   f"recognized: must be in {valid_units}")
        raise ValueError(err_msg)
