from __future__ import annotations
import datetime
import decimal
import re

import numpy as np
import pandas as pd
import pytz
import tzlocal

from pdtypes.error import error_trace


"""
Test cases:
-   np.timedelta64 with various units, from attoseconds to weeks
-   datetime.timedelta.max/min
-   pd.Timedelta.max/min
-   check every function returns a copy (no in-place modification)
-   mixed series.  datetime.timedelta.max cannot be represented as timedelta64
"""


_timedelta64_resolution_regex = re.compile(r'^[^\[]+\[([^\]]+)\]$')
_to_ns = {
    "as": 1e-9,
    "fs": 1e-6,
    "ps": 1e-3,
    "ns": 1,
    "nanosecond": 1,
    "nanoseconds": 1,
    "us": int(1e3),
    "microsecond": int(1e3),
    "microseconds": int(1e3),
    "ms": int(1e6),
    "millisecond": int(1e6),
    "milliseconds": int(1e6),
    "s": int(1e9),
    "sec": int(1e9),
    "second": int(1e9),
    "seconds": int(1e9),
    "m": 60 * int(1e9),
    "minute": 60 * int(1e9),
    "minutes": 60 * int(1e9),
    "h": 60 * 60 * int(1e9),
    "hour": 60 * 60 * int(1e9),
    "hours": 60 * 60 * int(1e9),
    "D": 24 * 60 * 60 * int(1e9),
    "day": 24 * 60 * 60 * int(1e9),
    "days": 24 * 60 * 60 * int(1e9),
    "W": 7 * 24 * 60 * 60 * int(1e9),
    "week": 7 * 24 * 60 * 60 * int(1e9),
    "weeks": 7 * 24 * 60 * 60 * int(1e9)
}


def total_nanoseconds(
    t: datetime.timedelta | pd.Timedelta | np.timedelta64) -> int:
    if isinstance(t, pd.Timedelta):
        return t.asm8.astype(int)
    if isinstance(t, datetime.timedelta):
        # casting to dtype="O" allows for >64-bit arithmetic
        coefficients = np.array([24 * 60 * 60 * int(1e9), int(1e9), int(1e3)],
                                dtype="O")
        components = np.array([t.days, t.seconds, t.microseconds], dtype="O")
        return np.sum(coefficients * components)
    if isinstance(t, np.timedelta64):
        unit = _timedelta64_resolution_regex.match(str(t.dtype)).group(1)
        return int(t.astype(int)) * _to_ns[unit]
    err_msg = (f"[{error_trace()}] could not interpret timedelta of type "
               f"{type(t)}")
    raise TypeError(err_msg)


def to_boolean(series: pd.Series,
               unit: str = "s",
               offset: datetime.timedelta | pd.Timestamp | None = None,
               force: bool = False,
               round: bool = False,
               tol: float = 1e-6,
               dtype: type = bool) -> pd.Series:
    series = to_integer(series, unit=unit, offset=offset, force=force,
                        round=round, tol=tol)

    # check for information loss
    if force:
        series = series.abs().clip(0, 1)
    elif series.min() < 0 or series.max() > 1:
        bad = series[(series < 0) | (series > 1)].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] could not convert timedelta to "
                       f"boolean without losing information (index: "
                       f"{list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] could not convert timedelta to "
                       f"boolean without losing information (indices: "
                       f"{list(bad)})")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] could not convert timedelta to "
                       f"boolean without losing information (indices: "
                       f"[{shortened}, ...] ({len(bad)}))")
        raise ValueError(err_msg)

    # return
    if series.hasnans:
        return series.astype(pd.BooleanDtype())
    return series.astype(dtype)


def to_integer(series: pd.Series,
               unit: str = "s",
               offset: datetime.timedelta | pd.Timedelta | None = None,
               force: bool = False,
               round: bool = False,
               tol: float = 1e-6,
               dtype: type = int) -> pd.Series:
    # determine offset amount in nanoseconds
    if offset is None:
        offset_ns = 0
    else:
        offset_ns = total_nanoseconds(offset)

    # convert to nanoseconds
    if pd.api.types.is_timedelta64_ns_dtype(series) and not series.hasnans:
        series = series.astype(int) + offset_ns  # fast
    else:  # slow, but universal
        series = series.apply(lambda x: None if pd.isna(x)
                                        else total_nanoseconds(x) + offset_ns,
                              convert_dtype=False)

    # round if appropriate
    scale_factor = _to_ns[unit]
    residuals = series % scale_factor
    series = series // scale_factor
    if round:  # always round
        round_up = (residuals >= scale_factor // 2)
        series[round_up] = series[round_up] + 1
    elif tol:  # round if within tolerance
        round_up = (residuals >= scale_factor - int(tol * scale_factor))
        residuals[round_up] = 0
        residuals[(residuals <= int(tol * scale_factor))] = 0
        series[round_up] = series[round_up] + 1

    # check for information loss
    if not (force or round) and residuals.any():
        bad = series[residuals > 0].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] could not convert timedelta to "
                       f"integer with unit {repr(unit)} (index: {list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] could not convert timedelta to "
                       f"integer with unit {repr(unit)} (indices: {list(bad)})")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] could not convert timedelta to "
                       f"integer with unit {repr(unit)} (indices: "
                       f"[{shortened}, ...] ({len(bad)}))")
        raise ValueError(err_msg)

    # check whether results fit within specified dtype
    min_val = series.min()
    max_val = series.max()
    if dtype in (int, "int", "i") and (min_val < -2**63 or max_val > 2**63 - 1):
        series[series.isna()] = None
        return series
    pandas_dtype = pd.api.types.pandas_dtype(dtype)
    if pd.api.types.is_unsigned_integer_dtype(dtype):
        min_poss = 0
        max_poss = 2**(8 * pandas_dtype.itemsize) - 1
    else:
        min_poss = -2**(8 * pandas_dtype.itemsize - 1)
        max_poss = 2**(8 * pandas_dtype.itemsize - 1) - 1
    if min_val < min_poss or max_val > max_poss:
        bad = series[(series < min_poss) | (series > max_poss)].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] series values exceed available "
                       f"range for {pandas_dtype} (index: {list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] series values exceed available "
                       f"range for {pandas_dtype} (indices: {list(bad)})")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] series values exceed available "
                       f"range for {pandas_dtype} (indices: "
                       f"[{shortened}, ...], ({len(bad)}))")
        raise OverflowError(err_msg)

    # convert and return
    if series.hasnans and not pd.api.types.is_extension_array_dtype(dtype):
        extension_types = {
            np.dtype(np.uint8): pd.UInt8Dtype(),
            np.dtype(np.uint16): pd.UInt16Dtype(),
            np.dtype(np.uint32): pd.UInt32Dtype(),
            np.dtype(np.uint64): pd.UInt64Dtype(),
            np.dtype(np.int8): pd.Int8Dtype(),
            np.dtype(np.int16): pd.Int16Dtype(),
            np.dtype(np.int32): pd.Int32Dtype(),
            np.dtype(np.int64): pd.Int64Dtype()
        }
        return series.astype(extension_types[np.dtype(dtype)])
    return series.astype(dtype)


def to_float(series: pd.Series,
             unit: str = "s",
             offset: datetime.timedelta | pd.Timedelta | None = None,
             dtype: type = float) -> pd.Series:
    series = to_integer(series, unit="ns", offset=offset, tol=0)
    series = series.astype(dtype) / _to_ns[unit]

    # check for overflow (np.inf)
    if series[series == np.inf].any():
        pandas_dtype = pd.api.types.pandas_dtype(dtype)
        bad = series[series == np.inf].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] series values exceed available "
                       f"range for {pandas_dtype} (index: {list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] series values exceed available "
                       f"range for {pandas_dtype} (indices: {list(bad)})")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] series values exceed available "
                       f"range for {pandas_dtype} (indices: "
                       f"[{shortened}, ...] ({len(bad)}))")
        raise OverflowError(err_msg)

    # return
    return series


def to_complex(series: pd.Series,
               unit: str = "s",
               offset: datetime.timedelta | pd.Timedelta | None = None,
               dtype: type = complex) -> pd.Series:
    series = to_integer(series, unit="ns", offset=offset, tol=0)
    if series.hasnans and pd.api.types.is_extension_array_dtype(series):
        # astype(complex) can't parse pd.NA -> convert to None
        series = series.astype(object)
        series[series.isna()] = None
    series = series.astype(dtype) / _to_ns[unit]

    # check for overflow (np.inf)
    if series[series == np.inf].any():
        pandas_dtype = pd.api.types.pandas_dtype(dtype)
        bad = series[series == np.inf].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] series values exceed available "
                       f"range for {pandas_dtype} (index: {list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] series values exceed available "
                       f"range for {pandas_dtype} (indices: {list(bad)})")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] series values exceed available "
                       f"range for {pandas_dtype} (indices: "
                       f"[{shortened}, ...] ({len(bad)}))")
        raise OverflowError(err_msg)

    # return
    return series


def to_decimal(
    series: pd.Series,
    unit: str = "s",
    offset: datetime.timedetla | pd.Timedelta | None = None) -> pd.Series:
    series = to_integer(series, unit="ns", offset=offset, tol=0)
    series = series.apply(lambda x: decimal.Decimal(np.nan) if pd.isna(x)
                                    else decimal.Decimal(x))
    return series / decimal.Decimal(_to_ns[unit])


def to_datetime(
    series: pd.Series,
    offset: datetime.datetime | pd.Timestamp | None = None,
    tz: str | pytz.timezone | None = "local") -> pd.Series:
    # TODO: update this to work in the general case

    # > 64 bit -> store as np.datetime64 objects
    if tz == "local":
        if offset and offset.tzinfo and offset.tzinfo.utcoffset(offset):
            tz = offset.tzinfo
        else:
            tz = tzlocal.get_localzone_name()
    if offset is None:
        offset = pd.Timestamp.fromtimestamp(0, "UTC")
    elif offset.tzinfo is None:
        return (series + offset).dt.tz_localize(tz)
    return (series + offset).dt.tz_convert(tz)


def to_timedelta(
    series: pd.Series,
    offset: datetime.timedelta | pd.Timedelta | None = None) -> pd.Series:
    original = series.copy()

    # attempt to return series as-is
    if pd.api.types.is_timedelta64_dtype(series):
        return original

    # series has object dtype -> infer objects
    series = series.infer_objects()
    if pd.api.types.is_timedelta64_dtype(series):
        return series

    # timedeltas don't fit within timedelta64[ns] -> try datetime.timedelta
    series = to_integer(series, unit="ns", offset=offset, tol=0)
    min_val = series.min()
    max_val = series.max()
    if (min_val >= total_nanoseconds(datetime.timedelta.min) and 
        max_val <= total_nanoseconds(datetime.timedelta.max)):
        conv = lambda x: (pd.NaT if pd.isna(x)
                          else datetime.timedelta(microseconds = x // 1000))
        return series.apply(conv)

    # timedeltas don't fit within datetime.timedelta -> try different units
    selected = None
    for unit in ("ms", "s", "m", "h", "D", "W"):
        scale_factor = _to_ns[unit]
        if (series % scale_factor).any():
            break
        if (min_val // scale_factor >= -2**63 and
            max_val // scale_factor <= 2**63 - 1):
            selected = unit
    if selected:    
        series = series // _to_ns[selected]
        return series.apply(lambda x: pd.NaT if pd.isna(x)
                                    else np.timedelta64(x, selected))

    # timedeltas don't fit within consistent timedelta64 units -> return mixed
    def to_timedelta64_any_precision(x):
        if pd.isna(x):
            return pd.NaT
        result = None
        for unit in ("ns", "us", "ms", "s", "m", "h", "D", "W"):
            scale_factor = _to_ns[unit]
            if x % scale_factor:
                break
            rescaled = x // scale_factor
            if -2**63 <= rescaled <= 2**63 - 1:
                result = np.timedelta64(rescaled, unit)
        if result:
            return result
        raise ValueError()  # stop at first bad value

    try:
        return series.apply(to_timedelta64_any_precision)
    except ValueError:  # everything else has failed, return original series
        return original


def to_string(series: pd.Series, dtype: type = str) -> pd.Series:
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)


def to_categorical(series: pd.Series,
                   categories: list | np.ndarray | pd.Series | None = None,
                   ordered: bool = False) -> pd.Series:
    values = pd.Categorical(series, categories=categories, ordered=ordered)
    return pd.Series(values)
