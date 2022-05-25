from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd
import pytz

from pdtypes.error import error_trace
from pdtypes.cast.util import _to_ns, total_nanoseconds


"""
Test Cases:
-   greater than 64-bit series
-   integer series that fit within uint64, but not int64
-   integer object series with None instead of nan or pd.NA
"""


def to_boolean(series: pd.Series,
               force: bool = False,
               dtype: type = bool) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_bool_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be bool-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)

    # check for information loss
    if force:
        if series.hasnans and not pd.api.types.is_extension_array_dtype(series):
            series = series.fillna(pd.NA)
        series = series.abs().clip(0, 1)
    elif series.min() < 0 or series.max() > 1:
        bad = series[(series < 0) | (series > 1)].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] could not convert integer to "
                       f"boolean: value out of range at index {list(bad)}")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] could not convert integer to "
                       f"boolean: value out of range at indices {list(bad)}")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] could not convert integer to "
                       f"boolean: value out of range at indices "
                       f"[{shortened}, ...] ({len(bad)})")
        raise OverflowError(err_msg)

    # return
    if series.hasnans:
        return series.astype(pd.BooleanDtype())
    return series.astype(dtype)


def to_integer(series: pd.Series, dtype: type = int) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_integer_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be int-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)

    # check whether series fits within specified dtype
    min_val = series.min()
    max_val = series.max()
    if dtype in (int, "int", "i") and (min_val < -2**63 or max_val > 2**63 - 1):
        if min_val >= 0 and max_val <= 2**64 - 1:  # > int64 but < uint64
            if series.hasnans:
                return series.astype(pd.UInt64Dtype())
            return series.astype(np.uint64)
        return series.apply(lambda x: pd.NA if pd.isna(x) else int(x))
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
            err_msg = (f"[{error_trace()}] series values do not fit within "
                       f"available range for {pandas_dtype} (index: "
                       f"{list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] series values do not fit within "
                       f"available range for {pandas_dtype} (indices: "
                       f"{list(bad)})")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] series values do not fit within "
                       f"available range for {pandas_dtype} (indices: "
                       f"[{shortened}, ...] ({len(bad)}))")
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


def to_float(series: pd.Series, dtype: type = float) -> pd.Series:
    if not pd.api.types.infer_dtype(series) == "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_float_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be float-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)

    # convert
    series = series.astype(dtype)

    # check for overflow
    if (series == np.inf).any():
        pandas_dtype = pd.api.types.pand
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
                       f"range for {pandas_dtype} (indices: {list(bad)})")
        raise OverflowError(err_msg)

    # return
    return series


def to_complex(series: pd.Series, dtype: type = complex) -> pd.Series:
    if not pd.api.types.infer_dtype(series) == "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_complex_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be complex-like "
                   f"(received: {dtype})")
        raise TypeError(err_msg)

    # convert
    series = series.astype(dtype)

    # check for overflow
    if (series == np.inf).any():
        pandas_dtype = pd.api.types.pand
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
                       f"range for {pandas_dtype} (indices: {list(bad)})")
        raise OverflowError(err_msg)

    # return
    return series


def to_decimal(series: pd.Series) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    return series.apply(lambda x: decimal.Decimal(np.nan) if pd.isna(x)
                                  else decimal.Decimal(x))


def to_datetime(
    series: pd.Series,
    unit: str = "s",
    offset: pd.Timestamp | datetime.datetime | None = None,
    tz: str | pytz.timezone | None = None) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    try:
        return pdtypes.cast.float.to_datetime(to_float(series),
                                              unit=unit, offset=offset, tz=tz)
    except Exception as err:
        err_msg = (f"[{error_trace()}] could not convert series to datetime")
        raise type(err)(err_msg) from err


def to_timedelta(
    series: pd.Series,
    unit: str = "s",
    offset: pd.Timedelta | datetime.timedelta | None = None) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # convert to nanoseconds and apply offset
    if offset:
        # casting to object prevents overflow
        series = series.astype(object) * _to_ns[unit] + total_nanoseconds(offset)
    else:
        series = series.astype(object) * _to_ns[unit]

    # get min/max to evaluate range
    min_val = series.min()
    max_val = series.max()

    # try to return as timedelta64[ns]
    if min_val >= -2**63 and max_val <= 2**63 - 1:
        return pd.to_timedelta(series, unit="ns")

    # try to return as datetime.timedelta
    if (min_val >= total_nanoseconds(datetime.timedelta.min) and
        max_val <= total_nanoseconds(datetime.timedelta.max) and
        not (series % 1000).any()):
        make_td = lambda x: datetime.timedelta(microseconds=int(x) // 1000)
        return series.apply(lambda x: pd.NaT if pd.isna(x) else make_td(x))

    # try to return as timedelta64 with consistent, non-ns units
    selected = None
    for unit in ("us", "ms", "s", "m", "h", "D", "W"):
        scale_factor = _to_ns[unit]
        if (series % scale_factor).any():
            break
        if (min_val // scale_factor >= -2**63 and
            max_val // scale_factor <= 2**63 - 1):
            selected = unit
    if selected:
        scale_factor = _to_ns[selected]
        make_td = lambda x: np.timedelta64(x // scale_factor, selected)
        return series.apply(lambda x: pd.NaT if pd.isna(x) else make_td(x))

    # try to return as timedelta64 with inconsistent units
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
    except ValueError:
        err_msg = (f"[{error_trace()}] series cannot be converted to "
                   f"any recognized timedelta format")
        raise ValueError(err_msg)


def to_string(series: pd.Series, dtype: type = str) -> pd.Series:
    if not pd.api.types.infer_dtype(series) == "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    # pandas is not picky about what constitutes a string dtype
    if (pd.api.types.is_object_dtype(dtype) or
        not pd.api.types.is_string_dtype(dtype)):
        err_msg = (f"[{error_trace()}] `dtype` must be string-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)
