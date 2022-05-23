from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd
import pytz

from pdtypes.error import error_trace
import pdtypes.cast.float


def to_boolean(series: pd.Series,
               force: bool = False,
               round: bool = True,
               tol: float = 1e-6,
               dtype: type = bool) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "complex":
        err_msg = (f"[{error_trace()}] `series` must contain complex data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_bool_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be bool-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    try:  # convert to complex to float, then float to boolean
        series = to_float(series, force=force, tol=tol)
        return pdtypes.cast.float.to_boolean(series, force=force, round=round,
                                             tol=tol, dtype=dtype)
    except Exception as err:
        err_msg = (f"[{error_trace()}] could not convert complex to boolean")
        raise type(err)(err_msg) from err


def to_integer(series: pd.Series,
               force: bool = False,
               round: bool = True,
               tol: float = 1e-6,
               dtype: type = int) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "complex":
        err_msg = (f"[{error_trace()}] `series` must contain complex data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_integer_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be int-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    series = to_float(series, force=force, tol=tol)
    return pdtypes.cast.float.to_integer(series, force=force, round=round,
                                         tol=tol, dtype=dtype)


def to_float(series: pd.Series,
             force: bool = False,
             tol: float = 1e-6,
             dtype: type = float) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "complex":
        err_msg = (f"[{error_trace()}] `series` must contain complex data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_float_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be float-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)

    # np.real/np.imag doesn't work on raw objects
    if pd.api.types.is_object_dtype(series):
        complex_type = series.apply(lambda x: np.dtype(type(x))).max()
        series = series.astype(complex_type)

    # split series into real and imaginary components
    real = pd.Series(np.real(series))
    imag = pd.Series(np.imag(series))

    # check for information loss
    if not force and (imag.abs() > tol).any():
        bad = series[imag.abs() > tol].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] could not convert complex series "
                       f"to float without losing information: imaginary "
                       f"component exceeds tolerance at index {list(bad)}")
        else:  # plural
            err_msg = (f"[{error_trace()}] could not convert complex series "
                       f"to float without losing information: imaginary "
                       f"component exceeds tolerance at indices {list(bad)}")
        raise ValueError(err_msg)

    # return as appropriate float type
    if dtype in (float, "float", "f"):  # maintain same precision as complex
        return real
    return real.astype(dtype)


def to_complex(series: pd.Series, dtype: type = complex) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "complex":
        err_msg = (f"[{error_trace()}] `series` must contain complex data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_complex_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be complex-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    return series.astype(dtype)


def to_decimal(series: pd.Series,
               force: bool = False,
               tol: float = 1e-6) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "complex":
        err_msg = (f"[{error_trace()}] `series` must contain complex data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    series = to_float(series, force=force, tol=tol)
    return series.apply(lambda x: decimal.Decimal(np.nan) if pd.isna(x)
                                  else decimal.Decimal(x))


def to_datetime(series: pd.Series,
                force: bool = False,
                tol: float = 1e-6,
                unit: str = "s",
                offset: pd.Timestamp | datetime.datetime | None = None,
                tz: str | pytz.timezone | None = "local") -> pd.Series:
    if pd.api.types.infer_dtype(series) != "complex":
        err_msg = (f"[{error_trace()}] `series` must contain complex data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    series = to_float(series, force=force, tol=tol)
    return pdtypes.cast.float.to_datetime(series, unit=unit, offset=offset,
                                          tz=tz)


def to_timedelta(series: pd.Series,
                 force: bool = False,
                 tol: float = 1e-6,
                 unit: str = "s") -> pd.Series:
    if pd.api.types.infer_dtype(series) != "complex":
        err_msg = (f"[{error_trace()}] `series` must contain complex data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    series = to_float(series, force=force, tol=tol)
    return pdtypes.cast.float.to_timedelta(series, unit=unit)


def to_string(series: pd.Series, dtype: type = str) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "complex":
        err_msg = (f"[{error_trace()}] `series` must contain complex data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if (pd.api.types.is_object_dtype(dtype) or
        not pd.api.types.is_string_dtype(dtype)):
        err_msg = (f"[{error_trace()}] `dtype` must be string-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)
