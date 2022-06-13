from __future__ import annotations
import datetime
import decimal
import re

import numpy as np
import pandas as pd
import pytz
import tzlocal

import pdtypes.cast.integer
from pdtypes.error import error_trace
from pdtypes.util.downcast import (
    downcast_complex, downcast_float, downcast_int_dtype
)
from pdtypes.util.time import _to_ns, total_nanoseconds


"""
Test cases:
-   np.timedelta64 with various units, from attoseconds to weeks
-   datetime.timedelta.max/min
-   pd.Timedelta.max/min
-   check every function returns a copy (no in-place modification)
-   mixed series.  datetime.timedelta.max cannot be represented as timedelta64
"""


def to_boolean(series: pd.Series,
               unit: str = "ns",
               offset: datetime.timedelta | pd.Timestamp | None = None,
               force: bool = False,
               round: bool = False,
               tol: float = 1e-6,
               dtype: type = bool) -> pd.Series:
    series = to_integer(series, unit=unit, offset=offset, force=force,
                        round=round, tol=tol)
    return pdtypes.cast.integer.to_boolean(series, force=force, dtype=dtype)


def to_integer(
    series: pd.Series,
    unit: str = "ns",
    offset: pd.Timedelta | datetime.timedelta | np.timedelta64 | None = None,
    force: bool = False,
    round: bool = False,
    tol: float = 1e-6,
    downcast: bool = False,
    dtype: type = int
) -> pd.Series:
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

    # get min/max to evaluate range
    min_val = series.min()
    max_val = series.max()

    # built-in integer special case - can be arbitrarily large
    if dtype in (int, "int", "i") and (min_val < -2**63 or max_val > 2**63 - 1):
        series[series.isna()] = None
        return series

    # convert to pandas dtype to expose itemsize attribute
    dtype = pd.api.types.pandas_dtype(dtype)

    # check whether results fit within specified dtype
    if pd.api.types.is_unsigned_integer_dtype(dtype):
        min_poss = 0
        max_poss = 2**(8 * dtype.itemsize) - 1
    else:
        min_poss = -2**(8 * dtype.itemsize - 1)
        max_poss = 2**(8 * dtype.itemsize - 1) - 1
    if min_val < min_poss or max_val > max_poss:
        bad = series[(series < min_poss) | (series > max_poss)].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] series values exceed available "
                       f"range for {dtype} (index: {list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] series values exceed available "
                       f"range for {dtype} (indices: {list(bad)})")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] series values exceed available "
                       f"range for {dtype} (indices: "
                       f"[{shortened}, ...], ({len(bad)}))")
        raise OverflowError(err_msg)

    # attempt to downcast if applicable
    if downcast:
        dtype = downcast_int_dtype(min_val, max_val, dtype)

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
             downcast: bool = False,
             dtype: type = float) -> pd.Series:
    series = to_integer(series, unit="ns", offset=offset, tol=0)
    if np.issubdtype(dtype, np.longdouble):
        # preserve longdouble precision
        series = series.astype(dtype) / _to_ns[unit]
    else:
        # division automatically converts to np.float64
        series = (series / _to_ns[unit]).astype(dtype)

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

    # attempt to downcast if applicable
    if downcast:
        series = series.apply(downcast_float)

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
    tz: str | pytz.timezone | None = "local",
    dtype: type | str = np.datetime64
) -> pd.Series:
    series = to_integer(series, unit="ns")
    return pdtypes.cast.integer.to_datetime(series, unit="ns", offset=offset,
                                            tz=tz, dtype=dtype)


def to_timedelta(
    series: pd.Series,
    offset: datetime.timedelta | pd.Timedelta | None = None
) -> pd.Series:
    if offset:
        return series.fillna(pd.NaT) + offset
    return series.fillna(pd.NaT)


def to_string(series: pd.Series, dtype: type = str) -> pd.Series:
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)


def to_categorical(series: pd.Series,
                   categories: list | np.ndarray | pd.Series | None = None,
                   ordered: bool = False) -> pd.Series:
    values = pd.Categorical(series, categories=categories, ordered=ordered)
    return pd.Series(values)
