from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd
import pytz

from pdtypes.error import error_trace
import pdtypes.cast_old_2.float


"""
Test cases:
-   timestamps < pd.Timestamp.min, or > pd.Timestamp.max
-   mixed aware/naive
    pd.Series([datetime.datetime.now(), pd.Timestamp.now("UTC")])
-   mixed timezones
    pd.Series([pd.Timestamp.now("US/Eastern"), pd.Timestamp.now("US/Pacific")])
"""


def to_boolean(
    series: pd.Series,
    force: bool = False,
    round: bool = False,
    tol: float = 1e-6,
    unit: str = "s",
    offset: datetime.datetime | pd.Timestamp | None = None,
    dtype: type = bool) -> pd.Series:
    # input error checks
    inferred_series_dtype = pd.api.types.infer_dtype(series)
    if not (inferred_series_dtype in ("datetime", "datetime64") or
            (inferred_series_dtype == "categorical" and
             pd.api.types.infer_dtype(series.dtype.categories) in ("datetime", "datetime64"))):
        err_msg = (f"[{error_trace()}] `series` must contain datetime data, "
                   f"not {repr(inferred_series_dtype)}")
        raise TypeError(err_msg)
    if not pd.api.types.is_bool_dtype(dtype):
        err_msg = f"[{error_trace()}] `dtype` must be bool-like, not {dtype}"
        raise TypeError(err_msg)

    # convert
    try:
        series = to_float(series, unit=unit, offset=offset)
        return pdtypes.cast_old_2.float.to_boolean(series, force=force, round=round,
                                             tol=tol, dtype=dtype)
    except Exception as err:
        err_msg = f"[{error_trace()}] could not convert datetime to boolean"
        raise type(err)(err_msg) from err


def to_integer(series: pd.Series,
               force: bool = False,
               round: bool = True,
               tol: float = 1e-6,
               unit: str = "s",
               offset: datetime.datetime | pd.Timestamp | None = None,
               dtype: type = int) -> pd.Series:
    inferred_series_dtype = pd.api.types.infer_dtype(series)
    if not (inferred_series_dtype in ("datetime", "datetime64") or
            (inferred_series_dtype == "categorical" and
             pd.api.types.infer_dtype(series.dtype.categories) in ("datetime", "datetime64"))):
        err_msg = (f"[{error_trace()}] `series` must contain datetime data, "
                   f"not {repr(inferred_series_dtype)}")
        raise TypeError(err_msg)
    if not pd.api.types.is_integer_dtype(dtype):
        err_msg = f"[{error_trace()}] `dtype` must be int-like, not {dtype}"
        raise TypeError(err_msg)

    # if offset


    # timestamps = datetime_to_float(series, unit=unit, naive_tz=naive_tz)
    # return float_to_integer(timestamps, force=force, round=round, tol=tol)


def to_float(series: pd.Series,
             unit: str = "s",
             offset: datetime.datetime | pd.Timestamp | None = None,
             dtype: type = float,
             naive_tz: str | None = None) -> pd.Series:
    unit_conversion = {
        "ns": 1,
        "nanosecond": 1,
        "nanoseconds": 1,
        "us": 1e3,
        "microsecond": 1e3,
        "microseconds": 1e3,
        "ms": 1e6,
        "millisecond": 1e6,
        "milliseconds": 1e6,
        "s": 1e9,
        "second": 1e9,
        "seconds": 1e9,
        "m": 60 * 1e9,
        "minute": 60 * 1e9,
        "minutes": 60 * 1e9,
        "h": 60 * 60 * 1e9,
        "hour": 60 * 60 * 1e9,
        "hours": 60 * 60 * 1e9,
        "d": 24 * 60 * 60 * 1e9,
        "day": 24 * 60 * 60 * 1e9,
        "days": 24 * 60 * 60 * 1e9,
        "w": 7 * 24 * 60 * 60 * 1e9,
        "week": 7 * 24 * 60 * 60 * 1e9,
        "weeks": 7 * 24 * 60 * 60 * 1e9,
        "y": 365.2425 * 24 * 60 * 60 * 1e9,  # correcting for leap years
        "year": 365.2425 * 24 * 60 * 60 * 1e9,
        "years": 365.2425 * 24 * 60 * 60 * 1e9
    }
    if pd.api.types.is_object_dtype(series):  # possible mixed tz or aware/naive
        naive = series.apply(lambda x: x.tzinfo is None)
        if naive_tz is None:
            naive_tz = tzlocal.get_localzone_name()
        series.loc[naive] = pd.to_datetime(series[naive]).dt.tz_localize(naive_tz)
        series = pd.to_datetime(series, utc=True)
    return pd.to_numeric(series) / unit_conversion[unit.lower()]


def datetime_to_complex(series: pd.Series,
                        unit: str = "s",
                        naive_tz: str | None = None) -> pd.Series:
    timestamps = datetime_to_float(series, unit=unit, naive_tz=naive_tz)
    return float_to_complex(timestamps)


def datetime_to_string(series: pd.Series,
                       format: str = "") -> pd.Series:
    return series.dt.strftime(format)


def datetime_to_timedelta(series: pd.Series,
                          naive_tz: str | None = None) -> pd.Series:
    timestamps = datetime_to_float(series, unit="s", naive_tz=naive_tz)
    return pd.to_timedelta(timestamps, unit="s")
